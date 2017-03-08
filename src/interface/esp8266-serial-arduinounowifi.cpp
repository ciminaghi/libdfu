
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-serial.h"

#include "Arduino.h"

/*
 * Arduino unowifi: uses UART0, GPIO4 for direct esp8266 <-> atmega328 uart
 * connection, GPIO12 for target reset, UART1 tx as a serial console.
 * THIS FILE IS FOR THE ARDUINO VERSION OF THE LIBRARY, DO NOT TRY TO BUILD IT
 * OUTSIDE THE ARDUINO ENVIRONMENT
 *
 * IMPORTANT NOTICE: this code only works for boards with hw revisions 3 and 4.
 * It does __not__ work for V2 boards.
 *
 * Original code by Sebastiano Longo <sebba@arduino.org> and
 * Francesco Alessi <francesco@arduino.org>
 */

static int
esp8266_serial_arduino_unowifi_target_reset(struct dfu_interface *iface)
{
	pinMode(4, OUTPUT);
	digitalWrite(4, 0);
	delay(100);
	pinMode(12, OUTPUT);
	digitalWrite(12, 0);
	delay(1);
	digitalWrite(12, 1);
	delay(200);
	return 0;
}

static void esp8266_serial_arduino_unowifi_done(struct dfu_interface *iface)
{
	digitalWrite(4, 1);
}

const struct dfu_interface_ops
esp8266_serial_arduino_unowifi_interface_ops = {
	.open = esp8266_serial_open,
	.write = esp8266_serial_write,
	.read = esp8266_serial_read,
	.write_read = NULL,
	.target_reset = esp8266_serial_arduino_unowifi_target_reset,
	.target_run = NULL,
	.poll_idle = esp8266_serial_poll_idle,
	.done = esp8266_serial_arduino_unowifi_done,
	.fini = esp8266_serial_fini,
};
