/*
 * STK500 firmware update protocol implementation
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2016
 */

#include "dfu.h"
#include "dfu-internal.h"

int stk500_init(struct dfu_target *target,
		struct dfu_interface *interface)
{
	target->interface = interface;
	dfu_log("%s: target = %p, interface = %p\n", __func__,
		target, interface);
	return 0;
}

/* Remove this ? */
int stk500_probe(struct dfu_target *target)
{
	return -1;
}

/* Chunk of binary data is available for writing */
int stk500_chunk_available(struct dfu_target *target,
			   phys_addr_t address,
			   const void *buf, unsigned long sz)
{
	return 0;
}

static int stk500_target_erase_all(struct dfu_target *target)
{
	return -1;
}

/* Reset and sync target */
static int stk500_reset_and_sync(struct dfu_target *target)
{
	return -1;
}

/* Let target run */
int stk500_run(struct dfu_target *target)
{
	return -1;
}

/* Interface event */
int stk500_on_interface_event(struct dfu_target *target)
{
	return -1;
}

struct dfu_target_ops stk500_dfu_target_ops = {
	.init = stk500_init,
	.probe  = stk500_probe,
	.chunk_available = stk500_chunk_available,
	.reset_and_sync = stk500_reset_and_sync,
	.erase_all = stk500_target_erase_all,
	.run = stk500_run,
	.on_interface_event = stk500_on_interface_event,
};
