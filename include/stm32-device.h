#ifndef __DFU_STM32_H__
#define __DFU_STM32_H__

#include "dfu.h"
#include "dfu-internal.h"

#ifdef __cplusplus
extern "C" {
#endif

enum memory_type {
	FLASH = 1,
	RAM = 2,
	OTP = 3,
	OPTIONS = 4,
	INVALID = -1,
};

enum boot_mode {
	SINGLE_BANK = 0,
	DUAL_BANK = 1,
};

struct stm32_flash_sector {
	unsigned long size;
};

struct stm32_memory_area {
	const char *name;
	enum memory_type type;
	phys_addr_t start;
	unsigned long size;
	/* Pointer to sectors map (in case of flash area) */
	const struct stm32_flash_sector *sectors;
	/* Points to ram containing a bitmask for erased/to-be-erased sectors */
	unsigned long *sectors_bitmask_ptr;
	int nsectors;
	int sectors_offset;
};


struct stm32_device_data {
	/* Single and dual bank mode memory maps */
	const struct stm32_memory_area *areas[2];
	int nareas[2];
	uint8_t part_id[2];
	enum boot_mode boot_mode;
	int (*device_probe)(struct dfu_target *);
};

extern int stm32_usart_read_memory(struct dfu_target *target, void *buf,
				   phys_addr_t _addr, unsigned long sz);

#ifdef __cplusplus
}
#endif

#endif /* __DFU_STM32_H__ */
