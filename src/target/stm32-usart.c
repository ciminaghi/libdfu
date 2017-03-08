/*
 * STM32 firmware update protocol implementation
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2016
 */

#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-cmd.h"
#include "stm32-device.h"

#define ACK 0x79

#define MAX_NSECTORS_ERASE 2

struct stm32_usart_data {
#define STM32_EXTENDED_MEMORY_ERASE	(1 << 0)
#define STM32_DOUBLE_NAK		(1 << 1)
	int target_flags;
	phys_addr_t curr_chunk_addr;
	struct dfu_cmdstate cmd_state;
	struct dfu_timeout cmd_timeout;
	const struct dfu_cmddescr *curr_descr;
	int to_be_erased[MAX_NSECTORS_ERASE];
	int n_to_be_erased;
	const struct stm32_memory_area *erase_area;
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

static inline int test_bit(int bitno, unsigned long *l)
{
	unsigned long *ptr = l + (bitno / sizeof(unsigned long));
	int bit = bitno % sizeof(unsigned long);

	return *ptr & (1 << bit);
}

static inline void set_bit(int bitno, unsigned long *l)
{
	unsigned long *ptr = l + (bitno / sizeof(unsigned long));
	int bit = bitno % sizeof(unsigned long);

	*ptr |= (1 << bit);
}

static const struct stm32_memory_area *
find_area(const struct stm32_device_data *pars,
	  phys_addr_t addr, unsigned long size)
{
	const struct stm32_memory_area *ma = pars->areas[pars->boot_mode];
	int i, nareas = pars->nareas[pars->boot_mode];

	if (!ma || nareas <= 0)
		return NULL;
	for (i = 0; i < nareas; i++, ma++)
		if (addr >= ma->start && addr + size <= ma->start + ma->size)
			return ma;
	return NULL;
}

static const struct stm32_memory_area *
map_chunk_address(struct dfu_target *target,
		  phys_addr_t addr, unsigned long size,
		  int *start_sector, int *nsectors)
{
	const struct stm32_device_data *pars = target->pars;
	int start_found, end_found;
	phys_addr_t a, end = addr + size - 1;
	const struct stm32_flash_sector *s;
	const struct stm32_memory_area *area;

	dfu_dbg("%s: must map chunk @0x%08x, size %lu\n", __func__,
		(unsigned int)addr, size);
	*start_sector = *nsectors = -1;

	area = find_area(pars, addr, size);
	if (!area || area->type != FLASH)
		return area;
	/* Flash area, look for start sector */
	for (s = area->sectors, a = area->start, start_found = end_found = 0;
	     !start_found || !end_found; a += s->size, s++) {
		if (!start_found && addr >= a && addr < a + s->size) {
			/* Found start sector */
			*start_sector =
				(s - area->sectors) + area->sectors_offset;
			*nsectors = 1;
			start_found = 1;
		}
		if (start_found && end < a + s->size) {
			end_found = 1;
			break;
		}
		(*nsectors)++;
	}
	if (!start_found || !end_found) {
		/* Start sector not found */
		dfu_err("Internal error, sectors map must be wrong\n");
		return NULL;
	}
	return area;
}

static int erased(struct dfu_target *target, phys_addr_t addr, unsigned long l,
		  int *to_be_erased, int *n_to_be_erased, int max_to_be_erased)
{
	int start_sec, nsec, i, index, ret = 1;
	const struct stm32_memory_area *a = map_chunk_address(target, addr, l,
							      &start_sec,
							      &nsec);

	*n_to_be_erased = 0;
	if (a->type != FLASH)
		return ret;
	if (nsec > max_to_be_erased) {
		dfu_err("stm32-usart, %s: too many sectors to be erased\n",
			__func__);
		return -1;
	}
	for (i = 0, index = start_sec - a->sectors_offset; i < nsec;
	     i++, index++)
		if (!test_bit(index, a->sectors_bitmask_ptr)) {
			to_be_erased[(*n_to_be_erased)++] = index +
				a->sectors_offset;
			ret = 0;
		}
	return ret;
}

static void mark_erased(const struct stm32_memory_area *a,
			int *to_be_erased, int n_to_be_erased)
{
	int i;

	for (i = 0; i < n_to_be_erased; i++)
		set_bit(to_be_erased[i] - a->sectors_offset,
			a->sectors_bitmask_ptr);
}

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

static void _erase_done(struct dfu_target *target,
			const struct dfu_cmddescr *descr)
{
	struct stm32_usart_data *priv = target->priv;

	if (descr->state->status != DFU_CMD_STATUS_OK) {
		dfu_err("ERASE\n");
		dfu_notify_error(target->dfu);
		return;
	}
	dfu_log("Erase OK\n");
	mark_erased(priv->erase_area, priv->to_be_erased, priv->n_to_be_erased);
	priv->curr_descr = NULL;
}

static int start_erasing(struct dfu_target *target,
			 const struct stm32_memory_area *a,
			 int *sectors, int nsectors)
{
	int i;
	static const uint8_t cmdb_ext[] = { 0x44, 0xbb, };
	static const uint8_t cmdb[] = { 0x43, 0xbc, };
	/* Contains number of sectors and sectors indices */
	static uint16_t se_16[MAX_NSECTORS_ERASE + 1];
	static uint8_t se[MAX_NSECTORS_ERASE + 1];
	static uint8_t ack, checksum;
	static struct dfu_cmdbuf cmds[] = {
		[0] = {
			.dir = OUT,
			.len = sizeof(cmdb),
		},
		[1] = {
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 200,
			.completed = _check_ack,
		},
		[2] = {
			.dir = OUT,
			.flags = START_CHECKSUM|SEND_CHECKSUM,
		},
		[3] = {
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 10000,
			.completed = _check_ack,
		},
	};
	static struct dfu_cmddescr descr0 = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
		.checksum_ptr = &checksum,
		.checksum_size = sizeof(checksum),
		.checksum_update = checksum_update,
		.completed = _erase_done,
	};
	struct stm32_usart_data *priv = target->priv;
	int ext = priv->target_flags & STM32_EXTENDED_MEMORY_ERASE;

	dfu_dbg("Starting memory erase (%s) start = %d, n = %d\n",
		ext ? "EXTENDED" : "STANDARD", sectors[0], nsectors);
	if (nsectors > MAX_NSECTORS_ERASE) {
		dfu_err("%s invalid number of sectors\n", __func__);
		return -1;
	}
	for (i = 0; i < nsectors; i++)
		priv->to_be_erased[i] = sectors[i];
	priv->n_to_be_erased = nsectors;
	priv->erase_area = a;
	if (ext) {
		cmds[0].buf.out = cmdb_ext;
		se_16[0] = cpu_to_be16(nsectors - 1);
		for (i = 0; i < nsectors; i++)
			se_16[1 + i] = cpu_to_be16(sectors[i]);
		cmds[2].buf.out = se_16;
	} else {
		cmds[0].buf.out = cmdb;
		se[0] = nsectors - 1;
		for (i = 0; i < nsectors; i++)
			se[1 + i] = sectors[i];
		cmds[2].buf.out = se;
	}
	cmds[2].len = (1 + nsectors);
	if (ext)
		cmds[2].len <<= 1;
	descr0.state = &priv->cmd_state;
	descr0.timeout = &priv->cmd_timeout;
	priv->curr_descr = &descr0;
	return dfu_cmd_start(target, &descr0);
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
	if (!target->pars) {
		dfu_err("%s: no parameters received\n", __func__);
		return -1;
	}
	dfu_log("STM32-USART target initialized\n");
	return 0;
}

/* Remove this ? */
int stm32_usart_probe(struct dfu_target *target)
{
	int stat = 0, i;
	struct stm32_get_cmd_reply r1;
	struct stm32_gid_cmd_reply r2;
	struct stm32_usart_data *priv = target->priv;
	const struct stm32_device_data *pars = target->pars;

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
	if (r2.pid[0] != pars->part_id[0] ||
	    r2.pid[1] != pars->part_id[1]) {
		dfu_err("Unexpected part id\n");
		return -1;
	}
	if (pars->device_probe)
		stat = pars->device_probe(target);
	dfu_log("stm32 target probed (%s)\n", stat < 0 ? "ERROR" : "OK");
	return stat;
}

static void _chunk_done(struct dfu_target *target,
			const struct dfu_cmddescr *descr)
{
	struct stm32_usart_data *priv = target->priv;

	if (descr->state->status == DFU_CMD_STATUS_OK)
		dfu_dbg("chunk 0x%08x programmed OK\n",
			(unsigned int)priv->curr_chunk_addr);
	else {
		dfu_err("ERROR PROGRAMMING CHUNK @0x%08x\n",
			(unsigned int)priv->curr_chunk_addr);
		dfu_notify_error(target->dfu);
		return;
	}
	dfu_binary_file_chunk_done(target->dfu->bf, priv->curr_chunk_addr,
				   descr->state->status == DFU_CMD_STATUS_OK ?
				   0 : -1);
	priv->curr_descr = NULL;
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
		if (dfu_interface_has_target_reset(interface))
			stat = dfu_interface_target_reset(interface);
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

	if (dfu_interface_has_target_run(interface))
		return dfu_interface_target_run(interface);
	if (dfu_interface_has_target_reset(interface))
		return dfu_interface_target_reset(interface);
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

static int stm32_usart_ignore_chunk_alignment(struct dfu_target *target)
{
	return 1;
}

int stm32_usart_read_memory(struct dfu_target *target, void *buf,
			    phys_addr_t _addr, unsigned long sz)
{
	static const uint8_t cmdb[] = { 0x11, 0xee, };
	static uint8_t ack, checksum;
	static uint32_t addr;
	static uint8_t nbytes;
	static struct dfu_cmdbuf cmds[] = {
		[0] = {
			.dir = OUT,
			.buf = {
				.out = cmdb,
			},
			.len = sizeof(cmdb),
		},
		[1] = {
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 200,
			.completed = _check_ack,
		},
		[2] = {
			.dir = OUT,
			.buf = {
				.out = &addr,
			},
			.flags = START_CHECKSUM|SEND_CHECKSUM,
			.len = sizeof(addr),
		},
		[3] = {
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 100,
			.completed = _check_ack,
		},
		[4] = {
			.dir = OUT,
			.buf = {
				.out = &nbytes,
			},
			.len = sizeof(nbytes),
		},
		[5] = {
			.dir = IN,
			.buf = {
				.in = &ack,
			},
			.len = sizeof(ack),
			.timeout = 100,
			.completed = _check_ack,
		},
		[6] = {
			.dir = IN,
			.timeout = 500,
		},
	};
	static struct dfu_cmddescr descr0 = {
		.cmdbufs = cmds,
		.ncmdbufs = ARRAY_SIZE(cmds),
		.checksum_ptr = &checksum,
		.checksum_size = sizeof(checksum),
		.checksum_update = checksum_update,
	};
	struct stm32_usart_data *priv = target->priv;


	if (sz > 256) {
		dfu_err("%s: trying to read more than 256 bytes\n", __func__);
		return -1;
	}
	if (_addr & 0x3) {
		dfu_err("%s: trying to read from unaligned address\n",
			__func__);
		return -1;
	}
	addr = _addr;
	cmds[6].buf.in = buf;
	cmds[6].len = sz - 1;
	
	descr0.state = &priv->cmd_state;
	descr0.timeout = &priv->cmd_timeout;
	priv->curr_descr = &descr0;
	return dfu_cmd_do_sync(target, &descr0);
}

static int stm32_usart_must_erase(struct dfu_target *target, phys_addr_t addr,
				  unsigned long l)
{
	int start, n, stat, to_be_erased[MAX_NSECTORS_ERASE], n_to_be_erased;
	const struct stm32_memory_area *a =
		map_chunk_address(target, addr, l, &start, &n);

	if (!a) {
		dfu_dbg("%s: could not map chunk @0x%08x\n", __func__,
			(unsigned int)addr);
		return 0;
	}
	dfu_dbg("chunk @0x%08x, size = %lu, mapped to area %s, "
		"start_sector = %d, nsectors = %d\n", (unsigned int)addr,
		l, a->name, start, n);
	if (a->type != FLASH)
		/* Not a flash area, we don't mind about it */
		return 0;
	/*
	 * Flash area: if already erased, return 0, otherwise return 1
	 * and start erasing
	 * Note that a chunk can be 256 bytes long max, so this will correspond
	 * to max 2 sectors to be erased (one most likely).
	 */
	stat = erased(target, addr, l, to_be_erased, &n_to_be_erased,
		      ARRAY_SIZE(to_be_erased));
	if (stat < 0) {
		dfu_err("stm32-usart: erased() returns error\n");
		dfu_notify_error(target->dfu);
		return 0;
	}
	if (stat) {
		/*
		 * Sectors corresponding to this chunk have already been erased
		 */
		dfu_dbg("%s: chunk has been erased\n", __func__);
		return 0;
	}
	if (start_erasing(target, a, to_be_erased, n_to_be_erased) < 0) {
		dfu_err("stm32-usart: start_erasing() returns error\n");
		dfu_notify_error(target->dfu);
		return 0;
	}
	return 1;
}

static int stm32_usart_fini(struct dfu_target *target)
{
	const struct stm32_device_data *pars = target->pars;
	const struct stm32_memory_area *areas = pars->areas[pars->boot_mode];
	int nareas = pars->nareas[pars->boot_mode], i, n;

	for (i = 0; i < nareas; i++) {
		if (!areas[i].sectors_bitmask_ptr || !areas[i].nsectors)
			continue;
		n = areas[i].nsectors / (sizeof(unsigned long) << 3);
		if (areas[i].nsectors % (sizeof(unsigned long) << 3))
			n++;
		memset(areas[i].sectors_bitmask_ptr, 0,
		       n * sizeof(unsigned long));
	}
	return 0;
}

struct dfu_target_ops stm32_dfu_target_ops = {
	.init = stm32_usart_init,
	.probe  = stm32_usart_probe,
	.chunk_available = stm32_usart_chunk_available,
	.reset_and_sync = stm32_usart_reset_and_sync,
	.run = stm32_usart_run,
	.on_interface_event = stm32_usart_on_interface_event,
	.on_idle = stm32_usart_on_idle,
	.get_write_chunk_size = stm32_usart_get_write_chunk_size,
	.ignore_chunk_alignment = stm32_usart_ignore_chunk_alignment,
	.read_memory = stm32_usart_read_memory,
	.must_erase = stm32_usart_must_erase,
	.fini = stm32_usart_fini,
};
