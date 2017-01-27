#ifndef __DFU_STM32_H__
#define __DFU_STM32_H__

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

struct stm32_memory_area {
	enum memory_type type;
	phys_addr_t start;
	unsigned long size;
};


struct stm32_device_data {
	/* Single and dual bank mode memory maps */
	const struct stm32_memory_area *areas[2];
	int nareas;
	/* Points to ram containing a bitmask for erased/to-be-erased sectors */
	unsigned long *sectors_bitmask_ptr;
	uint8_t part_id[2];
	enum boot_mode boot_mode;
	int (*device_probe)(struct dfu_target *);
};

#ifdef __cplusplus
}
#endif

#endif /* __DFU_STM32_H__ */
