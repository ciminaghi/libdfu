#ifndef __STK500_DEVICE_H__
#define __STK500_DEVICE_H__

#include "dfu.h"
#include "dfu-internal.h"

enum reset_disposition {
	RESET_DEDICATED = 0,
	RESET_IO = 1,
};

struct stk500_device_mem {
	uint32_t length;
	uint8_t readback[2];
	uint8_t paged;
	uint16_t page_size;
};

struct stk500_device_data {
	uint8_t devcode;
	uint8_t pagel;
	uint8_t bs2;
	enum reset_disposition rd;
	const struct stk500_device_mem *lock;
	const struct stk500_device_mem *fuse;
	const struct stk500_device_mem *lfuse;
	const struct stk500_device_mem *hfuse;
	const struct stk500_device_mem *efuse;
	const struct stk500_device_mem *flash;
	const struct stk500_device_mem *eeprom;
	
	uint8_t chip_erase[4];
	uint8_t enter_progmode[4];
	unsigned int timeout;
	unsigned int chip_erase_delay;
	unsigned int stabdelay;
	unsigned int cmdexedelay;
	unsigned int synchloops;
	unsigned int bytedelay;
	unsigned int pollindex;
	unsigned int pollvalue;
	unsigned int predelay;
	unsigned int postdelay;
};

#endif /* __STK500_DEVICE_H__ */

