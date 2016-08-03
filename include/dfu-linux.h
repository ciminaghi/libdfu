#ifndef __DFU_LINUX_H__
#define __DFU_LINUX_H__

#include <stdint.h>
#include <stdio.h>


struct linux_event_data {
	int fd;
	int events;
};

extern const struct dfu_interface_ops linux_serial_interface_ops;
extern const struct dfu_host_ops linux_dfu_host_ops;

#define dfu_log(a,args...) fprintf(stderr, "DFU: " a, ##args)
#define dfu_err(a,args...) fprintf(stderr, "DFU ERROR: " a, ##args)
#define dfu_log_noprefix(a,args...) fprintf(stderr, a, ##args)

#include <string.h>


#endif /* __DFU_LINUX_H__ */

