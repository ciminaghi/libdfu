
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "esp8266-serial.h"

#define DEFAULT_BAUD 115200

/* Depends on the sdk we're building for :-( */
#ifndef FUNC_U0RXD
#define FUNC_U0RXD 0
#endif

#define PARITY_MASK (BIT(0)|BIT(1))

static int baud = DEFAULT_BAUD;
static unsigned long base = REG_UART_BASE(0);

static int setup_pars(const struct dfu_serial_pars *pars)
{
	uint32_t v, tmp;

	/* Only parity is supported at the moment */
	switch(pars->parity) {
	case PARITY_NONE:
		return 0;
	case PARITY_EVEN:
		v = BIT(1);
		break;
	case PARITY_ODD:
		v = BIT(1)|BIT(0);
		break;
	default:
		dfu_err("esp8266-serial %s: invalid parity %u\n", __func__,
			pars->parity);
		return -1;
	}
	tmp = readl(base + UART_CONF0);
	tmp &= ~PARITY_MASK;
	writel(tmp | v, base + UART_CONF0);
	return 0;
}

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
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_U0RXD);
	/* Reset FIFO */
	v = readl(base + UART_CONF0);
	v |= (UART_RXFIFO_RST | UART_TXFIFO_RST);
	writel(v, base + UART_CONF0);
	v &= ~(UART_RXFIFO_RST | UART_TXFIFO_RST);
	writel(v, base + UART_CONF0);
	if (pars)
		return setup_pars(pars);
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
	return ret ? DFU_INTERFACE_EVENT : 0;
}

int esp8266_serial_fini(struct dfu_interface *iface)
{
	uint32_t v;

	/* Reset FIFOs */
	v = readl(base + UART_CONF0);
	v |= (UART_RXFIFO_RST | UART_TXFIFO_RST);
	writel(v, base + UART_CONF0);
	return 0;
}
