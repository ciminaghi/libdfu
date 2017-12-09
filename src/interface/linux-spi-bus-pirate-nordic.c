/*
 * linux SPI to nordic (arduino primo) via bus pirate interface
 * LGPL v2.1
 * Copyright WhatsNext S.r.l. 2017
 * Author Davide Ciminaghi
 */
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <stdio.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "linux-serial.h"
#include "linux-spi-bus-pirate.h"

static int linux_spi_bp_nordic_target_reset(struct dfu_interface *iface)
{
	/* Dummy target reset for the moment */
	return 0;
}

const struct dfu_interface_ops linux_spi_bp_nordic_target_interface_ops = {
	.open = linux_spi_bp_open,
	.write = linux_spi_bp_write,
	.write_read = linux_spi_bp_write_read,
	.target_reset = linux_spi_bp_nordic_target_reset,
	.fini = linux_spi_bp_fini,
};
