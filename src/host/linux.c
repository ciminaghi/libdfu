#include <stdlib.h>
#include <poll.h>
#include <time.h>
#include <errno.h>
#include "dfu.h"
#include "dfu-internal.h"


struct linux_host_data {
	struct linux_event_data interface_event_data;
	struct linux_event_data file_event_data;
};

static struct linux_host_data lhd = {
	.interface_event_data = {
		.fd = -1,
	},
	.file_event_data = {
		.fd = -1,
	},
};

static int linux_init(struct dfu_host *host)
{
	host->priv = &lhd;
	return 0;
}

void linux_udelay(struct dfu_host *host, unsigned long us)
{
	struct timespec tv;

	for (tv.tv_sec = 0; us > 1000000; tv.tv_sec++, us -= 1000000);
	tv.tv_nsec = us * 1000;
	nanosleep(&tv, &tv);
}

int linux_idle(struct dfu_host *host, int timeout_ms)
{
	int stat, ret = 0, nfds;
	struct linux_host_data *data = host->priv;
	struct pollfd *ptr;
	struct pollfd pfd[2] = {
		{
			.fd = data->interface_event_data.fd,
			.events = data->interface_event_data.events,
			.revents = 0,
		},
		{
			.fd = data->file_event_data.fd,
			.events = data->interface_event_data.events,
			.revents = 0,
		}
	};

	if (pfd[0].fd >= 0 && pfd[1].fd >= 0) {
		nfds = 2;
		ptr = pfd;
	} else if (pfd[0].fd < 0 && pfd[1].fd >= 0) {
		nfds = 1;
		ptr = &pfd[1];
	} else if (pfd[0].fd >= 0 && pfd[1].fd < 0) {
		nfds = 1;
		ptr = pfd;
	} else {
		nfds = 0;
		ptr = NULL;
	}
	if (!nfds && timeout_ms < 0)
		/* No timeout & no events, just return with 0 */
		return ret;
	stat = poll(ptr, nfds, timeout_ms);
	switch(stat) {
	case 0:
		return DFU_TIMEOUT;
	case -1:
		if (errno == EINTR)
			/* Interrupted */
			break;
		perror("poll");
		return stat;
	default:
		if (pfd[0].revents)
			ret |= DFU_INTERFACE_EVENT;
		if (pfd[1].revents)
			ret |= DFU_FILE_EVENT;
		return ret;
	}
	/* NEVER REACHED */
	return -1;
}

int linux_set_interface_event(struct dfu_host *host, void *linux_evt_info)
{
	struct linux_host_data *data = host->priv;

	data->interface_event_data =
		*(struct linux_event_data *)linux_evt_info;
	return 0;
}

int linux_set_binary_file_event(struct dfu_host *host, void *linux_evt_info)
{
	struct linux_host_data *data = host->priv;

	data->file_event_data =
		*(struct linux_event_data *)linux_evt_info;
	return 0;
}

unsigned long linux_get_current_time(struct dfu_host *host)
{
	int stat;
	struct timespec ts;

	stat = clock_gettime(CLOCK_MONOTONIC, &ts);
	if (stat < 0)
		return 0xffffffff;
	return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

const struct dfu_host_ops linux_dfu_host_ops = {
	.init = linux_init,
	.udelay = linux_udelay,
	.idle = linux_idle,
	.set_interface_event = linux_set_interface_event,
	.set_binary_file_event = linux_set_binary_file_event,
	.get_current_time = linux_get_current_time,
};
