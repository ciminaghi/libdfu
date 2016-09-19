#ifndef __DFU_ESP8266_H__
#define __DFU_ESP8266_H__

#include <c_types.h>
#include <machine/endian.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const struct dfu_interface_ops esp8266_serial_interface_ops;
extern const struct dfu_host_ops esp8266_dfu_host_ops;

#ifdef __cplusplus
}
#endif

#define dfu_log(a,args...) os_printf("DFU:" a, ##args)
#define dfu_err(a,args...) os_printf("DFU ERROR: " a, ##args)
#define dfu_log_noprefix(a,args...) os_printf(a, ##args)

#include <osapi.h>
#include "espmissingincludes.h"

#endif /* __DFU_ESP8266_H__ */

