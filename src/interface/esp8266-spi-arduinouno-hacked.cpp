
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-spi.h"

#include "Arduino.h"

/*
 * Hacked vanilla arduinouno with homemade 8266 shield: uses SPI1 and GPIO5
 * for target reset, UART1tx as a serial console.
 * This configuration is used for 8266 + avrisp debugging
 * THIS FILE IS FOR THE ARDUINO VERSION OF THE LIBRARY, DO NOT TRY TO BUILD IT
 * OUTSIDE THE ARDUINO ENVIRONMENT
 */

static int esp8266_arduinouno_hacked_spi_open(struct dfu_interface *iface,
					      const char *path,
					      const void *pars)
{
	pinMode(5, OUTPUT);
	pinMode(SCK, INPUT);
	digitalWrite(5, 1);
	return esp8266_spi_open(iface, path, pars);
}

static int
esp8266_spi_arduinouno_hacked_target_reset(struct dfu_interface *iface)
{
	digitalWrite(5, 0);
	delay(2);
	pinMode(SCK, OUTPUT);
	digitalWrite(SCK, 0);
	delay(2);
	digitalWrite(5, 1);
	delay(2);
	digitalWrite(5, 0);
	delay(2);
	pinMode(SCK, SPECIAL);
	return 0;
}

static int
esp8266_spi_arduinouno_hacked_target_run(struct dfu_interface *iface)
{
	/* Release reset on done and let the target run */
	digitalWrite(5, 1);
	delay(2);
	digitalWrite(5, 0);
	delay(2);
	digitalWrite(5, 1);
	return 0;
}

const struct dfu_interface_ops
esp8266_spi_arduinouno_hacked_interface_ops = {
	esp8266_arduinouno_hacked_spi_open,
	esp8266_spi_write,
	NULL,
	esp8266_spi_write_read,
	esp8266_spi_arduinouno_hacked_target_reset,
	esp8266_spi_arduinouno_hacked_target_run,
	esp8266_spi_poll_idle,
};
