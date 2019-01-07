
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-spi.h"

#include "Arduino.h"

/*
 * Arduino stamp v13: uses SPI direct esp8266 <-> atmega328
 * connection, GPIO5 for target reset, UART0 as a serial console,
 */

static int
esp8266_spi_stampv13_target_reset(struct dfu_interface *iface)
{
	pinMode(4, OUTPUT);
	digitalWrite(4, 0);
	delay(2);
	// Become spi master
	pinMode(SCK, OUTPUT);
	digitalWrite(SCK, 0);
	delay(2);
	digitalWrite(4, 0);
	delay(2);
	digitalWrite(4, 1);
	delay(2);
	pinMode(SCK, SPECIAL);
	delay(200);
	return 0;
}

static int
esp8266_spi_stampv13_target_run(struct dfu_interface *iface)
{
	/* Release reset on done and let the target run */
	digitalWrite(4, 1);
	delay(2);
	digitalWrite(4, 0);
	delay(2);
	digitalWrite(4, 1);
	return 0;
}

const struct dfu_interface_ops
esp8266_spi_stampv13_interface_ops = {
	.open = esp8266_spi_open,
	.write = esp8266_spi_write,
	.read = NULL,
	.write_read = esp8266_spi_write_read,
	.target_reset = esp8266_spi_stampv13_target_reset,
	.target_run = esp8266_spi_stampv13_target_run,
	.poll_idle = esp8266_spi_poll_idle,
	.done = NULL,
};
