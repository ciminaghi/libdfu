/*
 * AVR ISP (via SPI) update protocol implementation
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2017
 */

#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-cmd.h"
#include "stk500-device.h"
#include "dfu-avrisp.h"

#define MAX_SYNC_ATTEMPTS 8

struct avrisp_data {
	struct dfu_cmdstate cmd_state;
	struct dfu_timeout cmd_timeout;
	const struct dfu_cmddescr *curr_descr;
	phys_addr_t curr_chunk_addr;
	unsigned long curr_written;
	unsigned long to_write;
	const char *page_buf;
};

static uint8_t cmd_buffer[4];

/*
 * Just one command shall be active at any time.
 */
static struct avrisp_data data;

/* Command callbacks */

/* Target methods */


static int avrisp_init(struct dfu_target *target,
		       struct dfu_interface *interface)
{
	static struct avrisp_data priv;

	target->interface = interface;
	memset(&priv, 0, sizeof(priv));
	target->priv = &priv;
	if (!target->pars) {
		dfu_err("%s: target parameters expected\n", __func__);
		return -1;
	}
	dfu_log("AVRISP target initialized\n");
	return 0;
}

static int _check_get_sync(const struct dfu_cmddescr *descr,
			   const struct dfu_cmdbuf *buf)
{
	char *ptr = buf->buf.in;

	dfu_dbg("%s: ptr[0] = 0x%02x (expected 0x%02x)\n", __func__, ptr[0],
		0x53);
	return ptr[0] == 0x53 ? 0 : -1;
}

/* Probe by sending enter programming mode instruction and reading reply */

static int _get_sync(struct dfu_target *target)
{
	struct avrisp_data *priv = target->priv;
	const struct stk500_device_data *dd = target->pars;
	static uint8_t sync_reply[4];
	int ret;
	static const struct dfu_cmdbuf cmdbufs0[] = {
		/* Send sync */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = &cmd_buffer,
				.in = &sync_reply,
			},
			/* Send 0xac, write only */
			.len = 1,
		},
		[1] = {
			.dir = OUT_IN,
			.buf = {
				.out = &cmd_buffer[1],
				.in = &sync_reply,
			},
			/*
			 * Send 0x53 0x00, write + read: read phase
			 * implicitly sends a 0x00
			 */
			.len = 1,
			.timeout = 500,
			.completed = _check_get_sync,
		},
		[2] = {
			.dir = OUT,
			.buf = {
				.out = &cmd_buffer[3],
				.in = &sync_reply,
			},
			/* Send 0x00, write only */
			.len = 1,
		},
	};
	static const struct dfu_cmddescr descr0 = {
		.cmdbufs = cmdbufs0,
		.ncmdbufs = ARRAY_SIZE(cmdbufs0),
		.checksum_ptr = NULL,
		.checksum_size = 0,
		.state = &data.cmd_state,
		.timeout = &data.cmd_timeout,
		.checksum_reset = NULL,
		.checksum_update = NULL,
		.completed = NULL,
	};
	dfu_dbg("syncing target\n");
	priv->curr_descr = &descr0;
	memcpy(cmd_buffer, dd->enter_progmode, sizeof(sync_reply));
	ret = dfu_cmd_do_sync(target, &descr0);
	if (!ret)
		dfu_dbg("sync ok\n");
	else
		dfu_err("cannot sync target\n");
	return ret;
}

static int avrisp_probe(struct dfu_target *target)
{
	return 0;
}

static int _check_page_written(const struct dfu_cmddescr *descr,
			       const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;

	/*
	 * atmega328p manual, page 297:
	 * If the LSB in RDY/BSY data byte out is '1', a programming
	 * operation is still pending. Wait until this bit returns '0'
	 * before the next instruction is carried out.
	 */
	dfu_dbg("%s: ptr[0] = 0x%02x\n", __func__, ptr[0]);
	return ptr[0] & 0x1 ? -1 : 0;
}

static void _chunk_done(struct dfu_target *target,
			const struct dfu_cmddescr *descr)
{
	struct avrisp_data *priv = target->priv;

	if (descr->state->status == DFU_CMD_STATUS_OK)
		dfu_dbg("chunk 0x%08x programmed OK\n",
			(unsigned int)priv->curr_chunk_addr);
	dfu_binary_file_chunk_done(target->dfu->bf, priv->curr_chunk_addr,
				   descr->state->status == DFU_CMD_STATUS_OK ?
				   0 : -1);
}

static int _write_page(struct dfu_target *target)
{
	struct avrisp_data *priv = target->priv;
	static uint8_t reply[4];
	static const uint8_t poll_request[] = { 0xf0, 0x00, 0x00, 0x00, };
	const struct stk500_device_data *dd = target->pars;
	static struct dfu_cmdbuf cmdbufs0[] = {
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = 4,
		},
		[1] = {
			.dir = OUT,
			.buf = {
				.out = poll_request,
			},
			/* 0xf0 0x00 : write, don't read */
			.len = 2,
		},
		[2] = {
			.dir = OUT_IN,
			.buf = {
				.out = &poll_request[2],
				.in = reply,
			},
			/* 0x00: write + read : this adds one more 0 */
			.len = 1,
			.completed = _check_page_written,
			/*
			 * On error (page not yet written), jump back to
			 * step 1
			 */
			.flags = RETRY_ON_ERROR,
			.next_on_retry = 1,
		},
	};
	static const struct dfu_cmddescr descr0 = {
		.cmdbufs = cmdbufs0,
		.ncmdbufs = ARRAY_SIZE(cmdbufs0),
		.checksum_ptr = NULL,
		.checksum_size = 0,
		.state = &data.cmd_state,
		.timeout = &data.cmd_timeout,
		.checksum_reset = NULL,
		.checksum_update = NULL,
		.completed = _chunk_done,
	};
	phys_addr_t address = priv->curr_chunk_addr >> 1;
	phys_addr_t mask = ~((dd->flash->page_size >> 1) - 1);
	phys_addr_t page_address = address & mask;

	cmd_buffer[0] = 0x4c;
	cmd_buffer[1] = (page_address >> 8) & 0xff;
	cmd_buffer[2] = page_address & 0xff;
	cmd_buffer[3] = 0;
	priv->curr_descr = &descr0;
	return dfu_cmd_start(target, &descr0);
}

static int _load_byte(struct dfu_target *target)
{
	struct avrisp_data *priv = target->priv;
	static uint8_t reply[4];
	const struct stk500_device_data *dd = target->pars;
	static struct dfu_cmdbuf cmdbufs0[] = {
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = sizeof(reply),
		},
	};
	static const struct dfu_cmddescr descr0 = {
		.cmdbufs = cmdbufs0,
		.ncmdbufs = ARRAY_SIZE(cmdbufs0),
		.checksum_ptr = NULL,
		.checksum_size = 0,
		.state = &data.cmd_state,
		.timeout = &data.cmd_timeout,
		.checksum_reset = NULL,
		.checksum_update = NULL,
	};
	int ret;
	phys_addr_t mask = ((dd->flash->page_size >> 1) - 1);
	phys_addr_t address = ((priv->curr_chunk_addr + priv->curr_written)
			       >> 1) & mask;
	int hi = priv->curr_written & 0x1;

	cmd_buffer[0] = hi ? 0x48 : 0x40;
	cmd_buffer[1] = (address >> 8) & 0xff;
	cmd_buffer[2] = address & 0xff;
	cmd_buffer[3] = priv->page_buf[priv->curr_written++];
	ret = dfu_cmd_do_sync(target, &descr0);
	if (ret < 0) {
		priv->curr_descr = NULL;
		priv->to_write = 0;
		if (priv->curr_written == 1) {
			/* First byte, no need to call chunk_done */
			priv->curr_written = 0;
			return ret;
		}
		priv->curr_written = 0;
	}
	if (priv->curr_written >= priv->to_write) {
		if (ret >= 0) {
			ret = _write_page(target);
			priv->to_write = 0;
		}
		if (ret < 0)
			dfu_binary_file_chunk_done(target->dfu->bf,
						   priv->curr_chunk_addr,
						   ret);
	}
	return 0;
}

/* Chunk of binary data is available for writing */
static int avrisp_chunk_available(struct dfu_target *target,
				  phys_addr_t address,
				  const void *buf, unsigned long sz)
{
	struct avrisp_data *priv = target->priv;
	const struct stk500_device_data *dd = target->pars;

	if (!dd->flash) {
		dfu_err("%s: target has no flash\n", __func__);
		return -1;
	}

	priv->curr_chunk_addr = address;
	priv->curr_written = 0;
	priv->to_write = sz;
	priv->page_buf = buf;
	return _load_byte(target);
}

static int avrisp_target_erase_all(struct dfu_target *target)
{
	const struct stk500_device_data *dd = target->pars;
	struct avrisp_data *priv = target->priv;
	static uint8_t reply[4];
	static struct dfu_cmdbuf cmdbufs0[] = {
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
				.in = reply,
			},
			.len = sizeof(reply),
		},
	};
	static const struct dfu_cmddescr descr0 = {
		.cmdbufs = cmdbufs0,
		.ncmdbufs = ARRAY_SIZE(cmdbufs0),
		.checksum_ptr = NULL,
		.checksum_size = 0,
		.state = &data.cmd_state,
		.timeout = &data.cmd_timeout,
		.checksum_reset = NULL,
		.checksum_update = NULL,
	};
	int ret;

	priv->curr_descr = &descr0;
	memcpy(cmd_buffer, dd->chip_erase, sizeof(reply));
	ret = dfu_cmd_do_sync(target, &descr0);
	if (ret < 0)
		return ret;
	/* This can be done synchronously (takes a few msecs) */
	if (dfu_udelay(target->dfu, dd->chip_erase_delay) < 0)
		return -1;
	return ret;
}

/* Reset and sync target */
static int avrisp_reset_and_sync(struct dfu_target *target)
{
	int stat = 0, i;
	struct dfu_interface *interface = target->interface;

	for (i = 0; i < 50; i++) {
		if (dfu_interface_has_target_reset(interface))
			stat = dfu_interface_target_reset(interface);
		if (stat < 0)
			return stat;
		if (_get_sync(target) >= 0) {
			dfu_log("AVRISP target ready\n");
			return 0;
		}
		dfu_udelay(target->dfu, 1000);
	}
	dfu_err("AVRISP: could not sync target\n");
	return -1;
}

/* Let target run */

/* Just reset target */
static int avrisp_run(struct dfu_target *target)
{
	if (!dfu_interface_has_target_run(target->interface))
		return -1;
	return dfu_interface_target_run(target->interface);
}

/* Interface event */
static int avrisp_on_interface_event(struct dfu_target *target)
{
	struct avrisp_data *priv = target->priv;

	return dfu_cmd_on_interface_event(target, priv->curr_descr);
}

static int avrisp_on_idle(struct dfu_target *target)
{
	struct avrisp_data *priv = target->priv;

	if (!priv->curr_descr)
		return 0;
	if (priv->to_write)
		/*
		 * We're loading a page before trying to write it, just load
		 * a single byte
		 */
		return _load_byte(target);
	/* We're writing a page, which is an asynchronous process */
	return dfu_cmd_on_idle(target, priv->curr_descr);
}

static int avrisp_get_write_chunk_size(struct dfu_target *target)
{
	return 128;
}

struct dfu_target_ops avrisp_dfu_target_ops = {
	.init = avrisp_init,
	.probe  = avrisp_probe,
	.chunk_available = avrisp_chunk_available,
	.reset_and_sync = avrisp_reset_and_sync,
	.erase_all = avrisp_target_erase_all,
	.run = avrisp_run,
	.on_interface_event = avrisp_on_interface_event,
	.on_idle = avrisp_on_idle,
	.get_write_chunk_size = avrisp_get_write_chunk_size,
};
