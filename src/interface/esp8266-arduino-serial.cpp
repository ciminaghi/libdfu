
#include "dfu.h"
#include "dfu-internal.h"
#include "esp8266-serial.h"
#include "HardwareSerial.h"

/*
 * esp8266 serial interface, this file is part of the arduino dfu library,
 * do not build it outside the arduino ide.
 */

/*
 * FIXME: currently ignores path and works with serial0 only
 */
int esp8266_serial_open(struct dfu_interface *iface,
			const char *path, const void *pars)
{
	Serial.begin(115200);
	/* Non blocking calls */
	Serial.setTimeout(0);
	return 0;
}

int esp8266_serial_write(struct dfu_interface *iface,
				 const char *buf, unsigned long size)
{
	return Serial.write(buf, size);
}

int esp8266_serial_read(struct dfu_interface *iface, char *buf,
			unsigned long size)
{
	return Serial.readBytes(buf, size);
}

int esp8266_serial_poll_idle(struct dfu_interface *iface)
{
	return Serial.available() ? DFU_INTERFACE_EVENT : 0;
}
