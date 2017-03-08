/*
 * STK500v1 firmware update protocol implementation
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2016
 */

#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-cmd.h"
#include "stk500-device.h"

#define MAX_SYNC_ATTEMPTS 8

/* Commands */
enum stk500_cmd {
	STK_GET_SIGN_ON = 0x31,
	STK_GET_SYNC = 0x30,
	STK_GET_PARAMETER = 0x41,
	STK_SET_PARAMETER = 0x40,
	STK_SET_DEVICE = 0x42,
	STK_SET_DEVICE_EXT = 0x45,
	STK_ENTER_PROGMODE = 0x50,
	STK_LEAVE_PROGMODE = 0x51,
	STK_CHIP_ERASE = 0x52,
	STK_CHECK_AUTOINC = 0x53,
	STK_LOAD_ADDRESS = 0x55,
	STK_UNIVERSAL = 0x56,
	STK_PROG_FLASH = 0x60,
	STK_PROG_DATA = 0x61,
	STK_PROG_FUSE = 0x62,
	STK_PROG_FUSE_EXT = 0x65,
	STK_PROG_LOCK = 0x63,
	STK_PROG_PAGE = 0x64,
	STK_READ_FLASH = 0x70,
	STK_READ_DATA = 0x71,
	STK_READ_FUSE = 0x72,
	STK_READ_LOCK = 0x73,
	STK_READ_PAGE = 0x74,
	STK_READ_SIGN = 0x75,
	STK_READ_OSCCAL = 0x76,
	STK_READ_FUSE_EXT = 0x77,
	STK_READ_OSCCAL_EXT = 0x78,
};

/* Response codes */
enum stk500_resp {
	STK_OK = 0x10,
	STK_FAILED = 0x11,
	STK_UNKNOWN = 0x12,
	STK_NODEVICE = 0x13,
	STK_INSYNC = 0x14,
	STK_NOSYNC = 0x15,
	ADC_CHANNEL_ERROR = 0x16,
	ADC_MEASURE_OK = 0x17,
	PWM_CHANNEL_ERROR = 0x18,
	PWM_ADJUST_OK = 0x19,
};

/* Special constants */
#define STK_CRC_EOP 0x20

/* Parameters */
enum stk500_param {
	STK_HW_VER = 0x80,
	STK_SW_MAJOR = 0x81,
	STK_SW_MINOR = 0x82,
	STK_LEDS = 0x83,
	STK_VTARGET = 0x84,
	STK_VADJUST = 0x85,
	STK_OSC_PSCALE = 0x86,
	STK_OSC_CMATCH = 0x87,
	STK_RESET_DURATION = 0x88,
	STK_SCK_DURATION = 0x89,
	STK_BUFSIZEL = 0x90,
	STK_BUFSIZEH = 0x91,
	STK_DEVICE = 0x92,
	STK_PROGMODE = 0x93,
	STK_PARAMODE = 0x94,
	STK_POLLING = 0x95,
	STK_SELFTIMED = 0x96,
	STK500_TOPCARD_DETECT = 0x98,
};

struct stk500_data {
	struct dfu_cmdstate cmd_state;
	uint8_t checksum;
	struct dfu_timeout cmd_timeout;
	const struct dfu_cmddescr *curr_descr;
	int busy;
	phys_addr_t curr_chunk_addr;
};

static uint8_t cmd_buffer[32];

/*
 * Just one command shall be active at any time.
 */
static struct stk500_data data;

/* Command callbacks */

#if 0
static void stk500_completed(struct dfu_cmddescr *descr)
{
	dfu_log("%s !!\n", __func__);
}
#endif

/* Target methods */


static int stk500_init(struct dfu_target *target,
		       struct dfu_interface *interface)
{
	static struct stk500_data priv;

	target->interface = interface;
	memset(&priv, 0, sizeof(priv));
	target->priv = &priv;
	if (!target->pars) {
		dfu_err("%s: target parameters expected\n", __func__);
		return -1;
	}
	dfu_log("STK500 target initialized\n");
	return 0;
}

static int _check_sync(const struct dfu_cmddescr *descr,
		       const struct dfu_cmdbuf *buf)
{
	char *ptr = buf->buf.in;

	dfu_dbg("%s: ptr[0] = 0x%02x (expected 0x%02x)\n", __func__, ptr[0],
		STK_INSYNC);
	return ptr[0] == STK_INSYNC ? 0 : -1;
}

static int _check_get_param_reply(const struct dfu_cmddescr *descr,
				  const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;
	unsigned *out = descr->priv;

	if (ptr[0] != STK_INSYNC) {
		dfu_err("%s: no sync\n", __func__);
		goto error;
	}
	if (ptr[2] != STK_OK) {
		dfu_err("%s: ERROR\n", __func__);
		goto error;
	}
	*out = ptr[0];
	return 0;
error:
	dfu_log("buf = 0x%02x 0x%02x 0x%02x\n", ptr[0], ptr[1], ptr[2]);
	return -1;
}


struct stk500_get_param_cmd {
	uint8_t code;
	uint8_t param;
	uint8_t eop;
};

/* Get parameter */
static int _get_param(struct dfu_target *target, uint8_t param,
		      unsigned int *_out)
{
	int stat;
	struct stk500_data *priv = target->priv;
	struct stk500_get_param_cmd *cmdb = (struct stk500_get_param_cmd *)
		cmd_buffer;
	static uint8_t param_reply[3];
	static unsigned int out;
	static const struct dfu_cmdbuf cmdbufs0[] = {
		/* Send sync */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = sizeof(*cmdb),
		},
		[1] = {
			.dir = IN,
			.buf = {
				.in = param_reply,
			},
			.len = sizeof(param_reply),
			.timeout = 300,
			.completed = _check_get_param_reply,
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
		.priv = &out,
	};

	cmdb->code = STK_GET_PARAMETER;
	cmdb->param = param;
	cmdb->eop = STK_CRC_EOP;
	priv->curr_descr = &descr0;
	stat = dfu_cmd_do_sync(target, &descr0);
	if (stat < 0)
		return stat;
	*_out = out;
	return stat;
}

/* Probe by sending GET_SYNC command and waiting for reply */

struct stk500_get_sync_cmd {
	uint8_t code;
	uint8_t eop;
};

static int _check_get_sync(const struct dfu_cmddescr *descr,
			   const struct dfu_cmdbuf *buf)
{
	char *ptr = buf->buf.in;

	return ptr[0] == STK_INSYNC && ptr[1] == STK_OK ? 0 :
		DFU_CMD_STATUS_ERROR;
}

int _get_sync(struct dfu_target *target)
{
	struct stk500_data *priv = target->priv;
	struct stk500_get_sync_cmd *cmdb = (struct stk500_get_sync_cmd *)
		cmd_buffer;
	static uint8_t sync_reply[2];
	int i, ret;
	static const struct dfu_cmdbuf cmdbufs0[] = {
		/* Send sync */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = sizeof(*cmdb),
		},
		[1] = {
			.dir = IN,
			.buf = {
				.in = &sync_reply,
			},
			.len = sizeof(sync_reply),
			.timeout = 300,
			.completed = _check_get_sync,
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
	static const struct dfu_cmddescr descr1 = {
		.cmdbufs = &cmdbufs0[1],
		.ncmdbufs = 1,
		.checksum_ptr = NULL,
		.checksum_size = 0,
		.state = &data.cmd_state,
		.timeout = &data.cmd_timeout,
		.checksum_reset = NULL,
		.checksum_update = NULL,
		.completed = NULL,
	};

	dfu_dbg("syncing target\n");
	for (i = 0; i < MAX_SYNC_ATTEMPTS; i++) {
		cmdb->code= STK_GET_SYNC;
		cmdb->eop = STK_CRC_EOP;
		priv->curr_descr = &descr0;
		ret = dfu_cmd_do_sync(target, &descr0);
		if (!ret) {
			dfu_dbg("sync ok\n");
			return ret;
		}
		if (ret == DFU_CMD_STATUS_TIMEOUT) {
			/* No answer, resend command and try again */
			dfu_dbg("no reply\n");
			continue;
		}
		dfu_dbg("out of sync\n");
		/*
		 * There was an answer, but it was wrong.
		 * Flush input and wait 300ms more
		 */
		do {
			if (dfu_cmd_do_sync(target, &descr1) ==
			    DFU_CMD_STATUS_TIMEOUT)
				break;
		} while(1);
		dfu_dbg("retrying sync\n");
	}
	if (i >= MAX_SYNC_ATTEMPTS)
		dfu_err("cannot sync target\n");
	return i < MAX_SYNC_ATTEMPTS ? 0 : -1;
}

struct stk500_set_device_cmd {
	uint8_t code;
	uint8_t devicecode;
	uint8_t revision;
	uint8_t progtype;
	uint8_t parmode;
	uint8_t polling;
	uint8_t selftimed;
	uint8_t lockbytes;
	uint8_t fusebytes;
	uint8_t flashpollval1;
	uint8_t flashpollval2;
	uint8_t eeprompollval1;
	uint8_t eeprompollval2;
	uint16_t pagesize;
	uint8_t eepromsize[2];
	uint32_t flashsize;
	uint8_t eop;
} __attribute__((packed));

static int _check_set_device(const struct dfu_cmddescr *descr,
			     const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;

	return ptr[0] == STK_OK ? 0 : -1;
}

static int _set_device(struct dfu_target *target, int *n_extp)
{
	struct stk500_set_device_cmd *cmdb = (struct stk500_set_device_cmd *)
		cmd_buffer;
	const struct stk500_device_data *dd = target->pars;
	int stat;
	unsigned min, maj;
	static uint8_t sync_reply, result;
	struct stk500_data *priv = target->priv;
	static const struct dfu_cmdbuf cmdbufs0[] = {
		/* Send sync */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = sizeof(*cmdb),
		},
		[1] = {
			.dir = IN,
			.buf = {
				.in = &sync_reply,
			},
			.len = sizeof(sync_reply),
			.timeout = 300,
			.completed = _check_sync,
		},
		[2] = {
			.dir = IN,
			.buf = {
				.in = &result,
			},
			.len = sizeof(result),
			.timeout = 300,
			.completed = _check_set_device,
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

	stat = _get_param(target, STK_SW_MAJOR, &maj);
	if (stat < 0)
		return stat;
	stat = _get_param(target, STK_SW_MINOR, &min);
	if (stat < 0)
		return stat;
	*n_extp = ((maj > 1) || ((maj == 1) && (min > 10))) ? 4 : 3;
	cmdb->code = STK_SET_DEVICE;
	cmdb->devicecode = dd->devcode;
	cmdb->revision = 0;
	/* Parallel and serial ? */
	cmdb->progtype = 0;
	/* Polling supported */
	cmdb->polling = 1;
	/* Programming is self-timed */
	cmdb->selftimed = 1;
	cmdb->lockbytes = dd->lock ? dd->lock->length : 0;
	cmdb->fusebytes = dd->fuse ? dd->fuse->length : 0;
	cmdb->fusebytes += dd->lfuse ? dd->lfuse->length : 0;
	cmdb->fusebytes += dd->hfuse ? dd->hfuse->length : 0;
	cmdb->fusebytes += dd->efuse ? dd->efuse->length : 0;
	if (dd->flash) {
		cmdb->flashpollval1 = dd->flash->readback[0];
		cmdb->flashpollval2 = dd->flash->readback[1];
		if (dd->flash->paged)
			cmdb->pagesize = cpu_to_be16(dd->flash->page_size);
		cmdb->flashsize = cpu_to_be32(dd->flash->length);
	} else {
		cmdb->flashpollval1 = 0xff;
		cmdb->flashpollval2 = 0xff;
		cmdb->pagesize = 0;
		cmdb->flashsize = 0;
	}
	cmdb->eeprompollval1 = dd->eeprom ? dd->eeprom->readback[0] : 0xff;
	cmdb->eeprompollval2 = dd->eeprom ? dd->eeprom->readback[1] : 0xff;
	cmdb->eepromsize[0] = dd->eeprom ? dd->eeprom->length >> 8 : 0;
	cmdb->eepromsize[1] = dd->eeprom ? dd->eeprom->length : 0;
	cmdb->eop = STK_CRC_EOP;
	priv->curr_descr = &descr0;
	return dfu_cmd_do_sync(target, &descr0);
}


static int stk500_probe(struct dfu_target *target)
{
	return 0;
}

struct stk500_load_address_cmd {
	uint8_t code;
	uint16_t addr;
	uint8_t eop;
} __attribute__((packed));

static int _check_load_addr(const struct dfu_cmddescr *descr,
			    const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;

	return ptr[0] == STK_OK ? 0 : -1;
}

static int stk500_reset_and_sync(struct dfu_target *target);

static int _load_address(struct dfu_target *target, uint16_t addr)
{
	struct stk500_data *priv = target->priv;
	struct stk500_load_address_cmd *cmdb =
		(struct stk500_load_address_cmd *)cmd_buffer;
	static uint8_t sync_reply, result;
	static const struct dfu_cmdbuf cmdbufs0[] = {
		/* Send sync */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = sizeof(*cmdb),
		},
		[1] = {
			.dir = IN,
			.buf = {
				.in = &sync_reply,
			},
			.len = sizeof(sync_reply),
			.timeout = 300,
			.completed = _check_sync,
		},
		[2] = {
			.dir = IN,
			.buf = {
				.in = &result,
			},
			.len = sizeof(result),
			.timeout = 300,
			.completed = _check_load_addr,
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

	cmdb->code = STK_LOAD_ADDRESS;
	/* Convert to flash word address */
	cmdb->addr = addr >> 1;
	cmdb->eop = STK_CRC_EOP;
	priv->curr_descr = &descr0;
	return dfu_cmd_do_sync(target, &descr0);
}

struct stk500_prog_page_cmd {
	uint8_t code;
	uint16_t length;
	uint8_t memtype;
	uint8_t data[0];
} __attribute__((packed));

static int _check_program_page(const struct dfu_cmddescr *descr,
			       const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;

	return ptr[0] == STK_OK ? 0 : -1;
}

static void _chunk_done(struct dfu_target *target,
			const struct dfu_cmddescr *descr)
{
	struct stk500_data *priv = target->priv;

	if (descr->state->status == DFU_CMD_STATUS_OK)
		dfu_dbg("chunk 0x%08x programmed OK\n",
			(unsigned int)priv->curr_chunk_addr);
	dfu_binary_file_chunk_done(target->dfu->bf, priv->curr_chunk_addr,
				   descr->state->status == DFU_CMD_STATUS_OK ?
				   0 : -1);
}


/* Chunk of binary data is available for writing */
static int stk500_chunk_available(struct dfu_target *target,
				  phys_addr_t address,
				  const void *buf, unsigned long sz)
{
	const struct stk500_device_data *dd = target->pars;
	struct stk500_data *priv = target->priv;
	struct stk500_prog_page_cmd *cmdb = (struct stk500_prog_page_cmd *)
		cmd_buffer;
	int i;
	static const uint8_t end = STK_CRC_EOP;
	static uint8_t sync_reply, result;
	static struct dfu_cmdbuf cmdbufs0[] = {
		/* Send sync */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = sizeof(*cmdb),
		},
		[1] = {
			.dir = OUT,
		},
		[2] = {
			.dir = OUT,
			.buf = {
				.out = &end,
			},
			.len = sizeof(end),
		},
		[3] = {
			.dir = IN,
			.buf = {
				.in = &sync_reply,
			},
			.len = sizeof(sync_reply),
			.timeout = 300,
			.completed = _check_sync,
		},
		[4] = {
			.dir = IN,
			.buf = {
				.in = &result,
			},
			.len = sizeof(result),
			.timeout = 300,
			.completed = _check_program_page,
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

	if (!dd->flash) {
		dfu_err("%s: target has no flash\n", __func__);
		return -1;
	}
	dfu_dbg("%s: _load address 0x%08x\n", __func__,
		(unsigned int)address);
	for (i = 0; i < 3; i++) {
		if (!_load_address(target, address))
			break;
		dfu_err("%s: error loading address\n", __func__);
		stk500_reset_and_sync(target);
	}
	dfu_dbg("%s: address loaded ok\n", __func__);
	cmdb->code = STK_PROG_PAGE;
	cmdb->length = cpu_to_be16(sz);
	cmdb->memtype = 'F';
	cmdbufs0[1].buf.out = buf;
	cmdbufs0[1].len = sz;
	/* ASYNCHRONOUS */
	priv->curr_chunk_addr = address;
	priv->curr_descr = &descr0;
	return dfu_cmd_start(target, &descr0);
}

struct stk500_universal_cmd {
	uint8_t code;
	uint8_t cmd[4];
	uint8_t eop;
};

static int _universal(struct dfu_target *target, const uint8_t *cmd,
		      uint8_t *res)
{
	struct stk500_universal_cmd *cmdb = (struct stk500_universal_cmd *)
		cmd_buffer;
	int ret;
	static uint8_t sync_reply, result[2];
	struct stk500_data *priv = target->priv;
	static const struct dfu_cmdbuf cmdbufs0[] = {
		/* Send sync */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = sizeof(*cmdb),
		},
		[1] = {
			.dir = IN,
			.buf = {
				.in = &sync_reply,
			},
			.len = sizeof(sync_reply),
			.timeout = 300,
			.completed = _check_sync,
		},
		[2] = {
			.dir = IN,
			.buf = {
				.in = result,
			},
			.len = sizeof(result),
			.timeout = 300,
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

	cmdb->code = STK_UNIVERSAL;
	memcpy(cmdb->cmd, cmd, sizeof(cmdb->cmd));
	cmdb->eop = STK_CRC_EOP;
	priv->curr_descr = &descr0;
	ret = dfu_cmd_do_sync(target, &descr0);
	if (ret < 0)
		return ret;
	if (res)
		*res = result[0];
	return ret;
}

static int stk500_target_erase_all(struct dfu_target *target)
{
	const struct stk500_device_data *dd = target->pars;

	return _universal(target, dd->chip_erase, NULL);
}

struct stk500_set_extparams_cmd {
	uint8_t code;
	uint8_t commandsize;
	uint8_t eeprompagesize;
	uint8_t signalpagel;
	uint8_t signalbs2;
	uint8_t reset_disable;
	uint8_t eop;
};

static int _check_set_ext_params(const struct dfu_cmddescr *descr,
				 const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;

	return ptr[0] == STK_OK ? 0 : -1;
}

static int _set_extparams(struct dfu_target *target, int n_extp)
{
	struct stk500_data *priv = target->priv;
	const struct stk500_device_data *dd = target->pars;
	struct stk500_set_extparams_cmd *cmdb =
		(struct stk500_set_extparams_cmd *)cmd_buffer;
	static uint8_t sync_reply, result;
	static const struct dfu_cmdbuf cmdbufs0[] = {
		/* Send sync */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = sizeof(*cmdb),
		},
		[1] = {
			.dir = IN,
			.buf = {
				.in = &sync_reply,
			},
			.len = sizeof(sync_reply),
			.timeout = 300,
			.completed = _check_sync,
		},
		[2] = {
			.dir = IN,
			.buf = {
				.in = &result,
			},
			.len = sizeof(result),
			.timeout = 300,
			.completed = _check_set_ext_params,
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

	cmdb->code = STK_SET_DEVICE_EXT;
	cmdb->commandsize = n_extp + 1;
	cmdb->eeprompagesize = dd->eeprom ? dd->eeprom->page_size : 0;
	cmdb->signalpagel = dd->pagel;
	cmdb->signalbs2 = dd->bs2;
	/* bah, avrdude seems wrong, let's copy it anyway */
	cmdb->reset_disable = dd->rd;
	cmdb->eop = STK_CRC_EOP;
	priv->curr_descr = &descr0;
	return dfu_cmd_do_sync(target, &descr0);
}

static int _enter_progmode(struct dfu_target *target)
{
	const struct stk500_device_data *dd = target->pars;

	return _universal(target, dd->enter_progmode, NULL);
}

/* Reset and sync target */
static int stk500_reset_and_sync(struct dfu_target *target)
{
	int n_extp, stat = 0;

	if (dfu_interface_has_target_reset(target->interface))
		stat = dfu_interface_target_reset(target->interface);
	if (stat < 0)
		return stat;
	if (_get_sync(target) < 0)
		return -1;
	if (_set_device(target, &n_extp) < 0)
		return -1;
	if (n_extp > 0)
		if (_set_extparams(target, n_extp) < 0)
			return -1;
	if (_enter_progmode(target) < 0)
		return -1;
	dfu_log("stk500 target ready\n");
	return 0;
}

/* Let target run */

struct stk500_leave_progmode_cmd {
	uint8_t code;
	uint8_t eop;
};

static int _check_leave_progmode(const struct dfu_cmddescr *descr,
				 const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;

	return ptr[0] == STK_OK ? 0 : -1;
}

static int stk500_run(struct dfu_target *target)
{
	struct stk500_data *priv = target->priv;
	static struct stk500_leave_progmode_cmd *cmdb =
		(struct stk500_leave_progmode_cmd *)cmd_buffer;
	static uint8_t sync_reply, result;
	static const struct dfu_cmdbuf cmdbufs0[] = {
		/* Send sync */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmd_buffer,
			},
			.len = sizeof(*cmdb),
		},
		[1] = {
			.dir = IN,
			.buf = {
				.in = &sync_reply,
			},
			.len = sizeof(sync_reply),
			.timeout = 300,
			.completed = _check_sync,
		},
		[2] = {
			.dir = IN,
			.buf = {
				.in = &result,
			},
			.len = sizeof(result),
			.timeout = 300,
			.completed = _check_leave_progmode,
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
	cmdb->code = STK_LEAVE_PROGMODE;
	cmdb->eop = STK_CRC_EOP;
	priv->curr_descr = &descr0;
	return dfu_cmd_do_sync(target, &descr0);
}

/* Interface event */
static int stk500_on_interface_event(struct dfu_target *target)
{
	struct stk500_data *priv = target->priv;

	return dfu_cmd_on_interface_event(target, priv->curr_descr);
}

static int stk500_on_idle(struct dfu_target *target)
{
	struct stk500_data *priv = target->priv;

	if (!priv->curr_descr)
		return 0;
	return dfu_cmd_on_idle(target, priv->curr_descr);
}

static int stk500_get_write_chunk_size(struct dfu_target *target)
{
	return 128;
}

struct dfu_target_ops stk500_dfu_target_ops = {
	.init = stk500_init,
	.probe  = stk500_probe,
	.chunk_available = stk500_chunk_available,
	.reset_and_sync = stk500_reset_and_sync,
	.erase_all = stk500_target_erase_all,
	.run = stk500_run,
	.on_interface_event = stk500_on_interface_event,
	.on_idle = stk500_on_idle,
	.get_write_chunk_size = stk500_get_write_chunk_size,
};
