
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-serial.h"

/* REPEATED CODE IS EVIL: FIXME ! (uart1_putc, uart1_init) */

#define UART_BIT_NUM_S			2

/* Console gets redirected to uart1 */
static void uart1_init(void)
{
	uint32_t v;
	unsigned long base = REG_UART_BASE(1);

	/* Setup tx pin */
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_GPIO2_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
	/* Set baud rate */
	uart_div_modify(1, UART_CLK_FREQ / 115200);
	/* Set bit number and parity */
	writel(0x3 << UART_BIT_NUM_S, base + UART_CONF0);
	/* Reset tx fifo */
	v = readl(base + UART_CONF0);
	v |= UART_RXFIFO_RST | UART_TXFIFO_RST;
	writel(v, base + UART_CONF0);
	v &= ~(UART_RXFIFO_RST | UART_TXFIFO_RST);
	writel(v, base + UART_CONF0);
}

static void uart1_putc(char c)
{
	int i = 0;

	/* MMMMHHH FIXME: IS FIFO FREE ? */
	while (get_tx_fifo_cnt(REG_UART_BASE(1)) >= 126UL && i < 20000)
		i++;
	if (c == '\n')
		writel('\r', REG_UART_BASE(1) + UART_FIFO);
	writel(c, REG_UART_BASE(1) + UART_FIFO);
	/* Make this synchronous */
	while (get_tx_fifo_cnt(REG_UART_BASE(1)));
}

static int esp8266_serial_star8_open(struct dfu_interface *iface,
				     const char *path, const void *pars)
{
	static int first = 0;
	static const struct dfu_serial_pars _pars = {
		.parity = PARITY_EVEN,
	};

	if (!first) {
		dfu_log("Redirecting console to uart1\n");
		uart1_init();
		os_install_putc1(uart1_putc);
		dfu_log("Console redirected to uart1\n");
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
		first = 1;
	}
	return esp8266_serial_open(iface, path, pars ? pars : &_pars);
}


static int esp8266_serial_star8_target_reset(struct dfu_interface *iface)
{
	/*
	 * BOOT0  ----+        +--------
	 *            |        |
	 *            +--------+
	 *
	 * RST    ----+            +----
	 *            |            |
	 *            +------------+
	 *
	 * GPIO4 -> BOOT0
	 * GPIO12 -> #RST
	 */
	GPIO_OUTPUT_SET(4, 0);
	GPIO_OUTPUT_SET(12, 0);
	os_delay_us(2000);
	GPIO_OUTPUT_SET(4, 1);
	GPIO_OUTPUT_SET(12, 1);
	os_delay_us(5000);
	GPIO_OUTPUT_SET(4, 0);
	os_delay_us(10000);
	GPIO_OUTPUT_SET(12, 0);
	os_delay_us(10000);
	return 0;
}

static int esp8266_serial_star8_target_run(struct dfu_interface *iface)
{
	/*
	 * BOOT0  ---------------------  LOW
	 *
	 * RST    ----+            +----
	 *            |            |
	 *            +------------+
	 *
	 * GPIO4 -> BOOT0
	 * GPIO12 -> #RST
	 */
	GPIO_OUTPUT_SET(4, 0);
	GPIO_OUTPUT_SET(12, 0);
	os_delay_us(1000);
	GPIO_OUTPUT_SET(12, 1);
	os_delay_us(1000);
	GPIO_OUTPUT_SET(12, 0);
	return 0;
}


const struct dfu_interface_ops esp8266_serial_star8_interface_ops = {
	.open = esp8266_serial_star8_open,
	.write = esp8266_serial_write,
	.read = esp8266_serial_read,
	.target_reset = esp8266_serial_star8_target_reset,
	.target_run = esp8266_serial_star8_target_run,
};

