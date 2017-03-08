
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-serial.h"

#include "Arduino.h"

/*
 * Hacked vanilla arduinouno with homemade 8266 shield: uses UART0 and GPIO5
 * for target reset, UART1 tx as a serial console.
 * This configuration is used for 8266 + stk500 debugging
 * THIS FILE IS FOR THE ARDUINO VERSION OF THE LIBRARY, DO NOT TRY TO BUILD IT
 * OUTSIDE THE ARDUINO ENVIRONMENT
 */

static int
esp8266_serial_arduinouno_hacked_target_reset(struct dfu_interface *iface)
{
	pinMode(5, OUTPUT);
	digitalWrite(5, 0);
	delay(1);
	digitalWrite(5, 1);
	delay(200);
	return 0;
}

const struct dfu_interface_ops
esp8266_serial_arduinouno_hacked_interface_ops = {
	.open = esp8266_serial_open,
	.write = esp8266_serial_write,
	.read = esp8266_serial_read,
	.write_read = NULL,
	.target_reset = esp8266_serial_arduinouno_hacked_target_reset,
	.target_run = NULL,
	.poll_idle = esp8266_serial_poll_idle,
	.done = NULL,
	.fini = esp8266_serial_fini,
};
