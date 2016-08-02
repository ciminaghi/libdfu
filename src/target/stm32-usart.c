/*
 * STM32 firmware update protocol implementation
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2016
 */

#include "dfu.h"
#include "dfu-internal.h"

#define STM32_EXTENDED_MEMORY_ERASE	(1 << 0)
#define STM32_DOUBLE_NAK		(1 << 1)

static int target_flags;

enum stm32_usart_dir {
	IN,
	OUT,
};

/*
 * Send flags
 */
#define START_CHECKSUM 1
#define SEND_CHECKSUM  2
#define NO_ACK         4

struct stm32_usart_cmdbuf {
	enum stm32_usart_dir dir;
	int flags;
	union {
		void *in;
		const void *out;
	} buf;
	unsigned int len;
};

struct stm32_usart_cmdescr {
	struct stm32_usart_cmdbuf *cmdbufs;
	int ncmdbufs;
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

static int wait_for_ack(struct dfu_interface *interface)
{
	char c;
	int stat = interface->ops->read(interface, &c, 1);

	if (stat < 0)
		return -1;
	return c == 0x79 ? 0 : -1;
}

static void checksum_update(const uint8_t *buf, unsigned int l, uint8_t *out)
{
	int i;

	for (i = 0; i < l; i++)
		*out ^= buf[i];
}

int send_cmd(struct dfu_target *target,
	     const struct stm32_usart_cmdescr *descr)
{
	struct dfu_interface *interface = target->interface;
	struct stm32_usart_cmdbuf *ptr;
	int i, stat;
	static uint8_t checksum;

	if (!interface || !interface->ops || !interface->ops->write) {
		dfu_err("%s: cannot write to interface\n", __func__);
		return -1;
	}
	for (i = 0, ptr = descr->cmdbufs; i < descr->ncmdbufs; i++, ptr++) {
		if (ptr->flags & START_CHECKSUM)
			checksum = 0;
		switch (ptr->dir) {
		case OUT:
			checksum_update(ptr->buf.out, ptr->len, &checksum);
			stat = interface->ops->write(interface, ptr->buf.out,
						     ptr->len);
			if (stat < ptr->len) {
				dfu_err("%s: error writing to interface\n",
					__func__);
				return -1;
			}
			if (ptr->flags & SEND_CHECKSUM)
				stat = interface->ops->write(interface,
							     (char *)&checksum,
							     sizeof(checksum));
			if (stat < 1) {
				dfu_err("%s: error sending checksum\n",
					__func__);
				return -1;
			}
			if (!(ptr->flags & NO_ACK))
				if (wait_for_ack(interface) < 0) {
					dfu_err("%s: no ACK or NAK\n",
						__func__);
					return -1;
				}
			break;
		case IN:
			stat = interface->ops->read(interface, ptr->buf.in,
						     ptr->len);
			if (stat < ptr->len) {
				dfu_err("%s: error reading from interface\n",
					__func__);
				return -1;
			}
			break;
		default:
			dfu_err("%s: invalid buffer direction\n", __func__);
			return -1;
		}
	}
	return 0;
}

static int get_cmd(struct dfu_target *target, struct stm32_get_cmd_reply *r)
{
	static const uint8_t cmdb[] = { 0, 0xff, };
	static struct stm32_usart_cmdbuf cmds[] = {
		{
			.dir = OUT,
			.buf.out = cmdb,
			.len = sizeof(cmdb),
		},
		{
			.dir = IN,
			.len = sizeof(*r),
		},
	};
	static struct stm32_usart_cmdescr descr = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
	};

	cmds[1].buf.in = r;
	return send_cmd(target, &descr);
}

static int gid_cmd(struct dfu_target *target, struct stm32_gid_cmd_reply *r)
{
	static const uint8_t cmdb[] = { 0x02, 0xfd, };
	static struct stm32_usart_cmdbuf cmds[] = {
		{
			.dir = OUT,
			.buf.out = cmdb,
			.len = sizeof(cmdb),
		},
		{
			.dir = IN,
			.len = sizeof(*r),
		},
	};
	static struct stm32_usart_cmdescr descr = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
	};

	cmds[1].buf.in = r;
	return send_cmd(target, &descr);
}

int stm32_usart_init(struct dfu_target *target,
		     struct dfu_interface *interface)
{
	target->interface = interface;
	dfu_log("%s: target = %p, interface = %p\n", __func__,
		target, interface);
	return 0;
}

/* Remove this ? */
int stm32_usart_probe(struct dfu_target *target)
{
	int stat, i;
	struct stm32_get_cmd_reply r1;
	struct stm32_gid_cmd_reply r2;

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
		target_flags |= STM32_EXTENDED_MEMORY_ERASE;
	}
	if (r1.bootloader_version == 0x31) {
		dfu_log("Double NAK quirk enabled\n");
		target_flags |= STM32_DOUBLE_NAK;
	}
	stat = gid_cmd(target, &r2);
	if (stat < 0)
		return -1;
	dfu_log("Product id = 0x%02x 0x%02x\n", r2.pid[0], r2.pid[1]);
	dfu_log("stm32 target probed\n");
	return 0;
}

/* Chunk of binary data is available for writing */
int stm32_usart_chunk_available(struct dfu_target *target,
				phys_addr_t address,
				const void *buf, unsigned long sz)
{
	int stat;
	unsigned long written;
	static const uint8_t cmdb[] = { 0x31, 0xce, };
	static uint32_t addr;
	static int _nbytes;
	static uint8_t nbytes;
	static struct stm32_usart_cmdbuf cmds[] = {
		{
			.dir = OUT,
			.buf.out = cmdb,
			.len = sizeof(cmdb),
		},
		{
			.dir = OUT,
			.buf.out = &addr,
			.flags = START_CHECKSUM|SEND_CHECKSUM,
			.len = sizeof(addr),
		},
		{
			.dir = OUT,
			.flags = START_CHECKSUM|NO_ACK,
			.buf.out = &nbytes,
			.len = sizeof(nbytes),
		},
		{
			.dir = OUT,
			.flags = SEND_CHECKSUM,
		},
	};
	static struct stm32_usart_cmdescr descr = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
	};

	for (written = 0; written < sz; ) {
		_nbytes = min(sz - written - 1, 255);
		nbytes = _nbytes;
		addr = cpu_to_be32(address + written + 0x8000000);
		dfu_dbg("_nbytes = %d, written = %lu, sz = %lu\n", _nbytes,
			written, sz);
		cmds[3].len = nbytes + 1;
		cmds[3].buf.out = ((uint8_t *)buf) + written;
		stat = send_cmd(target, &descr);
		if (stat < 0)
			return stat;
		written += nbytes + 1;
		dfu_log_noprefix(".");
	}
	return 0;
}

/* Reset and sync target */
static int stm32_usart_reset_and_sync(struct dfu_target *target)
{
	char c = 0x7f, rpy;
	int stat = 0, i;

	/* Reset and sync: hw reset and enter bootloader */
	if (target->interface->ops->target_reset)
		stat = target->interface->ops->target_reset(target->interface);
	if (stat < 0)
		return stat;
	if (!target->interface->ops->write) {
		dfu_err("interface has no write operation\n");
		return -1;
	}
	for (i = 0; i < 1; i++) {
		stat = target->interface->ops->write(target->interface, &c,
						     sizeof(c));
		if (stat < 0)
			return stat;
		stat = target->interface->ops->read(target->interface, &rpy,
						    sizeof(rpy));
		if (stat < 1)
			continue;
		if (rpy == 0x79)
			break;
	}
	return 0;
}

/* Let target run */
int stm32_usart_run(struct dfu_target *target)
{
	return -1;
}

/* Interface event */
int stm32_usart_on_interface_event(struct dfu_target *target)
{
	return -1;
}

struct dfu_target_ops stm32_dfu_target_ops = {
	.init = stm32_usart_init,
	.probe  = stm32_usart_probe,
	.chunk_available = stm32_usart_chunk_available,
	.reset_and_sync = stm32_usart_reset_and_sync,
	.run = stm32_usart_run,
	.on_interface_event = stm32_usart_on_interface_event,
};
