/*
 * Internal header for esp8266 serial interface
 */
#ifndef __ESP8266_SERIAL_H__
#define __ESP8266_SERIAL_H__

#include "dfu.h"

#ifdef __cplusplus
extern "C" {
#endif

#define REG_UART_BASE(i)		(0x60000000 + (i)*0xf00)

/* Registers' offset */
#define UART_FIFO	0x00
#define UART_INT_RAW	0x04
#define UART_INT_ST	0x08
# define FRAMING_ERROR  BIT(3)
# define OVERRUN_ERROR  BIT(4)

#define UART_INT_ENA	0x0c
# define UART_RXFIFO_FULL_INT_ENA BIT(0)
#define UART_INT_CLR	0x10
#define UART_CLKDIV	0x14
#define UART_AUTOBAUD	0x18
#define UART_STATUS	0x1c
#define UART_CONF0	0x20
# define UART_RXFIFO_RST BIT(17)
# define UART_TXFIFO_RST BIT(18)
#define UART_CONF1	0x24
#define UART_LOWPULSE	0x28
#define UART_HIGHPULSE	0x2c
#define UART_PULSE_NUM	0x30
#define UART_DATE	0x78
#define UART_ID		0x7c

static inline unsigned int get_rx_fifo_cnt(unsigned long base)
{
	return readl(base + UART_STATUS) & 0xffUL;
}

static inline unsigned int get_tx_fifo_cnt(unsigned long base)
{
	return (readl(base + UART_STATUS) >> 16) & 0xffUL;
}

int esp8266_serial_open(struct dfu_interface *iface,
			const char *path, const void *pars);
int esp8266_serial_write(struct dfu_interface *iface,
			 const char *buf, unsigned long size);
int esp8266_serial_read(struct dfu_interface *iface, char *buf,
			unsigned long size);
int esp8266_serial_poll_idle(struct dfu_interface *iface);
int esp8266_serial_fini(struct dfu_interface *iface);

#ifdef __cplusplus
}
#endif

#endif /* __ESP8266_SERIAL_H__ */

