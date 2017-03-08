
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
		stat = dfu_interface_write(interface, &ptr[sent], len - sent);
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
		stat = dfu_interface_read(interface, &ptr[recvd], len - recvd);
		if (stat < 0)
			return stat;
	}
	return recvd;
}

static int _cmd_end(struct dfu_target *target,
		    const struct dfu_cmddescr *descr, enum dfu_cmd_status s)
{
	struct dfu_cmdstate *state = descr->state;

	dfu_dbg("%s, s = %d\n", __func__, s);
	state->status = s;
	if (descr->completed)
		descr->completed(target, descr);
	dfu_target_set_ready(target);
	return s < 0 ? DO_CMDBUF_ERROR : DO_CMDBUF_DONE;
}

static int _next_buf(struct dfu_target *target,
		     const struct dfu_cmddescr *descr)
{
	struct dfu_cmdstate *state = descr->state;
	const struct dfu_cmdbuf *buf = &descr->cmdbufs[state->cmdbuf_index];
	int stat = 0;

	dfu_dbg("%s\n", __func__);
	if (buf->completed) {
		dfu_dbg("%s: completed\n", __func__);
		stat = buf->completed(descr, buf);
		dfu_dbg("%s: completed cb returns %d\n", __func__, stat);
	}
	if (!(buf->flags & RETRY_ON_ERROR)) {
		if (buf->timeout > 0)
			dfu_cancel_timeout(descr->timeout);
		if (stat < 0)
			return _cmd_end(target, descr, stat);
		state->cmdbuf_index++;
		state->status = DFU_CMD_STATUS_INITIALIZED;
	} else {
		/*
		 * Repeat on error: only advance to next command and cancel
		 * timeout if completed callback returned ok.
		 */
		if (stat >= 0 && buf->timeout > 0)
			dfu_cancel_timeout(descr->timeout);
		state->cmdbuf_index = stat < 0 ? buf->next_on_retry :
			state->cmdbuf_index + 1;
		if (stat < 0)
			state->status = DFU_CMD_STATUS_RETRYING;
	}
	
	if (state->cmdbuf_index >= descr->ncmdbufs &&
	    state->status != DFU_CMD_STATUS_RETRYING)
		return _cmd_end(target, descr, DFU_CMD_STATUS_OK);
	return DO_CMDBUF_CONTINUE;
}

static void _on_cmd_timeout(struct dfu_data *data, const void *priv)
{
	const struct dfu_cmddescr *descr = priv;

	descr->state->status = DFU_CMD_STATUS_TIMEOUT;
	if (descr->completed)
		descr->completed(data->target, descr);
	dfu_target_set_ready(data->target);
}

#ifdef DEBUG
static inline void debug_print_out(const struct dfu_cmdbuf *buf)
{
	int i;
	const char *ptr = buf->buf.out;
	dfu_dbg("---> ");
	for (i = 0; i < buf->len; i++)
		dfu_log_noprefix("0x%02x ", ptr[i]);
	dfu_log_noprefix("\n");
}

static inline void debug_print_checksum(const void *_ptr, int size)
{
	int i;
	const char *ptr = _ptr;
	dfu_dbg("CHK ---> ");
	for (i = 0; i < size; i++)
		dfu_log_noprefix("0x%02x ", ptr[i]);
	dfu_log_noprefix("\n");
}
#else
static inline void debug_print_out(const struct dfu_cmdbuf *buf)
{
}

static inline void debug_print_checksum(const void *ptr, int size)
{
}
#endif

static int _do_cmdbuf(struct dfu_target *target,
		      const struct dfu_cmddescr *descr,
		      const struct dfu_cmdbuf *buf)
{
	struct dfu_cmdstate *state = descr->state;
	struct dfu_interface *interface = target->interface;
	char *ptr;
	int stat;
	char dummy_buf[8];

	if (state->status == DFU_CMD_STATUS_INITIALIZED) {
		if (buf->timeout > 0 && !descr->timeout)
			dfu_err("%s: cannot setup timeout\n", __func__);
		if (buf->timeout > 0 && descr->timeout) {
			dfu_dbg("%s: setup timeout (%d)\n", __func__,
				buf->timeout);
			descr->timeout->timeout = buf->timeout;
			descr->timeout->cb = _on_cmd_timeout;
			descr->timeout->priv = descr;
			stat = dfu_set_timeout(target->dfu,
					       descr->timeout);
			if (stat < 0)
				return stat;
		}
	}

	if (buf->flags & START_CHECKSUM) {
		if (descr->checksum_reset)
			descr->checksum_reset(descr);
		else
			memset(descr->checksum_ptr, 0, descr->checksum_size);
	}

	switch (buf->dir) {
	case OUT:
		dfu_dbg("%s OUT\n", __func__);
		if (descr->checksum_update)
			descr->checksum_update(descr, buf->buf.out, buf->len);
		/* Flush interface first */
		if (dfu_interface_has_read(interface))
			dfu_interface_read(interface, dummy_buf,
					   sizeof(dummy_buf));
		debug_print_out(buf);
		stat = _do_send(interface, buf->buf.out, buf->len);
		if (stat < 0) {
			dfu_err("%s: error writing to interface\n",
				__func__);
			return _cmd_end(target, descr, -1);
		}
		if (buf->flags & SEND_CHECKSUM) {
			debug_print_checksum(descr->checksum_ptr,
					     descr->checksum_size);
			stat = _do_send(interface, descr->checksum_ptr,
					descr->checksum_size);
		}
		if (stat < 0) {
			dfu_err("%s: error sending checksum\n",
				__func__);
			return _cmd_end(target, descr, -1);
		}
		return _next_buf(target, descr);
	case IN:
		dfu_dbg("%s IN\n", __func__);
		if (!buf->timeout) {
			dfu_dbg("%s: no timeout\n", __func__);
			/* Timeout is 0, do not wait */
			stat = _do_read(interface, buf->buf.in, buf->len);
			if (stat < 0) {
				dfu_err("%s: error reading from interface\n",
					__func__);
				return _cmd_end(target, descr, -1);
			}
		}
		if (state->status == DFU_CMD_STATUS_INITIALIZED ||
		    state->status == DFU_CMD_STATUS_RETRYING)
			state->received = 0;
		ptr = buf->buf.in;
		stat = dfu_interface_read(interface,
					  &ptr[state->received],
					  buf->len - state->received);
		dfu_dbg("%s: read returns %d\n", __func__, stat);
		if (stat > 0)
			state->received += stat;
		if (state->received == buf->len) {
			dfu_dbg("%s: next buf\n", __func__);
			return _next_buf(target, descr);
		}
		state->status = DFU_CMD_STATUS_WAITING;
		return DO_CMDBUF_WAIT;
	case OUT_IN:
		/*
		 * This is for spi based interfaces: spi can write and read
		 * at the same time. Write and read are synchronous, so we
		 * don't have to wait for the target to reply
		 */
		dfu_dbg("%s OUT_IN\n", __func__);
		if (buf->flags & SEND_CHECKSUM) {
			dfu_err("%s: checksum not supported for OUT_IN cmds\n",
				__func__);
			return _cmd_end(target, descr, -1);
		}
		if (!dfu_interface_has_write_read(interface)) {
			dfu_err("%s: write_read() not supported\n", __func__);
			return _cmd_end(target, descr, -1);
		}
		debug_print_out(buf);
		stat = dfu_interface_write_read(interface,
						buf->buf.out,
						buf->buf.in,
						buf->len);
		if (stat < 0) {
			dfu_err("%s: interface's write_read returns error\n",
				__func__);
			return _cmd_end(target, descr, -1);
		}
		return _next_buf(target, descr);
	default:
		dfu_err("%s: invalid buffer dir\n", __func__);
		return -1;
	}
	return 0;
}


int dfu_cmd_start(struct dfu_target *target, const struct dfu_cmddescr *descr)
{
	struct dfu_interface *interface = target->interface;
	int stat;
	struct dfu_cmdstate *state = descr->state;

	if (!interface || !dfu_interface_has_write(interface) ||
	    (!dfu_interface_has_read(interface) &&
	     !dfu_interface_has_write_read(interface))) {
		dfu_err("%s: cannot access interface\n", __func__);
		return -1;
	}
	if (!state) {
		dfu_err("%s: command has no state struct\n", __func__);
		return -1;
	}
	dfu_target_set_busy(target);
	state->status = DFU_CMD_STATUS_INITIALIZED;
	state->cmdbuf_index = 0;
	stat = _do_cmdbuf(target, descr,
			  &descr->cmdbufs[state->cmdbuf_index]);
	dfu_dbg("%s: _do_cmdbuf returns %d\n", __func__, stat);

	return stat == DO_CMDBUF_ERROR ? -1 : 0;
}

int dfu_cmd_on_interface_event(struct dfu_target *target,
			       const struct dfu_cmddescr *descr)
{
	char dummy_buf[8];
	struct dfu_interface *interface = target->interface;

	if (!descr)
		return 0;

	/*
	 * We're waiting for the target to reply, go on with the current
	 * command buffer
	 */
	dfu_dbg("%s, status = %d\n", __func__, descr->state->status);
	if (descr->state->status == DFU_CMD_STATUS_WAITING) {
		descr->state->status = DFU_CMD_STATUS_INTERFACE_READY;
		return 0;
	}
	if (descr->state->status == DFU_CMD_STATUS_OK ||
	    descr->state->status == DFU_CMD_STATUS_ERROR) {
		/* Flush interface */
		dfu_dbg("%s: flushing interface, status = %d\n", __func__,
			descr->state->status);
		dfu_interface_read(interface, dummy_buf, sizeof(dummy_buf));
	}
	return 0;
}

int dfu_cmd_on_idle(struct dfu_target *target,
		    const struct dfu_cmddescr *descr)
{
	int stat = 0;

	if (descr->state->status == DFU_CMD_STATUS_INITIALIZED ||
	    descr->state->status == DFU_CMD_STATUS_RETRYING ||
	    descr->state->status == DFU_CMD_STATUS_INTERFACE_READY) {
		stat = _do_cmdbuf(target, descr,
				  &descr->cmdbufs[descr->state->cmdbuf_index]);
		if (stat < 0)
			dfu_err("%s %d\n", __func__, __LINE__);
	}
	return stat;
}

int dfu_cmd_do_sync(struct dfu_target *target,
		    const struct dfu_cmddescr *descr)
{
	if (dfu_cmd_start(target, descr) < 0)
		return -1;
	while (descr->state->status == DFU_CMD_STATUS_INITIALIZED ||
	       descr->state->status == DFU_CMD_STATUS_RETRYING ||
	       descr->state->status == DFU_CMD_STATUS_WAITING ||
	       descr->state->status == DFU_CMD_STATUS_INTERFACE_READY) {
			if (dfu_idle(target->dfu) == DFU_CONTINUE)
				continue;
			break;
	}
	dfu_dbg("%s returns, status = %d\n", __func__, descr->state->status);
	return descr->state->status;
}
