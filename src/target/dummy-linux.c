/*
 * STM32 firmware update protocol implementation
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2016
 */

/* Dummy target, we just print out target operations */

#include "dfu.h"
#include "dfu-internal.h"


static int dummy_init(struct dfu_target *target,
		      struct dfu_interface *interface)
{
	printf("%s: target = %p, interface = %p\n", __func__,
	       target, interface);
	return 0;
}



static int dummy_probe(struct dfu_target *target)
{
	printf("%s: target = %p\n", __func__, target);
	return 0;
}

/* Chunk of binary data is available for writing */
static int dummy_chunk_available(struct dfu_target *target,
				 phys_addr_t address,
				 const void *buf, unsigned long sz)
{
	printf("%s: target = %p, address = 0x%08x, sz = %lu\n", __func__,
	       /* esp8266: uint32_t is unsigned long */
	       target, (unsigned int)address, sz);
	return 0;
}

/* Reset and sync target */
static int dummy_reset_and_sync(struct dfu_target *target)
{
	printf("%s: target = %p\n", __func__, target);
	return 0;
}

/* Let target run */
static int dummy_run(struct dfu_target *target)
{
	printf("%s: target = %p\n", __func__, target);
	return 0;
}


struct dfu_target_ops dummy_dfu_target_ops = {
	.init = dummy_init,
	.probe = dummy_probe,
	.chunk_available = dummy_chunk_available,
	.reset_and_sync = dummy_reset_and_sync,
	.run = dummy_run,
};
