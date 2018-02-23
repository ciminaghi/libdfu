
/*
 * libdfu, usage sample: programming the nrf52 on an arduino primo via lwip
 * and web interface. This is a test program for the simple single purpose
 * webserver in src/rx-method-http-lwip.c
 *
 * Author Davide Ciminaghi, 2016
 * Public domain
 */
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dfu.h>
#include <dfu-esp8266.h>
#include <dfu-nordic-spi.h>

#define USER_PRIO	0
#define USER_QUEUE_LEN	1

os_event_t user_proc_task_queue[USER_QUEUE_LEN];

/*
 * Temporarily here
 */
int flash_first_sector = 0x3c;

int sectsize = SPI_FLASH_SEC_SIZE;

struct spi_flash_sector sectors_start[128];
struct spi_flash_sector sectors_end[0];

/*
 * mmmmh, don't like this, but I don't know how to pass pointers to
 * user's event handlers
 */
static struct dfu_data *dfu;

/* nonstatic debug cmd option, exported in lwipopts.h */
unsigned char debug_flags;

static void ICACHE_FLASH_ATTR
loop(os_event_t *events)
{
	int stat;
	static int first = 0;

	if (!first) {
		/* Reset and probe target */
		if (dfu_target_reset(dfu) < 0) {
			dfu_err("Error resetting target\n");
			goto end;
		}
		dfu_log("dfu_target_reset() OK\n");
		if (dfu_target_probe(dfu) < 0) {
			dfu_err("Error probing target\n");
			goto end;
		}
		dfu_log("dfu_target_probe() OK\n");
		first = 1;
	end:
		system_os_post(USER_PRIO, 0, 0 );
		return;
	}
	
	stat = dfu_idle(dfu);
	switch(stat) {
	case DFU_CONTINUE:
		system_os_post(USER_PRIO, 0, 0 );
		break;
	case DFU_ERROR:
		dfu_err("Error programming\n");
		break;
	case DFU_ALL_DONE:
		dfu_log("Programming done\n");
		/* Let target run */
		dfu_target_go(dfu);
		break;
	default:
		dfu_err("Invalid return value %d from dfu_idle\n", stat);
		break;
	}
}

void ICACHE_FLASH_ATTR
user_init(void)
{
	struct rx_method_http_lwip_data md;
	struct dfu_binary_file *f;
	char ssid[32] = SSID;
	char password[64] = SSID_PASSWORD;
	struct station_config cfg;


	dfu = dfu_init(&esp8266_spi_arduinoprimo_interface_ops,
		       NULL,
		       NULL,
		       /* No interface start cb */
		       NULL,
		       NULL,
		       &nordic_spi_dfu_target_ops,
		       NULL,
		       &esp8266_dfu_host_ops,
		       &spi_flash_fc_ops);
	if (!dfu) {
		os_printf("Error initializing libdfu\n");
		return;
	}

	f = dfu_binary_file_start_rx(&dfu_rx_method_http, dfu, &md);
	if (!f) {
		dfu_err("Error setting up binary file struct\n");
		return;
	}
	if (dfu_binary_file_flush_start(f) < 0)
		return;

	wifi_set_opmode( 0x1 );
	os_memcpy(&cfg.ssid, ssid, 32);
	os_memcpy(&cfg.password, password, 64);
	wifi_station_set_config(&cfg);

	/* Start dfu task */
	system_os_task(loop, USER_PRIO, user_proc_task_queue, USER_QUEUE_LEN);
	system_os_post(USER_PRIO, 0, 0 );
}
