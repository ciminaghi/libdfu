
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "esp8266-serial.h"

int esp8266_serial_star8_target_reset(struct dfu_interface *iface)
{
	return -1;
}


const struct dfu_interface_ops esp8266_serial_star8_interface_ops = {
	.open = esp8266_serial_open,
	.write = esp8266_serial_write,
	.read = esp8266_serial_read,
	.target_reset = esp8266_serial_star8_target_reset,
};

