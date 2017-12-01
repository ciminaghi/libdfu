/*
 * NORDIC NRF52 (via SPI) update protocol implementation
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2017
 */
/*
 * See
 * http://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.sdk5.v13.1.0%2Flib_dfu_transport_serial.html
 */

#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-cmd.h"
#include "dfu-nordic-spi.h"

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

struct nordic_spi_data {
	unsigned int advertised_mtu;
	struct dfu_cmdstate cmd_state;
	struct dfu_timeout cmd_timeout;
	const struct dfu_cmddescr *curr_descr;
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
		  /* FIXME: WHAT'S THE RIGHT VALUE ? */
		  0x3, //NRF_DFU_RES_SUCCESS,
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
		{NRF_DFU_OP_GET_MTU, 0xff, 0xff, 0xff};
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
			.timeout = 1500,
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
			.timeout = 200,
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
	if (!ret)
		dfu_dbg("probe ok\n");
	else
		dfu_err("cannot probe target\n");
	return ret;
}

/* Chunk of binary data is available for writing */
static int nordic_spi_chunk_available(struct dfu_target *target,
				      phys_addr_t address,
				      const void *buf, unsigned long sz)
{
	return -1;
}

static int nordic_spi_target_erase_all(struct dfu_target *target)
{
	return -1;
}

/*
 * Reset and sync target: we do nothing here
 */
static int nordic_spi_reset_and_sync(struct dfu_target *target)
{
	return 0;
}

/* Let target run */

/* Just reset target */
static int nordic_spi_run(struct dfu_target *target)
{
	return 0;
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

	if (!priv->curr_descr)
		return 0;
	/* We're writing a page, which is an asynchronous process */
	return dfu_cmd_on_idle(target, priv->curr_descr);
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
};
