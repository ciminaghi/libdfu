
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-spi.h"

/*
 * Arduino primo interface. Uses GPIO ??
 * for target reset, UART0 tx as a serial console.
 */

/* Gpio5 is target's reset */
int esp8266_spi_arduinoprimo_target_reset(struct dfu_interface *iface)
{
	/* FIXME: IMPLEMENT THIS */
	return 0;
}

static int
esp8266_spi_arduinoprimo_target_run(struct dfu_interface *iface)
{
	/* FIXME: IMPLEMENT THIS */
	return 0;
}


const struct dfu_interface_ops
esp8266_spi_arduinoprimo_interface_ops = {
	.open = esp8266_spi_open,
	.write = esp8266_spi_write,
	.write_read = esp8266_spi_write_read,
	.target_reset = esp8266_spi_arduinoprimo_target_reset,
	.target_run = esp8266_spi_arduinoprimo_target_run,
	.poll_idle = esp8266_spi_poll_idle,
};
