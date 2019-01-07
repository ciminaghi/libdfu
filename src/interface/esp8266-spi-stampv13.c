
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-spi.h"

/*
 * Arduino stamp v1.3, should work from v1.1 on
 */

/* Gpio4 is target's reset */
int esp8266_spi_stampv13_target_reset(struct dfu_interface *iface)
{
	GPIO_OUTPUT_SET(4, 0);
	os_delay_us(1000);
	GPIO_OUTPUT_SET(4, 1);
	os_delay_us(200000);
	return 0;
}

static int
esp8266_spi_stampv13_target_run(struct dfu_interface *iface)
{
	/* Release reset on done and let the target run */
	GPIO_OUTPUT_SET(4, 1);
	os_delay_us(2000);
	GPIO_OUTPUT_SET(4, 0);
	os_delay_us(2000);
	GPIO_OUTPUT_SET(4, 1);
	return 0;
}


const struct dfu_interface_ops
esp8266_spi_stampv13_interface_ops = {
	.open = esp8266_spi_open,
	.write = esp8266_spi_write,
	.write_read = esp8266_spi_write_read,
	.target_reset = esp8266_spi_stampv13_target_reset,
	.target_run = esp8266_spi_stampv13_target_run,
	.poll_idle = esp8266_spi_poll_idle,
};
