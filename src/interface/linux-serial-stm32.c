/*
 * stm32 specific functions for linux serial interface
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

static int linux_serial_stm32_target_reset(struct dfu_interface *iface)
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


const struct dfu_interface_ops linux_serial_stm32_interface_ops = {
	.open = linux_serial_open,
	.write = linux_serial_write,
	.read = linux_serial_read,
	.target_reset = linux_serial_stm32_target_reset,
};
