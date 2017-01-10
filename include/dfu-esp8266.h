#ifndef __DFU_ESP8266_H__
#define __DFU_ESP8266_H__

#include <c_types.h>
#include <machine/endian.h>

#ifdef __cplusplus
extern "C" {
#endif

extern const struct dfu_interface_ops esp8266_serial_star8_interface_ops;
extern const struct dfu_interface_ops
esp8266_serial_arduinouno_hacked_interface_ops;
extern const struct dfu_interface_ops
esp8266_serial_arduino_unowifi_interface_ops;
extern const struct dfu_interface_ops
esp8266_spi_arduinouno_hacked_interface_ops;
extern const struct dfu_host_ops esp8266_dfu_host_ops;
extern int _dfu_log(const char *, ...);

#ifdef __cplusplus
}
#endif

#ifndef ARDUINO

#define dfu_log(a,args...) do { os_printf("DFU [%8u] ", system_get_time()) ; \
		os_printf(a, ##args); } while(0)
#define dfu_err(a,args...) do { os_printf("DFU [%8u] ", system_get_time()) ; \
		os_printf("ERROR: " a, ##args) ; } while(0)
#define dfu_log_noprefix(a,args...) os_printf(a, ##args)
#else
#define dfu_log(a,args...) _dfu_log("DFU " a, ##args)
#define dfu_err(a,args...) do { _dfu_log("DFU ERROR: "); \
	_dfu_log(a, ##args) ; } while(0)
#define dfu_log_noprefix(a,args...) _dfu_log(a, ##args)
#endif

/* Simple I/O accessors */
extern volatile uint32_t *regs;

static inline uint32_t readl(unsigned long reg)
{
	return regs[reg / 4];
}

static inline void writel(uint32_t val, unsigned long reg)
{
	regs[reg / 4] = val;
}

#include <osapi.h>
#ifndef ARDUINO
#include "espmissingincludes.h"
#endif

#endif /* __DFU_ESP8266_H__ */

