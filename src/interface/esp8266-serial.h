/*
 * Internal header for esp8266 serial interface
 */
#ifndef __ESP8266_SERIAL_H__
#define __ESP8266_SERIAL_H__

#include "dfu.h"

int esp8266_serial_open(struct dfu_interface *iface,
			const char *path, const void *pars);
int esp8266_serial_write(struct dfu_interface *iface,
			 const char *buf, unsigned long size);
int esp8266_serial_read(struct dfu_interface *iface, char *buf,
			unsigned long size);

#endif /* __ESP8266_SERIAL_H__ */

