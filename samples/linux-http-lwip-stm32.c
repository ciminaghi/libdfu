/*
 * libdfu, usage sample: programming the stm32 via lwip and web interface
 * under linux. This is a test program for the simple single purpose webserver
 * in src/rx-method-http-lwip.c
 *
 * Author Davide Ciminaghi, 2016
 * Public domain
 */
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <dfu.h>
#include <dfu-linux.h>
#include <dfu-stm32.h>

#include "lwip/opt.h"
#include "lwip/init.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/sys.h"
#include "lwip/tcpip.h"
#include "lwip/tcp_impl.h"
#include "lwip/ip_addr.h"
#include "lwip/ip_frag.h"
#include "lwip/inet.h"
#include "timer.h"
#include "mintapif.h"
#include "arch/perf.h"
#include "netif/etharp.h"

#include "lwip/icmp.h"
#include "lwip/raw.h"
#include "lwip/stats.h"

struct netif netif;
struct stats_ lwip_stats;

/* nonstatic debug cmd option, exported in lwipopts.h */
unsigned char debug_flags;

/* (manual) host IP configuration */
static ip_addr_t ipaddr, netmask, gw;

static void lwip_idle(struct netif *netif)
{
	sigset_t mask, oldmask, empty;

	/*
	 * poll for input packet and ensure
         * select() or read() arn't interrupted
	 */
	sigemptyset(&mask);
	sigaddset(&mask, SIGALRM);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);

	/*
	 * start of critical section,
         * poll netif, pass packet to lwIP
	 */
	if (mintapif_select(netif) > 0) {
		printf("ciccio\n");
		/*
		 * work, immediatly end critical section
		 * hoping lwIP ended quickly ...
		 */
		sigprocmask(SIG_SETMASK, &oldmask, NULL);
	} else {
		printf("pasticcio\n");
		/* no work, wait a little (10 msec) for SIGALRM */
		sigemptyset(&empty);
		sigsuspend(&empty);
		/* ... end critical section */
		sigprocmask(SIG_SETMASK, &oldmask, NULL);
	}
	if(timer_testclr_evt(TIMER_EVT_TCPTMR))
		tcp_tmr();
#if IP_REASSEMBLY
	if(timer_testclr_evt(TIMER_EVT_IPREASSTMR))
		ip_reass_tmr();
#endif
	if(timer_testclr_evt(TIMER_EVT_ETHARPTMR))
		etharp_tmr();
}

static void help(int argc, char *argv[])
{
	fprintf(stderr, "Use %s serial_port_path\n", argv[0]);
}

int main(int argc, char *argv[])
{
	char *port;
	int ret;
	struct dfu_data *dfu;
	struct dfu_binary_file *f;
	struct in_addr inaddr;
	char ip_str[16] = {0}, nm_str[16] = {0}, gw_str[16] = {0};
	struct rx_method_http_lwip_data md;

	if (argc < 2) {
		help(argc, argv);
		exit(127);
	}
	port = argv[1];

	dfu = dfu_init(&linux_serial_stm32_interface_ops,
		       port,
		       NULL,
		       &stm32_dfu_target_ops,
		       NULL,
		       &linux_dfu_host_ops,
		       NULL);
	if (!dfu) {
		fprintf(stderr, "Error initializing libdfu\n");
		exit(127);
	}

	IP4_ADDR(&gw, 192,168,1,1);
	IP4_ADDR(&netmask, 255,255,255,0);
	IP4_ADDR(&ipaddr, 192,168,1,2);
	debug_flags = (LWIP_DBG_ON|LWIP_DBG_TRACE|LWIP_DBG_STATE|
		       LWIP_DBG_FRESH|LWIP_DBG_HALT);
	inaddr.s_addr = ipaddr.addr;
	strncpy(ip_str,inet_ntoa(inaddr),sizeof(ip_str));
	inaddr.s_addr = netmask.addr;
	strncpy(nm_str,inet_ntoa(inaddr),sizeof(nm_str));
	inaddr.s_addr = gw.addr;
	strncpy(gw_str,inet_ntoa(inaddr),sizeof(gw_str));
	printf("Host at %s mask %s gateway %s\n", ip_str, nm_str, gw_str);

	lwip_init();
	fprintf(stderr, "TCP/IP initialized.\n");
	netif_add(&netif, &ipaddr, &netmask, &gw, NULL, mintapif_init,
		  ethernet_input);
	netif_set_default(&netif);
	netif_set_up(&netif);

	md.netif_idle_fun = lwip_idle;
	md.netif = &netif;
	f = dfu_binary_file_start_rx(&dfu_rx_method_http, dfu, &md);
	if (!f) {
		fprintf(stderr, "Error setting up binary file struct\n");
		exit(127);
	}
	if (dfu_binary_file_flush_start(f) < 0)
		exit(127);
	timer_init();
	timer_set_interval(TIMER_EVT_ETHARPTMR, ARP_TMR_INTERVAL / 10);
	timer_set_interval(TIMER_EVT_TCPTMR, TCP_TMR_INTERVAL / 10);
#if IP_REASSEMBLY
	timer_set_interval(TIMER_EVT_IPREASSTMR, IP_TMR_INTERVAL / 10);
#endif

	/* Reset and probe target */
	if (dfu_target_reset(dfu) < 0) {
		fprintf(stderr, "Error resetting target\n");
		exit(127);
	}
	if (dfu_target_probe(dfu) < 0) {
		fprintf(stderr, "Error probing target\n");
		exit(127);
	}
	if (dfu_target_erase_all(dfu) < 0) {
		fprintf(stderr, "Error erasing target memory\n");
		exit(127);
	}

	/* Loop around waiting for events */
	do {
		lwip_idle(&netif);
		ret = dfu_idle(dfu);
		switch (ret) {
		case DFU_ERROR:
			fprintf(stderr, "Error programming file\n");
			break;
		case DFU_ALL_DONE:
			fprintf(stderr, "Programming DONE\n");
			break;
		case DFU_CONTINUE:
			break;
		default:
			fprintf(stderr,
				"Invalid ret value %d from dfu_idle()\n", ret);
			break;
		}
	} while(ret == DFU_CONTINUE);
	/* Let target run */
	exit(dfu_target_go(dfu));
}
