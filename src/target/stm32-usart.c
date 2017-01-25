/*
 * STM32 firmware update protocol implementation
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2016
 */

#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-cmd.h"

#define ACK 0x79

struct stm32_usart_data {
#define STM32_EXTENDED_MEMORY_ERASE	(1 << 0)
#define STM32_DOUBLE_NAK		(1 << 1)
	int target_flags;
	phys_addr_t curr_chunk_addr;
	struct dfu_cmdstate cmd_state;
	struct dfu_timeout cmd_timeout;
	const struct dfu_cmddescr *curr_descr;
};

struct stm32_get_cmd_reply {
	uint8_t len;
	uint8_t bootloader_version;
	uint8_t supported_commands[11];
	uint8_t ack;
} __attribute__((packed));

struct stm32_gid_cmd_reply {
	uint8_t len;
	uint8_t pid[2];
	uint8_t ack;
};


static void checksum_update(const struct dfu_cmddescr *descr, const void *_buf,
			    unsigned int l)
{
	uint8_t *cptr = descr->checksum_ptr;
	const uint8_t *buf = _buf;
	unsigned int i;

	if (!cptr) {
		dfu_dbg("%s invoked with NULL checksum ptr\n", __func__);
		return;
	}
	for (i = 0; i < l; i++)
		*cptr ^= buf[i];
}

static int _check_ack(const struct dfu_cmddescr *descr,
		      const struct dfu_cmdbuf *buf)
{
	char *ptr = buf->buf.in;

	dfu_dbg("%s: ptr[0] = 0x%02x (expected 0x%02x)\n", __func__, ptr[0],
		ACK);
	return ptr[0] == ACK ? 0 : -1;
}

static int get_cmd(struct dfu_target *target, struct stm32_get_cmd_reply *r)
{
	struct stm32_usart_data *priv = target->priv;
	static const uint8_t cmdb[] = { 0, 0xff, };
	static uint8_t ack;
	static struct dfu_cmdbuf cmds[] = {
		{
			.dir = OUT,
			.buf = {
				.out = cmdb,
			},
			.len = sizeof(cmdb),
		},
		{
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 200,
			.completed = _check_ack,
		},
		{
			.dir = IN,
			.len = sizeof(*r),
			.timeout = 300,
		},
	};
	static struct dfu_cmddescr descr0 = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
	};

	descr0.state = &priv->cmd_state;
	descr0.timeout = &priv->cmd_timeout;
	cmds[2].buf.in = r;
	priv->curr_descr = &descr0;
	return dfu_cmd_do_sync(target, &descr0);
}

static int gid_cmd(struct dfu_target *target, struct stm32_gid_cmd_reply *r)
{
	struct stm32_usart_data *priv = target->priv;
	static const uint8_t cmdb[] = { 0x02, 0xfd, };
	static uint8_t ack;
	static struct dfu_cmdbuf cmds[] = {
		{
			.dir = OUT,
			.buf = {
				.out = cmdb,
			},
			.len = sizeof(cmdb),
		},
		{
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 200,
			.completed = _check_ack,
		},
		{
			.dir = IN,
			.len = sizeof(*r),
			.timeout = 300,
		},
	};
	static struct dfu_cmddescr descr0 = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
	};

	cmds[2].buf.in = r;
	descr0.state = &priv->cmd_state;
	descr0.timeout = &priv->cmd_timeout;
	priv->curr_descr = &descr0;
	return dfu_cmd_do_sync(target, &descr0);
}

int stm32_usart_init(struct dfu_target *target,
		     struct dfu_interface *interface)
{
	static struct stm32_usart_data priv;
	target->interface = interface;
	memset(&priv, 0, sizeof(priv));
	target->priv = &priv;
	dfu_log("STM32-USART target initialized\n");
	return 0;
}

/* Remove this ? */
int stm32_usart_probe(struct dfu_target *target)
{
	int stat, i;
	struct stm32_get_cmd_reply r1;
	struct stm32_gid_cmd_reply r2;
	struct stm32_usart_data *priv = target->priv;

	dfu_log("Probing stm32 target\n");
	stat = get_cmd(target, &r1);
	if (stat < 0)
		return -1;
	dfu_log("Bootloader version = 0x%02x\n", r1.bootloader_version);
	dfu_log("Supported commands:\n");
	for (i = 0; i < sizeof(r1.supported_commands); i++)
		dfu_log("\t0x%02x\n", r1.supported_commands[i]);
	if (r1.supported_commands[6] == 0x44) {
		dfu_log("Extended memory erase cmd supported\n");
		priv->target_flags |= STM32_EXTENDED_MEMORY_ERASE;
	}
	if (r1.bootloader_version == 0x31) {
		dfu_log("Double NAK quirk enabled\n");
		priv->target_flags |= STM32_DOUBLE_NAK;
	}
	stat = gid_cmd(target, &r2);
	if (stat < 0)
		return -1;
	dfu_log("Product id = 0x%02x 0x%02x\n", r2.pid[0], r2.pid[1]);
	dfu_log("stm32 target probed\n");
	return 0;
}

static void _chunk_done(struct dfu_target *target,
			const struct dfu_cmddescr *descr)
{
	struct stm32_usart_data *priv = target->priv;

	if (descr->state->status == DFU_CMD_STATUS_OK)
		dfu_dbg("chunk 0x%08x programmed OK\n",
			(unsigned int)priv->curr_chunk_addr);
	dfu_binary_file_chunk_done(target->dfu->bf, priv->curr_chunk_addr,
				   descr->state->status == DFU_CMD_STATUS_OK ?
				   0 : -1);
}

/* Chunk of binary data is available for writing */
int stm32_usart_chunk_available(struct dfu_target *target,
				phys_addr_t address,
				const void *buf, unsigned long sz)
{
	static const uint8_t cmdb[] = { 0x31, 0xce, };
	static uint32_t addr;
	static uint8_t nbytes, ack, checksum;
	static struct dfu_cmdbuf cmds[] = {
		/* Command, ~Command */
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmdb,
			},
			.len = sizeof(cmdb),
		},
		/* Wait for acknowledge */
		[1] = {
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 200,
			.completed = _check_ack,
		},
		/* Send address */
		[2] = {
			.dir = OUT,
			.buf = {
				.out = &addr,
			},
			.flags = START_CHECKSUM|SEND_CHECKSUM,
			.len = sizeof(addr),
		},
		/* Wait for acknowledge */
		[3] = {
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 200,
			.completed = _check_ack,
		},
		/* Send data */
		[4] = {
			.dir = OUT,
			.flags = START_CHECKSUM,
			.buf = {
				.out = &nbytes,
			},
			.len = sizeof(nbytes),
		},
		/* Send checksum */
		[5] = {
			.dir = OUT,
			.flags = SEND_CHECKSUM,
		},
		/* Wait for acknowledge */
		[6] = {
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 200,
			.completed = _check_ack,
		},
	};
	static struct dfu_cmddescr descr0 = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
		.completed = _chunk_done,
		.checksum_update = checksum_update,
		.checksum_ptr = &checksum,
		.checksum_size = sizeof(checksum),
	};
	struct stm32_usart_data *priv = target->priv;

	if (sz > 256) {
		dfu_err("%s: invalid length %lu\n", __func__, sz);
		return -1;
	}
	descr0.state = &priv->cmd_state;
	descr0.timeout = &priv->cmd_timeout;
	/* Asynchronous command */
	priv->curr_chunk_addr = address;
	priv->curr_descr = &descr0;
	addr = cpu_to_be32(address);
	nbytes = sz - 1;
	cmds[5].len = sz;
	cmds[5].buf.out = buf;
	return dfu_cmd_start(target, &descr0);
}

static void _memory_erase_done(struct dfu_target *target,
			       const struct dfu_cmddescr *descr)
{
	if (descr->state->status != DFU_CMD_STATUS_OK) {
		dfu_err("ERROR ERASING MEMORY\n");
		dfu_notify_error(target->dfu);
		return;
	}
	dfu_dbg("Memory erased OK\n");
}

static int stm32_usart_target_erase_all(struct dfu_target *target)
{
	static const uint8_t cmdb[] = { 0x44, 0xbb, };
	static const uint8_t se[] = { 0xff, 0xff, };
	static uint8_t ack, checksum;
	static struct dfu_cmdbuf cmds[] = {
		{
			.dir = OUT,
			.buf = {
				.out = cmdb,
			},
			.len = sizeof(cmdb),
		},
		{
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 200,
			.completed = _check_ack,
		},
		{
			.dir = OUT,
			.buf = {
				.out = se,
			},
			.flags = START_CHECKSUM|SEND_CHECKSUM,
			.len = sizeof(se),
		},
		{
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 60000,
			.completed = _check_ack,
		},
	};
	static struct dfu_cmddescr descr0 = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
		.checksum_ptr = &checksum,
		.checksum_size = sizeof(checksum),
		.checksum_update = checksum_update,
		.completed = _memory_erase_done,
	};
	struct stm32_usart_data *priv = target->priv;

	dfu_log("Starting global memory erase\n");
	descr0.state = &priv->cmd_state;
	descr0.timeout = &priv->cmd_timeout;
	priv->curr_descr = &descr0;
	return dfu_cmd_start(target, &descr0);
}

/* Reset and sync target */
static int stm32_usart_reset_and_sync(struct dfu_target *target)
{
	struct stm32_usart_data *priv = target->priv;
	struct dfu_interface *interface = target->interface;
	int stat = 0, i;
	static uint8_t cmdb[] = { 0x7f, };
	static uint8_t ack;
	static struct dfu_cmdbuf cmds[] = {
		{
			.dir = OUT,
			.buf = {
				.out = cmdb,
			},
			.len = sizeof(cmdb),
		},
		{
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 100,
			.completed = _check_ack,
		},
	};
	static struct dfu_cmddescr descr0 = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
	};

	descr0.state = &priv->cmd_state;
	descr0.timeout = &priv->cmd_timeout;
	for (i = 0; i < 5; i++) {
		/* Reset and sync: hw reset and enter bootloader */
		if (interface->ops->target_reset)
			stat = interface->ops->target_reset(interface);
		if (stat < 0)
			return stat;
		priv->curr_descr = &descr0;
		if (!dfu_cmd_do_sync(target, &descr0)) {
			dfu_dbg("Target sync OK\n");
			return 0;
		}
	}
	dfu_err("Could not sync target\n");
	return -1;
}

/* Let the target run */
int stm32_usart_run(struct dfu_target *target)
{
	struct dfu_interface *interface = target->interface;

	if (interface->ops->target_run)
		return interface->ops->target_run(interface);
	if (interface->ops->target_reset)
		return interface->ops->target_reset(interface);
	return -1;
}

/* Interface event: no async commands, do nothing */
int stm32_usart_on_interface_event(struct dfu_target *target)
{
	struct stm32_usart_data *priv = target->priv;

	return dfu_cmd_on_interface_event(target, priv->curr_descr);
}

int stm32_usart_on_idle(struct dfu_target *target)
{
	struct stm32_usart_data *priv = target->priv;

	if (!priv->curr_descr)
		return 0;
	return dfu_cmd_on_idle(target, priv->curr_descr);
}

static int stm32_usart_get_write_chunk_size(struct dfu_target *target)
{
	return 256;
}

struct dfu_target_ops stm32_dfu_target_ops = {
	.init = stm32_usart_init,
	.probe  = stm32_usart_probe,
	.chunk_available = stm32_usart_chunk_available,
	.reset_and_sync = stm32_usart_reset_and_sync,
	.erase_all = stm32_usart_target_erase_all,
	.run = stm32_usart_run,
	.on_interface_event = stm32_usart_on_interface_event,
	.on_idle = stm32_usart_on_idle,
	.get_write_chunk_size = stm32_usart_get_write_chunk_size,
};
