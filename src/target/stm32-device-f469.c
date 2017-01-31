#include "stm32-device.h"

/*
 * Flash is only supported on 1MB devices at the moment
 */

static unsigned long bitmask_ptr0, bitmask_ptr1;

static const struct stm32_flash_sector _double_bank_sectors0[] = {
	[0 ... 3] = {
		.size = 16 * 1024,
	},
	[4] = {
		.size = 64 * 1024,
	},
	[5 ... 11] = {
		.size = 128 * 1024,
	},

};

static const struct stm32_flash_sector _double_bank_sectors1[] = {
	[0 ... 3] = {
		.size = 16 * 1024,
	},
	[4] = {
		.size = 64 * 1024,
	},
	[5 ... 11] = {
		.size = 128 * 1024,
	},
};

static const struct stm32_memory_area _double_bank_areas[] = {
	{
		.name = "FLASH BANK1",
		.type = FLASH,
		.start = 0x08000000,
		.size = 1024 * 1024,
		.sectors = _double_bank_sectors0,
		.nsectors = ARRAY_SIZE(_double_bank_sectors0),
		.sectors_bitmask_ptr = &bitmask_ptr0,
		.sectors_offset = 0,
	},
	{
		.name = "FLASH BANK2",
		.type = FLASH,
		.start = 0x08000000 + 1024 * 1024,
		.size = 1024 * 1024,
		.sectors = _double_bank_sectors1,
		.nsectors = ARRAY_SIZE(_double_bank_sectors0),
		.sectors_bitmask_ptr = &bitmask_ptr1,
		.sectors_offset = 12,
	},
	{
		.name = "SRAM",
		.type = RAM,
		.start = 0x20000000,
		.size = 320 * 1024,
	},
};

const struct stm32_device_data stm32f469bi_device_data = {
	/* Only double bank mode supported */
	.areas = { NULL, _double_bank_areas, },
	.nareas = { -1, ARRAY_SIZE(_double_bank_areas), },
	.part_id = { 0x04, 0x34, },
	.boot_mode = DUAL_BANK,
};
