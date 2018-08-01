
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "gpio.h"
#include "esp8266-spi.h"

#include "Arduino.h"

#undef NRF52_DEBUG

//
// Arduino primo interface. Uses GPIO 4 for target reset, UART0 tx as a
// serial console.
// ESP_GPIO15 -> CS
// ESP_GPIO12 -> MOSI
// ESP_GPIO13 -> MISO
// ESP_GPIO14 -> CLK
//

static inline void _reset_activate(void)
{
	digitalWrite(4, 1);
}

static inline void _reset_deactivate(void)
{
	digitalWrite(4, 0);
}

static inline void _cs_activate(void)
{
	digitalWrite(15, 0);
}

static inline void _cs_deactivate(void)
{
	digitalWrite(15, 1);
}

#ifndef NRF52_DEBUG
static inline void _do_reset(void)
{
	dfu_dbg("REAL target reset\n");
	_cs_activate();
	delay(10);
	_reset_activate();
	delay(5);
	_reset_deactivate();
	delay(200);
	_cs_deactivate();
	delay(200);
}
#else
static inline void _do_reset(void)
{
	dfu_dbg("FAKE target reset\n");
}
#endif

//
// Gpio4 is target's reset: we enter the bootloader by
// resetting the nrf52 with spi cs (GPIO15) LOW.
//
int esp8266_spi_arduinoprimo_target_reset(struct dfu_interface *iface)
{
	pinMode(4, OUTPUT);
	pinMode(15, OUTPUT);
	_do_reset();
	return 0;
}

// The bootloader does a self-reset, no need to do anything
static int
esp8266_spi_arduinoprimo_target_run(struct dfu_interface *iface)
{
	return 0;
}

static int esp8266_spi_arduinoprimo_write(struct dfu_interface *iface,
					  const char *out_buf,
					  unsigned long size)
{
	int ret;

	_cs_activate();
	delayMicroseconds(10);
	ret = esp8266_spi_write(iface, out_buf, size);
	_cs_deactivate();
	return ret;
}

//
// Activate cs, do a write-read and deactivate cs
// Proper write-read doesn't actually work on esp8266 hspi (but mysteriously
// works with avrisp), so let's just __read__ and ignore the output buffer
//
static int esp8266_spi_arduinoprimo_write_read(struct dfu_interface *iface,
					       const char *out_buf,
					       char *in_buf,
					       unsigned long size)
{
	int ret;

	_cs_activate();
	delayMicroseconds(10);
	ret = esp8266_spi_read(iface, in_buf, size);
	_cs_deactivate();
	return ret;
}

const struct dfu_interface_ops
esp8266_spi_arduinoprimo_interface_ops = {
	// open
	esp8266_spi_open,
	// write
	esp8266_spi_arduinoprimo_write,
	// read
	NULL,
	// write_read
	.write_read = esp8266_spi_arduinoprimo_write_read,
	// target_reset
	.target_reset = esp8266_spi_arduinoprimo_target_reset,
	// target_run
	.target_run = esp8266_spi_arduinoprimo_target_run,
	// poll_idle
	.poll_idle = esp8266_spi_poll_idle,
	// done
	NULL,
	// fini,
	NULL,
};
