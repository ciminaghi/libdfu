/*
 * Internal header for esp8266 serial interface
 */
#ifndef __ESP8266_SPI_H__
#define __ESP8266_SPI_H__

#include "dfu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REG_SPI_BASE(i)		(0x60000200 - (i << 8))

/* Registers' offset */
#define SPI_CMD		0x00
#  define SPI_BUSY	BIT(18)

#define SPI_ADDR	0x04
#define SPI_CTRL	0x08
#define SPI_RD_STATUS	0x10
#define SPI_CTRL2	0x14
#define SPI_CLOCK	0x18
#  define SPI_CLOCK_PRESCALER_SHIFT 18
#  define SPI_CLOCK_DIVIDER_SHIFT 12
	
#define SPI_USER	0x1c
#define SPI_USER1	0x20
#  define MOSI_BITLEN_MASK 0x1ff
#  define MOSI_BITLEN_SHIFT 17
#  define MISO_BITLEN_MASK 0x1ff
#  define MISO_BITLEN_SHIFT 8

#define SPI_USER2	0x24
#define SPI_PIN		0x2c
#define SPI_SLAVE	0x30
#  define SLAVE_MODE	BIT(30)
	
#define SPI_W(j)	(0x40 + ((j) << 2))

int esp8266_spi_open(struct dfu_interface *iface,
		     const char *path, const void *pars);
int esp8266_spi_write(struct dfu_interface *iface,
		      const char *buf, unsigned long size);
int esp8266_spi_write_read(struct dfu_interface *iface,
			   const char *out_buf, char *in_buf,
			   unsigned long size);
int esp8266_spi_poll_idle(struct dfu_interface *iface);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_SPI_H__ */

