/*
 * libdfu, usage sample (programming the stm32 via serial port under linux)
 * Author Davide Ciminaghi, 2016
 * Public domain
 */
#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_interface.h"
#include "espmissingincludes.h"
#include <dfu.h>
#include <dfu-esp8266.h>
#include <dfu-stm32.h>

#define UART0 0

#define USER_PRIO	0
#define USER_QUEUE_LEN	1

os_event_t user_proc_task_queue[USER_QUEUE_LEN];
static void loop(os_event_t *events);

/*
 * mmmmh, don't like this, but I don't know how to pass pointers to
 * user's event handlers
 */
static struct dfu_data *dfu;
static struct dfu_binary_file *bfile;

static void ICACHE_FLASH_ATTR
loop(os_event_t *events)
{
	if (dfu_binary_file_written(bfile)) {
		/* File was written, start target and die */
		dfu_target_go(dfu);
		return;
	}
	dfu_idle(dfu);
	system_os_post(USER_PRIO, 0, 0 );
}

/* Init function */
void ICACHE_FLASH_ATTR
user_init()
{
	uart_div_modify(UART0, UART_CLK_FREQ / 115200);

	dfu = dfu_init(&esp8266_serial_interface_ops, "", NULL,
		       &stm32_dfu_target_ops,
		       &esp8266_dfu_host_ops);

	if (!dfu) {
		os_printf("Error setting up dfu\n");
		return;
	}
	if (dfu_target_reset(dfu) < 0) {
		os_printf("Error resetting target\n");
		return;
	}
	bfile = dfu_binary_file_start_rx("http_post", dfu);
	if (!bfile) {
		os_printf("Error starting file rx\n");
		return;
	}
	/* Start dfu task */
	system_os_task(loop, USER_PRIO, user_proc_task_queue, USER_QUEUE_LEN);
	system_os_post(USER_PRIO, 0, 0 );
}

