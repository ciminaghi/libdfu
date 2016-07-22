
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
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
	iface->priv = &sdata;
	sdata.fd = open(path, O_RDWR);
	if (sdata.fd < 0)
		return sdata.fd;
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

	return read(priv->fd, buf, size);
}

/* FIXME: do hw reset for target (maybe gpio ?) */
int linux_serial_target_reset(struct dfu_interface *iface)
{
	return 0;
}


const struct dfu_interface_ops linux_serial_interface_ops = {
	.open = linux_serial_open,
	.write = linux_serial_write,
	.read = linux_serial_read,
	.target_reset = linux_serial_target_reset,
};

