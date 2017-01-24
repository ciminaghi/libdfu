
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-serial.h"

#define UART_BIT_NUM_S			2

/*
 * Hacked vanilla arduinouno with homemade 8266 shield: uses UART0 and GPIO5
 * for target reset, UART1 tx as a serial console.
 * This configuration is used for 8266 + stk500 debugging
 */

#ifndef ARDUINO
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

/* Gpio5 is target's reset */
int esp8266_serial_arduinouno_hacked_target_reset(struct dfu_interface *iface)
{
	static int first = 0;

	if (!first) {
		dfu_log("Redirecting console to uart1\n");
		uart1_init();
		os_install_putc1(uart1_putc);
		dfu_log("Console redirected to uart1\n");
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
		first = 1;
	}
	GPIO_OUTPUT_SET(5, 0);
	os_delay_us(1000);
	GPIO_OUTPUT_SET(5, 1);
	os_delay_us(200000);
	return 0;
}

#else /* ARDUINO */

int esp8266_serial_arduinouno_hacked_target_reset(struct dfu_interface *iface)
{
	pinMode(5, OUTPUT);
	digitalWrite(5, 0);
	delay(1);
	digitalWrite(5, 1);
	delay(200);
	return 0;
}

#endif /* ARDUINO */

const struct dfu_interface_ops
esp8266_serial_arduinouno_hacked_interface_ops = {
	.open = esp8266_serial_open,
	.write = esp8266_serial_write,
	.read = esp8266_serial_read,
	.target_reset = esp8266_serial_arduinouno_hacked_target_reset,
	.poll_idle = esp8266_serial_poll_idle,
	.fini = esp8266_serial_fini,
};
