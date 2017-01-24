/*
 * interface operations for arduino uno.
 */

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
#include "linux-serial.h"

static int linux_serial_arduino_uno_open(struct dfu_interface *iface,
					 const char *path, const void *pars)
{
	struct termios config;
	struct linux_serial_data *sdata;

	if (linux_serial_open(iface, path, pars) < 0)
		return -1;
	sdata = iface->priv;

	/* FIXME: USE pars FOR SERIAL PORT CONFIGURATION ? */
	if (tcgetattr(sdata->fd, &config) < 0) {
		dfu_err("Error reading termios config\n");
		return -1;
	}
	config.c_iflag = IGNBRK;
	config.c_oflag = 0;
	config.c_lflag = 0;
	config.c_cflag = (CS8 | CREAD | CLOCAL);
	config.c_cc[VMIN]  = 1;
	config.c_cc[VTIME] = 0;
	if (cfsetispeed(&config, B115200) < 0 ||
	    cfsetospeed(&config, B115200) < 0) {
		dfu_err("Error setting serialx port speed\n");
		return -1;
	}
	if (tcsetattr(sdata->fd, TCSANOW, &config) < 0) {
		dfu_err("%s: tcsetattr failed\n", __func__);
		return -1;
	}
	return 0;
}

static int linux_serial_arduino_uno_target_reset(struct dfu_interface *iface)
{
	struct linux_serial_data *priv = iface->priv;
	int v, stat;

	/*
	 * RST    ----+            +----
	 *            |            |
	 *            +------------+
	 */
	/* DTR -> RST */
	/* Set RST to 0 (active) */
	v = TIOCM_DTR;
	stat = ioctl(priv->fd, TIOCMBIS, &v);
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


const struct dfu_interface_ops linux_serial_arduino_uno_interface_ops = {
	.open = linux_serial_arduino_uno_open,
	.write = linux_serial_write,
	.read = linux_serial_read,
	.target_reset = linux_serial_arduino_uno_target_reset,
	.fini = linux_serial_fini,
};
