/*
 * NORDIC NRF52 (via SPI) update protocol implementation
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2017
 */
/*
 * See
 * http://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.sdk5.v13.1.0%2Flib_dfu_transport_serial.html
 * and
 * https://github.com/NordicSemiconductor/pc-nrfutil/blob/master/nordicsemi/dfu/dfu_transport_serial.py
 */

#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-cmd.h"
#include "dfu-nordic-spi.h"

/* Dummy operation, we just get the reply to our previous request */
#define NRF_DFU_OP_DUMMY	0x00
#define NRF_DFU_OP_CREATE	0x01
#define NRF_DFU_OP_SET_PRN	0x02
#define NRF_DFU_OP_CALC_CHK	0x03
#define NRF_DFU_OP_EXEC		0x04
#define NRF_DFU_OP_SELECT	0x06
#define NRF_DFU_OP_GET_MTU	0x07
#define NRF_DFU_OP_WRITE	0x08
#define NRF_DFU_OP_RESPONSE	0x60

#define NRF_DFU_RES_INV_CODE		0x00
#define NRF_DFU_RES_SUCCESS		0x01
#define NRF_DFU_RES_OP_NOT_SUPPORTED	0x02
#define NRF_DFU_RES_INV_PAR		0x03
#define NRF_DFU_RES_INS_RES		0x04
#define NRF_DFU_RES_INV_OBJ		0x05
#define NRF_DFU_RES_TYPE_NOT_SUPPORTED	0x07
#define NRF_DFU_RES_OP_NOT_PERMITTED	0x08
#define NRF_DFU_RES_OP_FAILED		0x0a

enum nordic_spi_send_state {
	WAITING = 0,
	SENDING,
	FILE_SENT,
	OBJECT_SENT,
	FILE_CRC_REQUESTED,
	OBJECT_CRC_REQUESTED,
	FILE_CRC_OK,
	OBJECT_CRC_OK,
	FILE_EXEC_REQUESTED,
	OBJECT_EXEC_REQUESTED,
	THROWING_AWAY,
};

struct nordic_spi_select_object_data {
	unsigned int offset;
	unsigned int max_size;
	uint32_t crc;
	enum nzbf_type type;
};

struct nordic_spi_data {
	unsigned int advertised_mtu;
	struct dfu_cmdstate cmd_state;
	struct dfu_timeout cmd_timeout;
	const struct dfu_cmddescr *curr_descr;
	unsigned int curr_file_size;
	unsigned int curr_obj_size;
	unsigned int curr_obj_written;
	unsigned int curr_chunk_size;
	unsigned int send_offset;
	/* These are read via crc calc command */
	uint32_t object_final_offset;
	uint32_t object_crc_from_target;
	struct nordic_spi_select_object_data sod;
	enum nordic_spi_send_state send_state;
};

/*
 * Just one command shall be active at any time.
 */
static struct nordic_spi_data data;

/* Command callbacks */

/* Target methods */


static int nordic_spi_init(struct dfu_target *target,
			   struct dfu_interface *interface)
{
	dfu_log("NORDIC SPI target initialized\n");
	target->priv = &data;
	return 0;
}

static int _check_prn_reply(const struct dfu_cmddescr *descr,
			    const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;
	static const char expected_reply[] =
		{ NRF_DFU_OP_RESPONSE,
		  NRF_DFU_OP_SET_PRN,
		  NRF_DFU_RES_SUCCESS,
		};

	dfu_dbg("%s entered\n", __func__);
	dfu_dbg("reply = 0x%02x 0x%02x 0x%02x\n", ptr[0], ptr[1], ptr[2]);
	return !memcmp(ptr, expected_reply, sizeof(expected_reply)) ? 0 : -1;
}

static int _check_mtu_reply(const struct dfu_cmddescr *descr,
			    const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;
	static const char expected_reply[] =
		{ NRF_DFU_OP_RESPONSE,
		  NRF_DFU_OP_GET_MTU,
		};

	dfu_dbg("%s entered\n", __func__);
	if (memcmp(ptr, expected_reply, sizeof(expected_reply))) {
		dfu_err("%s: unexpected get mtu reply\n", __func__);
		return -1;
	}
	dfu_dbg("%s: data = 0x%02x 0x%02x 0x%02x 0x%02x\n", __func__,
		ptr[0], ptr[1],
		ptr[2], ptr[3]);
	/* MTU is sent as a big endian number */
	data.advertised_mtu = (ptr[2] << 8) + ptr[3];
	dfu_dbg("%s: advertised MTU = %u\n", __func__, data.advertised_mtu);
	return 0;
}

/*
 * probe time: set PNR and get MTU
 */
static int nordic_spi_probe(struct dfu_target *target)
{
	int ret;
	struct nordic_spi_data *priv = target->priv;
	static const uint8_t set_prn_cmd[] = {
		NRF_DFU_OP_SET_PRN,
		/* PRN = 256 */
		0x01, 0x00,
	};
	static uint8_t set_prn_reply[3];
	static const uint8_t get_mtu_cmd[] = {
		NRF_DFU_OP_GET_MTU
	};
	static const uint8_t get_mtu_reply_outbuf[4] =
		{ NRF_DFU_OP_DUMMY, 0, 0, 0 };
	static uint8_t get_mtu_reply[4];
	static const struct dfu_cmdbuf cmdbufs0[] = {
		/* Sent PRN to 256 */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = set_prn_cmd,
			},
			.len = sizeof(set_prn_cmd),
		},
		/* WAIT 200ms */
		{
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 150,
		},
		{
			.dir = OUT_IN,
			.buf = {
				.out = set_prn_cmd,
				.in = set_prn_reply,
			},
			.len = sizeof(set_prn_cmd),
		},
		{
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 150,
		},
		{
			.dir = OUT_IN,
			.buf = {
				.out = set_prn_cmd,
				.in = set_prn_reply,
			},
			.len = sizeof(set_prn_cmd),
			.completed = _check_prn_reply,
		},
		/* Read advertised mtu */
		{
			.dir = OUT,
			.buf = {
				.out = get_mtu_cmd,
			},
			.len = sizeof(get_mtu_cmd),
		},
		/* WAIT 200ms */
		{
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 200,
		},
		{
			.dir = OUT_IN,
			.buf = {
				.out = get_mtu_reply_outbuf,
				.in = get_mtu_reply,
			},
			.len = sizeof(get_mtu_reply),
		},
		/* WAIT 200ms */
		{
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 200,
		},
		{
			.dir = OUT_IN,
			.buf = {
				.out = get_mtu_reply_outbuf,
				.in = get_mtu_reply,
			},
			.completed = _check_mtu_reply,
			.len = sizeof(get_mtu_reply),
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
	priv->curr_descr = &descr0;
	ret = dfu_cmd_do_sync(target, &descr0);
	if (!ret) {
		dfu_dbg("probe ok\n");
		priv->send_state = WAITING;
	} else
		dfu_err("cannot probe target\n");
	return ret;
}


static int _check_create_obj_reply(const struct dfu_cmddescr *descr,
				   const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;
	static const char expected_reply[] =
		{ NRF_DFU_OP_RESPONSE,
		  NRF_DFU_OP_CREATE,
		  NRF_DFU_RES_SUCCESS,
		};

	dfu_dbg("%s entered\n", __func__);
	dfu_dbg("reply = 0x%02x 0x%02x 0x%02x\n", ptr[0], ptr[1], ptr[2]);
	return !memcmp(ptr, expected_reply, sizeof(expected_reply)) ? 0 : -1;
}

static int _create_obj(struct dfu_target *target, enum nzbf_type t,
		       unsigned int size)
{
	int ret;
	struct nordic_spi_data *priv = target->priv;
	static uint8_t create_obj_cmd[6] = {
		NRF_DFU_OP_CREATE,
	};
	static uint8_t dummy_create_obj_cmd[6] = {
		NRF_DFU_OP_DUMMY,
	};
	static uint8_t create_obj_reply[6];
	uint32_t v = cpu_to_le32(size);
	static const struct dfu_cmdbuf cmdbufs0[] = {
		[0] = {
			.dir = OUT,
			.buf = {
				.out = create_obj_cmd,
			},
			.len = sizeof(create_obj_cmd),
		},
		/* WAIT 700ms */
		{
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 700,
		},
		{
			.dir = OUT_IN,
			.buf = {
				.out = dummy_create_obj_cmd,
				.in = create_obj_reply,
			},
			.len = sizeof(dummy_create_obj_cmd),
			.completed = _check_create_obj_reply,
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

	if (t > NZ_TYPE_DATA) {
		dfu_err("%s: invalid type %d\n", __func__, t);
		return -1;
	}
	create_obj_cmd[1] = t;
	memcpy(&create_obj_cmd[2], &v, sizeof(v));
	priv->curr_descr = &descr0;
	ret = dfu_cmd_do_sync(target, &descr0);
	if (!ret)
		dfu_dbg("OBJECT CREATED OK\n");
	else
		dfu_err("Error creating object\n");
	return ret;
}

static int _check_select_obj_reply(const struct dfu_cmddescr *descr,
				   const struct dfu_cmdbuf *buf)
{
	unsigned char *ptr = buf->buf.in;
	static const char expected_reply[] =
		{ NRF_DFU_OP_RESPONSE,
		  NRF_DFU_OP_SELECT,
		  NRF_DFU_RES_SUCCESS
		};
	struct nordic_spi_select_object_data *sod = &data.sod;
	uint32_t v;

	dfu_dbg("%s entered\n", __func__);
	dfu_dbg("reply = 0x%02x 0x%02x 0x%02x\n", ptr[0], ptr[1], ptr[2]);
	if (memcmp(ptr, expected_reply, sizeof(expected_reply)))
		return -1;
	memcpy(&v, &ptr[3], sizeof(v));
	sod->max_size = le32_to_cpu(v);
	dfu_dbg("max_size = %u\n", sod->max_size);
	memcpy(&v, &ptr[7], sizeof(v));
	sod->offset = le32_to_cpu(v);
	dfu_dbg("offset = %u\n", sod->offset);
	memcpy(&v, &ptr[11], sizeof(v));
	sod->crc = le32_to_cpu(v);
	dfu_dbg("crc = 0x%08x\n", (unsigned int)sod->crc);
	return 0;
}

static int _select_obj(struct dfu_target *target)
{
	int ret;
	struct nordic_spi_data *priv = target->priv;
	struct nordic_spi_select_object_data *sod = &priv->sod;
	static uint8_t select_obj_cmd[15] = {
		NRF_DFU_OP_SELECT,
		[1 ... 14] = 0xff,
	};
	static uint8_t dummy_select_obj_cmd[15] = {
		0,
	};
	static uint8_t select_obj_reply[15];
	static const struct dfu_cmdbuf cmdbufs0[] = {
		{
			.dir = OUT,
			.buf = {
				.out = select_obj_cmd,
			},
			.len = sizeof(select_obj_cmd),
		},
		/* WAIT 700ms */
		{
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 700,
		},
		{
			.dir = OUT_IN,
			.buf = {
				.out = dummy_select_obj_cmd,
				.in = select_obj_reply,
			},
			.len = sizeof(select_obj_cmd),
			.completed = _check_select_obj_reply,
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

	if (sod->type > NZ_TYPE_DATA) {
		dfu_err("%s: invalid type %d\n", __func__, sod->type);
		return -1;
	}

	select_obj_cmd[1] = sod->type;
	priv->curr_descr = &descr0;
	ret = dfu_cmd_do_sync(target, &descr0);
	if (ret < 0)
		dfu_err("Error selecting object\n");
	return ret;
}

static void _chunk_sent(struct dfu_target *target,
			const struct dfu_cmddescr *descr)
{
	struct nordic_spi_data *priv = target->priv;

	if (descr->state->status == DFU_CMD_STATUS_OK)
		dfu_dbg("chunk 0x%08x programmed OK\n",
			(unsigned int)priv->send_offset);
	priv->curr_obj_written += priv->curr_chunk_size;
	dfu_dbg("%s: written = %u, size = %u\n", __func__,
		priv->curr_obj_written, priv->curr_obj_size);
	if (nzbf_offset(priv->send_offset) + priv->curr_chunk_size >=
	    priv->curr_file_size) {
		dfu_dbg("file written !\n");
		priv->send_state = FILE_SENT;
	} else if (priv->curr_obj_written >= priv->curr_obj_size) {
		dfu_dbg("curr object done, switching to OBJECT_SENT state\n");
		priv->send_state = OBJECT_SENT;
	} else
		dfu_binary_file_chunk_done(target->dfu->bf, priv->send_offset,
					   descr->state->status ==
					   DFU_CMD_STATUS_OK ? 0 : -1);
}

static int _send_buffer(struct dfu_target *target,
			unsigned long offset,
			const void *buf, unsigned long sz)
{
	struct nordic_spi_data *priv = target->priv;
	static uint8_t _buf[257];
	static struct dfu_cmdbuf cmdbufs0[] = {
		[0] = {
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 100,
		},
		[1] = {
			.dir = OUT,
		},
		[2] = {
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 100,
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
		.completed = _chunk_sent,
	};

	if (sz > priv->advertised_mtu || sz > (sizeof(_buf) - 1))
		return -1;
	_buf[0] = NRF_DFU_OP_WRITE;
	memcpy(&_buf[1], buf, sz);
	cmdbufs0[1].buf.out = _buf;
	cmdbufs0[1].len = sz + 1;

	/* ASYNCHRONOUS */
	priv->send_offset = offset;
	priv->curr_descr = &descr0;
	priv->curr_chunk_size = sz;
	return dfu_cmd_start(target, &descr0);
}

static int _check_calc_crc_reply(const struct dfu_cmddescr *descr,
				 const struct dfu_cmdbuf *buf)
{
	int ret;
	unsigned char *ptr = buf->buf.in;
	static const char expected_reply[] =
		{ NRF_DFU_OP_RESPONSE,
		  NRF_DFU_OP_CALC_CHK,
		  NRF_DFU_RES_SUCCESS
		};
	dfu_dbg("%s entered\n", __func__);
	dfu_dbg("reply = 0x%02x 0x%02x 0x%02x\n", ptr[0], ptr[1], ptr[2]);
	ret = !memcmp(ptr, expected_reply, sizeof(expected_reply)) ? 0 : -1;
	if (ret) {
		dfu_err("%s, error\n", __func__);
		return -1;
	}
	memcpy(&data.object_final_offset, &ptr[3], sizeof(uint32_t));
	memcpy(&data.object_crc_from_target, &ptr[7], sizeof(uint32_t));
	dfu_dbg("object_final_offset = %u, object_crc_from_target = 0x%08x\n",
		(unsigned)data.object_final_offset,
		(unsigned)data.object_crc_from_target);
	return ret;
}

static void _crc_ok(struct dfu_target *target,
		    const struct dfu_cmddescr *descr)
{
	struct nordic_spi_data *priv = target->priv;

	switch (priv->send_state) {
	case FILE_CRC_REQUESTED:
		priv->send_state = FILE_CRC_OK;
		break;
	case OBJECT_CRC_REQUESTED:
		priv->send_state = OBJECT_CRC_OK;
		break;
	default:
		dfu_err("%s in unexpected state %d\n", __func__,
			priv->send_state);
		break;
	}
}

static int _calc_crc(struct dfu_target *target, enum nordic_spi_send_state s)
{
	int ret;
	struct nordic_spi_data *priv = target->priv;
	static uint8_t calc_crc_cmd[1] = {
		NRF_DFU_OP_CALC_CHK,
	};
	static const uint8_t dummy_calc_crc_cmd[11] = {
		[0 ... 10] = 0,
	};
	static uint8_t calc_crc_reply[11];
	static const struct dfu_cmdbuf cmdbufs0[] = {
		[0] = {
			.dir = OUT,
			.buf = {
				.out = calc_crc_cmd,
			},
			.len = sizeof(calc_crc_cmd),
		},
		/* WAIT 200ms */
		{
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 200,
		},
		{
			.dir = OUT_IN,
			.buf = {
				.out = dummy_calc_crc_cmd,
				.in = calc_crc_reply,
			},
			.len = sizeof(calc_crc_reply),
			.completed = _check_calc_crc_reply,
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
		.completed = _crc_ok,
	};

	priv->curr_descr = &descr0;
	priv->send_state = s;
	ret = dfu_cmd_start(target, &descr0);
	if (ret < 0)
		dfu_err("Error in crc calculation\n");
	return ret;
}

static void _chunk_done(struct dfu_target *target,
			const struct dfu_cmddescr *descr)
{
	struct nordic_spi_data *priv = target->priv;
	struct dfu_binary_file *bf = target->dfu->bf;

	switch (priv->send_state) {
	case FILE_EXEC_REQUESTED:
		priv->send_state = WAITING;
		break;
	case OBJECT_EXEC_REQUESTED:
		priv->send_state = SENDING;
		break;
	default:
		dfu_err("%s in unexpected state %d\n", __func__,
			priv->send_state);
		break;
	}
	dfu_binary_file_chunk_done(bf, priv->send_offset, 0);
}

static int _check_exec_obj_reply(const struct dfu_cmddescr *descr,
				 const struct dfu_cmdbuf *buf)
{
	int ret;
	unsigned char *ptr = buf->buf.in;
	static const char expected_reply[] =
		{ NRF_DFU_OP_RESPONSE,
		  NRF_DFU_OP_EXEC,
		  NRF_DFU_RES_SUCCESS
		};

	dfu_dbg("%s entered\n", __func__);
	dfu_dbg("reply = 0x%02x 0x%02x 0x%02x\n", ptr[0], ptr[1], ptr[2]);
	ret = !memcmp(ptr, expected_reply, sizeof(expected_reply)) ? 0 : -1;
	if (ret)
		dfu_err("%s: error\n", __func__);
	return ret;
}


static int _exec_obj(struct dfu_target *target, enum nordic_spi_send_state s)
{
	int ret;
	struct nordic_spi_data *priv = target->priv;
	static uint8_t exec_obj_cmd[1] = {
		NRF_DFU_OP_EXEC,
	};
	static const uint8_t dummy_exec_obj_cmd[3] = {
		NRF_DFU_OP_DUMMY, 0, 0,
	};
	static uint8_t exec_obj_reply[3];
	static const struct dfu_cmdbuf cmdbufs0[] = {
		[0] = {
			.dir = OUT,
			.buf = {
				.out = exec_obj_cmd,
			},
			.len = sizeof(exec_obj_cmd),
		},
		/* WAIT 200ms */
		{
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 200,
		},
		{
			.dir = OUT_IN,
			.buf = {
				.out = dummy_exec_obj_cmd,
				.in = exec_obj_reply,
			},
			.len = sizeof(exec_obj_reply),
			.completed = _check_exec_obj_reply,
		},
		/* WAIT 200ms */
		{
			.dir = NONE,
			.buf = {},
			.len = 0,
			.timeout = 200,
		},
		{
			/*
			 * THIS LAST COMMAND FORCES THE UPDATE OF SETTINGS
			 * AND TARGET REBOOT
			 */
			.dir = OUT,
			.buf = {
				.out = exec_obj_cmd,
				.in = exec_obj_reply,
			},
			.len = sizeof(exec_obj_cmd),
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

	priv->send_state = s;
	priv->curr_descr = &descr0;
	ret = dfu_cmd_start(target, &descr0);
	if (ret < 0)
		dfu_err("Error executing object\n");
	return ret;
}

static int _start_sending_file(struct dfu_target *target,
			       struct dfu_binary_file *bf,
			       phys_addr_t address,
			       const void *buf, unsigned long sz)
{
	struct nordic_spi_data *priv = target->priv;
	struct nordic_spi_select_object_data *sod = &priv->sod;
	unsigned int total_file_size;
	uint32_t expected_crc;
	int ret = 0;

	ret = nzbf_get_file_type_and_size(bf, address, &sod->type,
					  &total_file_size);
	if (ret < 0)
		return ret;
	dfu_dbg("%s: addr = 0x%08x, type = %d, sz = %lu, total file size = %u\n",
		__func__, (unsigned)address, sod->type, sz, total_file_size);

	ret = _select_obj(target);
	if (ret < 0) {
		dfu_dbg("%s %d\n", __func__, __LINE__);
		return ret;
	}
	if (sod->type == NZ_TYPE_COMMAND && total_file_size > sod->max_size) {
		dfu_err("%s: max file size exceeded (%u)\n", __func__,
			total_file_size);
		return -1;
	}
	dfu_dbg("%s: sod->offset = %u\n", __func__, sod->offset);
	if (!sod->offset || sod->offset > total_file_size ||
	    nzbf_calc_crc(bf, address, sod->offset, &expected_crc) < 0 ||
	    expected_crc != sod->crc) {
		/*
		 * No object with this type on target or wrong crc: do not
		 * recover, just send everything
		 */
		priv->send_state = SENDING;
		priv->send_offset = 0;
		priv->curr_file_size = total_file_size;
		if (nzbf_offset(address))
			ret = -1;
	} else if (sod->offset < total_file_size) {
		/* Partial packet already on target, send what is missing */
		priv->send_state = SENDING;
		priv->send_offset = sod->offset;
		if (sod->type == NZ_TYPE_DATA)
			priv->send_offset |= NZ_FWFILE_DATA_FLAG;
		/* FIXME: CORRECTLY SUPPORT OTHER IMAGES ? */
		priv->curr_file_size = total_file_size;
	} else {
		/* CRC and length are ok, just run the execute command */
		ret = _exec_obj(target, FILE_EXEC_REQUESTED);
	}
	return ret;
}

static int _send_file(struct dfu_target *target,
		      phys_addr_t address,
		      const void *buf, unsigned long sz)
{
	struct nordic_spi_data *priv = target->priv;
	struct nordic_spi_select_object_data *sod = &priv->sod;
	struct dfu_binary_file *bf = target->dfu->bf;
	unsigned int obj_sz;
	int ret;

	switch (priv->send_state) {
	case WAITING:
		ret = _start_sending_file(target, bf, address, buf, sz);
		if (ret < 0) {
			dfu_dbg("%s error\n", __func__);
			break;
		}
		/* FALL THROUGH */
	case SENDING:
#if 0
		if (nzbf_offset(address) + sz - 1 <
		    nzbf_offset(priv->send_offset)) {
			/* The end of this buffer is before send_offset */
			dfu_dbg("%s: skipping chunk\n", __func__);
			return sz;
		}
		/*
		 * nzbf_offset(address) + sz - 1 >= send_offset here
		 * which means that the end of the buffer is above send_offset
		 */
		if (nzbf_offset(address) < nzbf_offset(priv->send_offset)) {
			/* The beginning of the buffer is before send_offset */
			dfu_dbg("%s: trimming chunk\n", __func__);
			sz -= nzbf_offset(priv->send_offset) - address;
			buf += nzbf_offset(priv->send_offset) - address;
			address = priv->send_offset;
		}
#endif
		if (!(nzbf_offset(address) % sod->max_size)) {
			obj_sz = sod->type == NZ_TYPE_COMMAND ?
				priv->curr_file_size :
				min(priv->curr_file_size -
				    (nzbf_offset(priv->send_offset) + 128),
				    sod->max_size);
			priv->curr_obj_size = obj_sz;
			priv->curr_obj_written = 0;
			dfu_dbg("before _create_obj(): curr_file_size = %u, send_offset = %u, sod max_size = %u\n", priv->curr_file_size, priv->send_offset,
				sod->max_size);
			ret = _create_obj(target, sod->type, obj_sz);
			if (ret < 0) {
				dfu_err("%s %d, ret = %d\n", __func__,
					__LINE__, ret);
				return ret;
			}
		}
		ret = _send_buffer(target, address, buf, sz);
		if (ret < 0) {
			dfu_err("%s %d, ret = %d\n", __func__, __LINE__, ret);
			return ret;
		}
		break;
	case FILE_SENT:
	case OBJECT_SENT:
		/* This is taken care of in idle */
		ret = 0;
		break;
	default:
		ret = -1;
		dfu_dbg("%s %d, state = %d\n", __func__, __LINE__,
			priv->send_state);
		break;
	}
	if (ret < 0) {
		dfu_err("%s %d, ret = %d\n", __func__, __LINE__, ret);
		dfu_notify_error(target->dfu);
	}
	return ret;
}

/* Chunk of binary data is available for writing */
static int nordic_spi_chunk_available(struct dfu_target *target,
				      phys_addr_t address,
				      const void *buf, unsigned long sz)
{
	struct nordic_spi_data *priv = target->priv;

	if (address & NZ_FWFILE_THROW_AWAY) {
		if (priv->send_state != WAITING) {
			dfu_err("THROW AWAY CHUNK NOT IN WAITING STATE\n");
			return -1;
		}
		priv->send_state = THROWING_AWAY;
		return sz;
	}

	return _send_file(target, address & ~NZ_FWFILE_THROW_AWAY, buf, sz);
}

static int nordic_spi_target_erase_all(struct dfu_target *target)
{
	return -1;
}

/*
 * Reset and sync target
 */
static int nordic_spi_reset_and_sync(struct dfu_target *target)
{
	dfu_dbg("%s entered\n", __func__);
	if (!dfu_interface_has_target_reset(target->interface)) {
		dfu_err("PLEASE ADD A TARGET RESET FOR THIS INTERFACE\n");
		return -1;
	}
	return dfu_interface_target_reset(target->interface);
}

/* Let the target run */
static int nordic_spi_run(struct dfu_target *target)
{
	if (!dfu_interface_has_target_run(target->interface)) {
		dfu_err("PLEASE ADD A TARGET RUN FOR THIS INTERFACE\n");
		return -1;
	}
	return dfu_interface_target_run(target->interface);
}

/* Interface event */
static int nordic_spi_on_interface_event(struct dfu_target *target)
{
	struct nordic_spi_data *priv = target->priv;

	return dfu_cmd_on_interface_event(target, priv->curr_descr);
}

static int nordic_spi_on_idle(struct dfu_target *target)
{
	struct nordic_spi_data *priv = target->priv;
	int ret;

	if (!priv->curr_descr)
		return 0;
	switch (priv->send_state) {
	case FILE_SENT:
	case OBJECT_SENT:
		dfu_dbg("GETTING TARGET TO CALCULATE OBJECT CRC32\n");
		ret = _calc_crc(target, priv->send_state == FILE_SENT ?
				FILE_CRC_REQUESTED : OBJECT_CRC_REQUESTED);
		if (ret < 0)
			return ret;
		break;
	case FILE_CRC_OK:
	case OBJECT_CRC_OK:
		dfu_dbg("EXECUTING OBJECT\n");
		ret = _exec_obj(target, priv->send_state == FILE_CRC_OK ?
				FILE_EXEC_REQUESTED : OBJECT_EXEC_REQUESTED);
		if (ret < 0)
			return ret;
		break;
	case THROWING_AWAY:
		priv->send_state = WAITING;
		dfu_binary_file_chunk_done(target->dfu->bf,
					   NZ_FWFILE_THROW_AWAY, 0);
		break;
	default:
		break;
	}
	/* Go on with async cmd */
	return dfu_cmd_on_idle(target, priv->curr_descr);
}

static int nordic_spi_get_write_chunk_size(struct dfu_target *target)
{
	return 128;
}

struct dfu_target_ops nordic_spi_dfu_target_ops = {
	.init = nordic_spi_init,
	.probe  = nordic_spi_probe,
	.chunk_available = nordic_spi_chunk_available,
	.reset_and_sync = nordic_spi_reset_and_sync,
	.erase_all = nordic_spi_target_erase_all,
	.run = nordic_spi_run,
	.on_interface_event = nordic_spi_on_interface_event,
	.on_idle = nordic_spi_on_idle,
	.get_write_chunk_size = nordic_spi_get_write_chunk_size,
};
