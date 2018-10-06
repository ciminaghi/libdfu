/*
 * Internal header for esp8266 serial interface
 */
#ifndef __ESP8266_SPI_H__
#define __ESP8266_SPI_H__

#include "dfu.h"

#ifdef __cplusplus
extern "C" {
#endif

int esp8266_spi_open(struct dfu_interface *iface,
		     const char *path, const void *pars);
int esp8266_spi_write(struct dfu_interface *iface,
		      const char *buf, unsigned long size);
int esp8266_spi_read(struct dfu_interface *iface,
		     char *buf, unsigned long size);
int esp8266_spi_write_read(struct dfu_interface *iface,
			   const char *out_buf, char *in_buf,
			   unsigned long size);
int esp8266_spi_poll_idle(struct dfu_interface *iface);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_SPI_H__ */

