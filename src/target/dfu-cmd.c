
#include "dfu-cmd.h"

#define DO_CMDBUF_CONTINUE 0
#define DO_CMDBUF_WAIT 1
#define DO_CMDBUF_ERROR 2
#define DO_CMDBUF_DONE 3

/* Write exactly len bytes or return error */
static int _do_send(struct dfu_interface *interface, const void *out,
		    unsigned int len)
{
	int stat;
	unsigned int sent;
	const char *ptr = out;

	for (sent = 0; sent < len; sent += stat) {
		stat = interface->ops->write(interface, &ptr[sent], len - sent);
		if (stat < 0)
			return stat;
	}
	return sent;
}

/* Read exactly len bytes or return error */
static int _do_read(struct dfu_interface *interface, void *in, unsigned int len)
{
	int stat;
	unsigned int recvd;
	char *ptr = in;

	for (recvd = 0; recvd < len; recvd += stat) {
		stat = interface->ops->read(interface, &ptr[recvd],
					    len - recvd);
		if (stat < 0)
			return stat;
	}
	return recvd;
}

static int _cmd_end(const struct dfu_cmddescr *descr, enum dfu_cmd_status s)
{
	struct dfu_cmdstate *state = descr->state;

	state->status = s;
	if (descr->completed)
		descr->completed(descr);
	return s < 0 ? DO_CMDBUF_ERROR : DO_CMDBUF_DONE;
}

static int _next_buf(const struct dfu_cmddescr *descr)
{
	struct dfu_cmdstate *state = descr->state;
	const struct dfu_cmdbuf *buf = &descr->cmdbufs[state->cmdbuf_index];
	int stat = 0;
	
	if (buf->completed)
		stat = buf->completed(descr, buf);
	if (stat < 0)
		return _cmd_end(descr, stat);
	state->cmdbuf_index++;
	state->status = DFU_CMD_STATUS_INITIALIZED;
	
	if (state->cmdbuf_index >= descr->ncmdbufs)
		return _cmd_end(descr, DFU_CMD_STATUS_OK);
	return DO_CMDBUF_CONTINUE;
}

static void _on_cmd_timeout(struct dfu_data *data, const void *priv)
{
	const struct dfu_cmddescr *descr = priv;

	descr->state->status = DFU_CMD_STATUS_TIMEOUT;
	descr->completed(descr);
}

static int _do_cmdbuf(struct dfu_target *target,
		      const struct dfu_cmddescr *descr,
		      const struct dfu_cmdbuf *buf)
{
	struct dfu_cmdstate *state = descr->state;
	struct dfu_interface *interface = target->interface;
	int stat;

	switch (buf->dir) {
	case OUT:
		if (descr->checksum_update)
			descr->checksum_update(descr, buf->buf.out, buf->len);
		stat = _do_send(interface, buf->buf.out, buf->len);
		if (stat < 0) {
			dfu_err("%s: error writing to interface\n",
				__func__);
			return _cmd_end(descr, -1);
		}
		if (buf->flags & SEND_CHECKSUM)
			stat = _do_send(interface, descr->checksum_ptr,
					descr->checksum_size);
		if (stat < 0) {
			dfu_err("%s: error sending checksum\n",
				__func__);
			return _cmd_end(descr, -1);
		}
		return _next_buf(descr);
	case IN:
		if (!buf->timeout) {
			/* Timeout is 0, do not wait */
			stat = _do_read(interface, buf->buf.in, buf->len);
			if (stat < 0) {
				dfu_err("%s: error reading from interface\n",
					__func__);
				return _cmd_end(descr, -1);
			}
		}
		if (state->status == DFU_CMD_STATUS_INITIALIZED) {
			state->received = 0;
			if (buf->timeout > 0 && !descr->timeout)
				dfu_err("%s: cannot setup timeout\n", __func__);
			if (buf->timeout > 0 && descr->timeout) {
				descr->timeout->timeout = buf->timeout;
				descr->timeout->cb = _on_cmd_timeout;
				descr->timeout->priv = descr;
				stat = dfu_set_timeout(target->dfu,
						       descr->timeout);
				if (stat < 0)
					return stat;
			}
		}
		stat = interface->ops->read(interface, buf->buf.in, buf->len);
		if (stat > 0)
			state->received += stat;
		if (state->received == buf->len)
			return _next_buf(descr);
		else
			state->status = DFU_CMD_STATUS_WAITING;
		break;
	default:
		dfu_err("%s: invalid buffer dir\n", __func__);
		return -1;
	}
	return 0;
}


int dfu_cmd_start(struct dfu_target *target, const struct dfu_cmddescr *descr)
{
	struct dfu_interface *interface = target->interface;
	const struct dfu_cmdbuf *ptr;
	int stat;
	struct dfu_cmdstate *state = descr->state;

	if (!interface || !interface->ops || !interface->ops->write ||
	    !interface->ops->read) {
		dfu_err("%s: cannot access interface\n", __func__);
		return -1;
	}
	if (!state) {
		dfu_err("%s: command has no state struct\n", __func__);
		return -1;
	}
	state->status = DFU_CMD_STATUS_INITIALIZED;
	state->cmdbuf_index = 0;
	ptr = descr->cmdbufs;
	do {
		stat = _do_cmdbuf(target, descr, ptr);
	} while(stat == DO_CMDBUF_CONTINUE);

	return stat == DO_CMDBUF_ERROR ? -1 : 0;
}

int dfu_cmd_on_interface_event(struct dfu_target *target,
			       const struct dfu_cmddescr *descr)
{
	/*
	 * We're waiting for the target to reply, go on with the current
	 * command buffer
	 */
	return _do_cmdbuf(target, descr,
			  &descr->cmdbufs[descr->state->cmdbuf_index]);
}

int dfu_cmd_do_sync(struct dfu_target *target,
		    const struct dfu_cmddescr *descr)
{
	if (dfu_cmd_start(target, descr) < 0)
		return -1;
	while (descr->state->status == DFU_CMD_STATUS_WAITING)
		dfu_idle(target->dfu);
	return descr->state->status == DFU_CMD_STATUS_OK ? 0 : -1;
}