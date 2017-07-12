
#include <stdarg.h>
#include "dfu.h"

#include "Arduino.h"
#include "HardwareSerial.h"

static HardwareSerial log_port = Serial1;

void dfu_log_set_serial(HardwareSerial& s)
{
  log_port = s;
}

extern "C" {

/* log function, comes from Print::printf() */
int _dfu_log(const char *format, ...)
{
	va_list arg;
	va_start(arg, format);
	char temp[64];
	char* buffer = temp;
	size_t len = vsnprintf(temp, sizeof(temp), format, arg);
	va_end(arg);
	if (len > sizeof(temp) - 1) {
		buffer = new char[len + 1];
		if (!buffer) {
			return 0;
		}
		va_start(arg, format);
		vsnprintf(buffer, len + 1, format, arg);
		va_end(arg);
	}
	len = log_port.write((const uint8_t*) buffer, len);
	if (buffer[len - 1] == '\n')
		log_port.write('\r');
	if (buffer != temp) {
		delete[] buffer;
	}
	return len;
}

}
