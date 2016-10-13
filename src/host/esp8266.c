
#include <stdlib.h>
#include "dfu.h"
#include "dfu-esp8266.h"
#include "dfu-internal.h"
#include "user_interface.h"

volatile uint32_t *regs = 0;

static int esp8266_init(struct dfu_host *host)
{
	uart_div_modify(0, UART_CLK_FREQ / 115200);
	dfu_dbg("%s\n", __func__);
	return 0;
}

void esp8266_udelay(struct dfu_host *host, unsigned long us)
{
	os_delay_us(us);
}

int esp8266_idle(struct dfu_host *host, long next_timeout)
{
	return 0;
}

unsigned long esp8266_get_current_time(struct dfu_host *host)
{
	return system_get_time() / 1000;
}

const struct dfu_host_ops esp8266_dfu_host_ops = {
	.init = esp8266_init,
	.udelay = esp8266_udelay,
	.idle = esp8266_idle,
	.get_current_time = esp8266_get_current_time,
};
