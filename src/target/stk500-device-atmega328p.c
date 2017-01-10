/*
 * STK500v1 firmware update protocol implementation - device data for
 * ATMega328p (from avrdude config file).
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2016
 */
#include "stk500-device.h"

const struct stk500_device_mem atmega328p_flash = {
	.length = 32768,
	.paged = 1,
	.page_size = 128,
};

const struct stk500_device_data atmega328p_device_data = {
	.devcode = 0x86,
	.pagel = 0xd7,
	.bs2 = 0xc2,
	/* FIXME ? */
	.rd = 0,
	.flash = &atmega328p_flash,
	.chip_erase = { 0xac, 0x80, 00, 00, },
	.enter_progmode = { 0xac, 0x53, 0x00, 0x00, },
	.timeout = 200,
	.chip_erase_delay = 9000,
	.stabdelay = 100,
	.cmdexedelay = 25,
	.synchloops = 32,
	.bytedelay = 0,
	.pollindex = 3,
	.pollvalue = 0x53,
	.predelay = 1,
	.postdelay = 1,
};
