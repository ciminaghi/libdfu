
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"

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

#define DEFAULT_BAUD 115200
#define UART0_BASE 0x60000000

static int baud = DEFAULT_BAUD;
static unsigned long base = UART0_BASE;
static int rx_fifo_threshold = 0;

/* Simple I/O accessors */
uint32_t *regs = 0;

static inline uint32_t readl(unsigned long reg)
{
	return regs[reg / 4];
}

static inline void writel(uint32_t val, unsigned long reg)
{
	regs[reg / 4] = val;
}

static inline unsigned int get_rx_fifo_cnt(unsigned long base)
{
	return readl(base + UART_STATUS) & 0xffUL;
}

void esp8266_uart_irq_handler(void *dummy)
{
	uint32_t status;

	status = readl(base + UART_INT_ST);
	if ((status & FRAMING_ERROR) || (status & OVERRUN_ERROR)) {
		dfu_dbg("%s: ERROR (0x%08x)\n", __func__, (unsigned int)status);
		goto end;
	}
end:
	writel(status, base + UART_INT_CLR);
}

/*
 * FIXME: currently ignores path and works with serial0 only
 */
static int esp8266_serial_open(struct dfu_interface *iface,
			       const char *path, const void *pars)
{
	uint32_t v;

	uart_div_modify(0, UART_CLK_FREQ / baud);
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
	/* Attach interrupt handler */
	ETS_UART_INTR_ATTACH(esp8266_uart_irq_handler, NULL);
	return 0;
}

static inline unsigned int get_tx_fifo_cnt(unsigned long base)
{
	return (readl(base + UART_STATUS) >> 16) & 0xffUL;
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
	int available = get_rx_fifo_cnt(base), copied = min(available, size);

	if (!available)
		return -1;
	memcpy(buf, (void *)(base + UART_FIFO), min(available, copied));
	return copied;
}


int esp8266_serial_target_reset(struct dfu_interface *iface)
{
	return -1;
}


const struct dfu_interface_ops esp8266_serial_interface_ops = {
	.open = esp8266_serial_open,
	.write = esp8266_serial_write,
	.read = esp8266_serial_read,
	.target_reset = esp8266_serial_target_reset,
};

