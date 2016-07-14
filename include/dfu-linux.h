#ifndef __DFU_LINUX_H__
#define __DFU_LINUX_H__

#include <stdio.h>

extern const struct dfu_interface_ops linux_serial_interface_ops;
extern const struct dfu_host_ops linux_dfu_host_ops;

#define dfu_log(a,args...) printf("DFU:" a, ##args)
#define dfu_err(a,args...) fprintf(stderr, "DFU ERROR: " a, ##args)

#include <string.h>


#endif /* __DFU_LINUX_H__ */

