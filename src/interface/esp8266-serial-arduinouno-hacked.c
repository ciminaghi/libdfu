
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-serial.h"

#define REG_UART_BASE(i)		(0x60000000 + (i)*0xf00)
#define UART_CONF0(i)			(REG_UART_BASE(i) + 0x20)
#define UART_BIT_NUM_S			2
#define UART_TXFIFO_RST			(BIT(18))
#define UART_FIFO(i)			(REG_UART_BASE(i) + 0x0)



/*
 * Hacked vanilla arduinouno with homemade 8266 shield: uses UART0 and GPIO5
 * for target reset, UART1 tx as a serial console.
 * This configuration is used for 8266 + stk500 debugging
 */

/* Console gets redirected to uart1 */
static void uart1_init(void)
{
	/* Setup tx pin */
	PIN_PULLUP_DIS(PERIPHS_IO_MUX_GPIO2_U);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_U1TXD_BK);
	/* Set baud rate */
	uart_div_modify(1, UART_CLK_FREQ / 115200);
	/* Set bit number and parity */
	WRITE_PERI_REG(UART_CONF0(1), 0x3 << UART_BIT_NUM_S);
	/* Reset tx fifo */
	SET_PERI_REG_MASK(UART_CONF0(1),  UART_TXFIFO_RST);
	CLEAR_PERI_REG_MASK(UART_CONF0(1), UART_TXFIFO_RST);
}

static void uart1_putc(char c)
{
	/* MMMMHHH FIXME: IS FIFO FREE ? */
	if (c == '\n')
		WRITE_PERI_REG(UART_FIFO(1), '\r');
	WRITE_PERI_REG(UART_FIFO(1), c);
}

/* Gpio5 is target's reset */
int esp8266_serial_arduinouno_hacked_target_reset(struct dfu_interface *iface)
{
	os_printf("Resetting target ...");
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);
	GPIO_OUTPUT_SET(5, 0);
	os_delay_us(100000);
	GPIO_OUTPUT_SET(5, 1);
	os_printf("DONE\n");
	os_printf("Redirecting console to uart1\n");
	uart1_init();
	os_install_putc1(uart1_putc);
	os_printf("Console redirected to uart1\n");
	return 0;
}


const struct dfu_interface_ops
esp8266_serial_arduinouno_hacked_interface_ops = {
	.open = esp8266_serial_open,
	.write = esp8266_serial_write,
	.read = esp8266_serial_read,
	.target_reset = esp8266_serial_arduinouno_hacked_target_reset,
};
