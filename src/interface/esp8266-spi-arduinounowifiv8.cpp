
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-spi.h"

#include "Arduino.h"

/*
 * Arduino unowifi v8: uses SPI direct esp8266 <-> atmega328
 * connection, GPIO4 for target reset, UART0 as a serial console,
 * GPIO16 (XPD-DCDC) for setting SPI direction (low -> esp8266 is master)
 * THIS FILE IS FOR THE ARDUINO VERSION OF THE LIBRARY, DO NOT TRY TO BUILD IT
 * OUTSIDE THE ARDUINO ENVIRONMENT
 *
 * IMPORTANT NOTICE: this code only works for boards with hw revision 8
 */

static int
esp8266_spi_arduino_unowifiv8_target_reset(struct dfu_interface *iface)
{
	pinMode(4, OUTPUT);
	digitalWrite(4, 0);
	delay(2);
	// Become spi master
	digitalWrite(16, 0);
	pinMode(SCK, OUTPUT);
	digitalWrite(SCK, 0);
	delay(2);
	digitalWrite(4, 1);
	delay(2);
	digitalWrite(4, 0);
	delay(2);
	pinMode(SCK, SPECIAL);
	delay(200);
	return 0;
}

static int
esp8266_spi_arduino_unowifiv8_target_run(struct dfu_interface *iface)
{
	/* Release reset on done and let the target run */
	digitalWrite(4, 1);
	delay(2);
	digitalWrite(4, 0);
	delay(2);
	digitalWrite(4, 1);
	// Make sure we're slave
	digitalWrite(16, 1);
	return 0;
}

static void esp8266_spi_arduino_unowifiv8_done(struct dfu_interface *iface)
{
	// Back to humble slave
	digitalWrite(16, 1);
}

static int esp8266_spi_arduino_unowifiv8_open(struct dfu_interface *iface,
					      const char *path,
					      const void *pars)
{
	// Open but port but remain slave
	pinMode(16, OUTPUT);
	digitalWrite(16, 1);
	return esp8266_spi_open(iface, path, pars);
}

const struct dfu_interface_ops
esp8266_spi_arduino_unowifiv8_interface_ops = {
	.open = esp8266_spi_arduino_unowifiv8_open,
	.write = esp8266_spi_write,
	.read = NULL,
	.write_read = esp8266_spi_write_read,
	.target_reset = esp8266_spi_arduino_unowifiv8_target_reset,
	.target_run = esp8266_spi_arduino_unowifiv8_target_run,
	.poll_idle = esp8266_spi_poll_idle,
	.done = esp8266_spi_arduino_unowifiv8_done,
};
