
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-serial.h"

#include "Arduino.h"

/*
 * Arduino star otto board.
 * ESP8266 CONSOLE: Serial1
 * STM32 PROGRAMMING SERIAL: Serial
 * GPIO4  -> BOOT0
 * GPIO12 -> #RST
 *
 * THIS FILE IS FOR THE ARDUINO VERSION OF THE LIBRARY, DO NOT TRY TO BUILD IT
 * OUTSIDE THE ARDUINO ENVIRONMENT
 */

static int esp8266_serial_star8_open(struct dfu_interface *iface,
				     const char *path,
				     const void *pars)
{
	static const struct dfu_serial_pars _pars = {
		.parity = PARITY_EVEN,
	};

	return esp8266_serial_open(iface, path, pars ? pars : &_pars);
}

static int
esp8266_serial_star8_target_reset(struct dfu_interface *iface)
{

	/*
	 * BOOT0               +--------
	 *                     |
	 *        -------------+
	 *
	 * RST    ----+            +----
	 *            |            |
	 *            +------------+
	 */
	pinMode(4, OUTPUT);
	pinMode(12, OUTPUT);
	delay(5);
	digitalWrite(4, 0);
	digitalWrite(12, 0);
	delay(2);
	digitalWrite(12, 1);
	delay(5);
	digitalWrite(4, 1);
	delay(10);
	digitalWrite(12, 0);
	delay(200);
	return 0;
}

static int
esp8266_serial_star8_target_run(struct dfu_interface *iface)
{
	pinMode(4, OUTPUT);
	pinMode(12, OUTPUT);
	digitalWrite(4, 0);
	digitalWrite(12, 0);
	delay(1);
	digitalWrite(12, 1);
	delay(1);
	digitalWrite(12, 0);
	return 0;
}

const struct dfu_interface_ops
esp8266_serial_star8_interface_ops = {
	.open = esp8266_serial_star8_open,
	.write = esp8266_serial_write,
	.read = esp8266_serial_read,
	.write_read = NULL,
	.target_reset = esp8266_serial_star8_target_reset,
	.target_run = esp8266_serial_star8_target_run,
	.poll_idle = esp8266_serial_poll_idle,
	.done = NULL,
	.fini = esp8266_serial_fini,
};
