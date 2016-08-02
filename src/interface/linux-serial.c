
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "dfu.h"
#include "dfu-internal.h"

/*
 * Linux serial interface private data
 */
struct linux_serial_data {
	int fd;
};

/* Just one instance */
static struct linux_serial_data sdata;

static int linux_serial_open(struct dfu_interface *iface,
			     const char *path, const void *pars)
{
	struct termios config;

	iface->priv = &sdata;
	sdata.fd = open(path, O_RDWR | O_NOCTTY);
	if (sdata.fd < 0)
		return sdata.fd;
	/* FIXME: USE pars FOR SERIAL PORT CONFIGURATION ? */
	if (tcgetattr(sdata.fd, &config) < 0) {
		dfu_err("Error reading termios config\n");
		return -1;
	}
	config.c_cflag |= PARENB;
	config.c_cflag |= CS8;
	if(cfsetispeed(&config, B115200) < 0) {
		dfu_err("Error setting serial port speed\n");
		return -1;
	}
	if (tcsetattr(sdata.fd, TCSAFLUSH, &config) < 0) {
		dfu_err("Error setting termios config\n");
		return -1;
	}
	return 0;
}


int linux_serial_write(struct dfu_interface *iface,
		       const char *buf, unsigned long size)
{
	struct linux_serial_data *priv = iface->priv;

	return write(priv->fd, buf, size);
}


int linux_serial_read(struct dfu_interface *iface, char *buf,
		      unsigned long size)
{
	struct linux_serial_data *priv = iface->priv;
	struct pollfd pfd = {
		.fd = priv->fd,
		.events = POLLIN,
		.revents = 0,
	};
	int stat;

	/* FIXME: make timeout configurable at runtime */
	stat = poll(&pfd, 1, 1000);
	if (stat < 0) {
		dfu_err("%s: %s\n", __func__, strerror(errno));
		return -1;
	}
	if (!stat) {
		dfu_err("Timeout waiting for serial read\n");
		return -1;
	}
	if (stat != 1) {
		dfu_err("%s: unexpected return value from poll()\n", __func__);
		return -1;
	}
	return read(priv->fd, buf, size);
}

/* FIXME: THIS IS STM32 SPECIFIC, IMPLEMENT AN STM32 SPECIFIC INTERFACE */
int linux_serial_target_reset(struct dfu_interface *iface)
{
	struct linux_serial_data *priv = iface->priv;
	int v, stat;

	/* RTS -> BOOT0 */
	/* DTR -> RST */
	/* Set RST to 0 (active) */
	v = TIOCM_RTS;
	stat = ioctl(priv->fd, TIOCMBIS, &v);
	if (stat < 0) {
		dfu_err("error resetting target (%s)\n", strerror(errno));
		return stat;
	}
	v = TIOCM_RTS;
	stat = ioctl(priv->fd, TIOCMBIC, &v);
	if (stat < 0) {
		dfu_err("error resetting target (%s)\n", strerror(errno));
		return stat;
	}
	v = TIOCM_DTR;
	stat = ioctl(priv->fd, TIOCMBIC, &v);
	if (stat < 0) {
		dfu_err("error resetting target (%s)\n", strerror(errno));
		return stat;
	}
	/* Wait 50ms */
	poll(NULL, 0, 200);
	return 0;
}


const struct dfu_interface_ops linux_serial_interface_ops = {
	.open = linux_serial_open,
	.write = linux_serial_write,
	.read = linux_serial_read,
	.target_reset = linux_serial_target_reset,
};

