#ifndef __DFU_LINUX_H__
#define __DFU_LINUX_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>


struct linux_event_data {
	int fd;
	int events;
};

static inline unsigned get_time(void)
{
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return tv.tv_sec * 1000000 + tv.tv_usec;
}

extern const struct dfu_interface_ops linux_serial_stm32_interface_ops;
extern const struct dfu_interface_ops linux_serial_arduino_uno_interface_ops;
extern const struct dfu_interface_ops linux_spi_bp_nordic_target_interface_ops;
extern const struct dfu_host_ops linux_dfu_host_ops;

#define dfu_log(a,args...) fprintf(stderr, "[%08u] DFU: " a, get_time(), ##args)
#define dfu_err(a,args...) fprintf(stderr, "[%08u] DFU ERROR: " a, get_time(), \
				   ##args)
#define dfu_log_noprefix(a,args...) fprintf(stderr, a, ##args)

#include <string.h>

#ifdef __cplusplus
}
#endif

#endif /* __DFU_LINUX_H__ */

