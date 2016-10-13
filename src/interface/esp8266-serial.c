
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "esp8266-serial.h"

#define DEFAULT_BAUD 115200

static int baud = DEFAULT_BAUD;
static unsigned long base = REG_UART_BASE(0);
static int rx_fifo_threshold = 0;

/*
 * FIXME: currently ignores path and works with serial0 only
 */
int esp8266_serial_open(struct dfu_interface *iface,
			const char *path, const void *pars)
{
	uint32_t v;

	uart_div_modify(0, UART_CLK_FREQ / baud);
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0RXD_U);
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_U0TXD_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0TXD_U, FUNC_U0TXD);
	/* Reset FIFO */
	v = readl(base + UART_CONF0);
	v |= (UART_RXFIFO_RST | UART_TXFIFO_RST);
	writel(v, base + UART_CONF0);
	v &= ~(UART_RXFIFO_RST | UART_TXFIFO_RST);
	writel(v, base + UART_CONF0);
	/* Set rx fifo threshold */
	v = readl(base + UART_CONF1);
	v &= ~0x7f;
	v |= (rx_fifo_threshold & 0x7f);
	writel(v, base + UART_CONF1);
	return 0;
}

static int _putc(char c)
{
	while (get_tx_fifo_cnt(base) >= 126UL);
	writel(c, base + UART_FIFO);
	return 1;
}

int esp8266_serial_write(struct dfu_interface *iface,
			 const char *buf, unsigned long size)
{
	unsigned long i;

	for (i = 0; i < size; i++)
		if (_putc(buf[i]) < 0)
			return -1;
	return size;
}

int esp8266_serial_read(struct dfu_interface *iface, char *buf,
			unsigned long size)
{
	int available = get_rx_fifo_cnt(base), copied = min(available, size), i;

	if (!available)
		return -1;
	for (i = 0; i < copied; i++)
		buf[i] = readl(base + UART_FIFO);
	return copied;
}

int esp8266_serial_poll_idle(struct dfu_interface *iface)
{
	int ret;

	ret = get_rx_fifo_cnt(base);
	dfu_dbg("%s returns %d\n", __func__, ret);
	return ret;
}
