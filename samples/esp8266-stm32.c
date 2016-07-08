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

#define UART0 0

#define USER_PRIO	0
#define USER_QUEUE_LEN	1

os_event_t user_proc_task_queue[USER_QUEUE_LEN];
static void loop(os_event_t *events);


static void ICACHE_FLASH_ATTR
loop(os_event_t *events)
{
	static unsigned long i = 0;

	os_printf("Hello %lu\n\r", i++);
	os_delay_us(1000000);
	system_os_post(USER_PRIO, 0, 0 );
}

/* Init function */
void ICACHE_FLASH_ATTR
user_init()
{
	uart_div_modify(UART0, UART_CLK_FREQ / 115200);

	/* Start os task */
	system_os_task(loop, USER_PRIO, user_proc_task_queue, USER_QUEUE_LEN);
	system_os_post(USER_PRIO, 0, 0 );
}

