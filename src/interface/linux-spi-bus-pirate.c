/*
 * linux SPI via bus pirate interface
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

struct linux_spi_bp_data {
	int fd;
	int cs_active_state;
};

/* Just one instance */
static struct linux_spi_bp_data _data;

static int get_reply(struct linux_spi_bp_data *data, uint8_t *buf, int len,
		     unsigned long timeout_ms)
{
	struct pollfd pfd;
	int ret;

	pfd.fd = data->fd;
	pfd.events = POLLIN;
	switch (poll(&pfd, 1, timeout_ms)) {
	case 0:
		dfu_dbg("%s: timeout\n", __func__);
		return 0;
	case -1:
		dfu_err("%s: poll(): %s\n", __func__, strerror(errno));
		return -1;
	default:
		ret = read(data->fd, buf, len);
		if (ret < 0) {
			dfu_err("%s: read(): %s\n", __func__, strerror(errno));
			return -1;
		}
		return ret;
	}
	return -1;
}

static int spi_bus_pirate_config(struct linux_spi_bp_data *data,
				 int cs_active_state)
{
	uint8_t cmd;
	uint8_t reply;

	data->cs_active_state = cs_active_state;

	/* Set speed = 125KHz */
	cmd = 0x60;
	if (write(data->fd, &cmd, 1) < 0) {
		dfu_err("%s: write(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (get_reply(data, &reply, 1, 1000UL) <= 0)
		return -1;
	if (reply != 0x1) {
		dfu_err("%s: error setting spi frequency\n", __func__);
		return -1;
	}
	/* 3.3V on, cpol = cphase = 0 */
	cmd = 0x88;
	if (write(data->fd, &cmd, 1) < 0) {
		dfu_err("%s: write(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (get_reply(data, &reply, 1, 1000UL) <= 0)
		return -1;
	if (reply != 0x1) {
		dfu_err("%s: error setting spi frequency\n", __func__);
		return -1;
	}
	/* Set power and cs */
	cmd = 0x4c;
	if (write(data->fd, &cmd, 1) < 0) {
		dfu_err("%s: write(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (get_reply(data, &reply, 1, 1000UL) <= 0)
		return -1;
	if (reply != 0x1) {
		dfu_err("%s: error setting spi power and cs\n", __func__);
		return -1;
	}
	return 0;
}

static int do_cs(struct linux_spi_bp_data *priv, int assert)
{
	uint8_t cmd, reply;

	cmd = assert ? (priv->cs_active_state ? 3 : 2) :
		(priv->cs_active_state ? 2 : 3);
	if (write(priv->fd, &cmd, 1) < 0) {
		dfu_err("%s: write(): %s\n", __func__, strerror(errno));
		return -1;
	}
	if (get_reply(priv, &reply, 1, 1000UL) <= 0)
		return -1;
	if (reply != 0x1) {
		dfu_err("%s: error setting spi power and cs\n", __func__);
		return -1;
	}
	return 0;
}

static int cs_assert(struct linux_spi_bp_data *priv)
{
	return do_cs(priv, 1);
}

static int cs_deassert(struct linux_spi_bp_data *priv)
{
	return do_cs(priv, 0);
}

static int do_write(int fd, const char *buf, unsigned long sz)
{
	int stat, sent;

	for (sent = 0; sent < sz; sent += stat) {
		/*
		 * WORK AROUND APPARENT BUS PIRATE OVERRUN FOR LENGTH >= 8:
		 * WRITE 1 BYTE A TIME !
		 */
		stat = write(fd, &buf[sent], 1);
		usleep(200);
		if (stat < 0) {
			dfu_err("%s, write: %s\n", __func__, strerror(errno));
			return stat;
		}
	}
	return sent;
}

static int do_read(int fd, char *buf, unsigned long sz)
{
	int stat, done;
	struct pollfd pfd;

	for (done = 0; done < sz; done += stat) {
		pfd.fd = fd;
		pfd.events = POLLIN;
		switch (poll(&pfd, 1, 100)) {
		case -1:
			dfu_err("%s, poll: %s\n", __func__, strerror(errno));
			return -1;
		case 0:
			return 0;
		default:
			stat = read(fd, &buf[done], sz - done);
			if (stat < 0) {
				dfu_err("%s, read: %s\n",
					__func__, strerror(errno));
				return stat;
			}
			break;
		}
	}
	return done;
}

static int flush_input(int fd)
{
	char tmp_buf[20];

	while(do_read(fd, tmp_buf, sizeof(tmp_buf)) > 0);
	return 0;
}


int linux_spi_bp_open(struct dfu_interface *iface,
		      const char *path, const void *pars)
{
	int ret = -1, i;
	char buf[20];
	char reply[8];
	int fd;
	struct termios tios, saved_tios;
	struct linux_event_data edata;

	fd = open(path, O_RDWR|O_NOCTTY);
	if (fd < 0)
		return fd;
	if (tcgetattr(fd, &tios) < 0) {
		dfu_err("tcgetattr");
		return -1;
	}
	if (tcgetattr(fd, &saved_tios) < 0) {
		dfu_err("tcgetattr");
		return -1;
	}
	tios.c_cflag= CS8 | CLOCAL | CREAD;
	tios.c_iflag= IGNPAR | BRKINT;
	tios.c_oflag= 0;
	tios.c_lflag= 0;
	if (cfsetspeed(&tios, B115200) < 0) {
		dfu_err("cfsetspeed");
		return -1;
	}
	if (tcsetattr(fd, TCSANOW, &tios) < 0) {
		dfu_err("tcsetattr");
		return -1;
	}
	_data.fd = fd;
	iface->priv = &_data;
	for (i = 0; i < 20; i++) {
		if (do_write(fd, "\0", 1) < 1)
			return -1;
		if (do_read(fd, reply, 5) == 5)
			break;
	}
	if (memcmp(reply, "BBIO", 4)) {
		dfu_err("%s: invalid reply %s\n",
			__func__, buf);
		return ret;
	}
	printf("BUS PIRATE DETECTED raw bitbang version %c\n", reply[4]);
	flush_input(fd);
	/* Now enter binary spi mode */
	if (write(fd, "\1", 1) < 0) {
		dfu_err("%s: write(): %s\n", __func__,
			strerror(errno));
		return -1;
	}
	if (do_read(fd, buf, 4) < 0) {
		dfu_err("%s: read(): %s\n", __func__,
			strerror(errno));
		return -1;
	}
	if (memcmp(buf, "SPI", 3)) {
		dfu_err("%s: invalid reply %s\n",
			__func__, buf);
		return ret;
	}
	printf("SPI protocol version %c\n", buf[3]);
	ret = spi_bus_pirate_config(&_data, 0);
	if (ret < 0)
		return ret;
	edata.fd = fd;
	edata.events = POLLIN;
	if (dfu_set_interface_event(iface->dfu, &edata) < 0) {
		dfu_err("Error setting interface event\n");
		return -1;
	}
	return 0;
}

static int _bulk_xfer(struct linux_spi_bp_data *priv,
		      const char *out_buf, char *in_buf,
		      unsigned long size)
{
	char my_buf[17], *ptr, dummy_reply[16];
	unsigned short sz = 0;
	uint8_t reply;
	unsigned long done;

	dfu_dbg("%s entered, size = %lu\n", __func__, size);
	cs_assert(priv);
	for (done = 0; done < size; done += sz) {
		sz = min(size - done, 16);

		if (sz < 1)
			return sz;

		/* Command: write and read (bulk xfer) */
		my_buf[0] = 0x10 | (sz - 1);
		dfu_dbg("sz = %u, my_buf[0] = 0x%02x\n",
			sz, (unsigned int)my_buf[0]);
		memcpy(&my_buf[1], &out_buf[done], sz);

		/* Send write data (0xff) */
		if (do_write(priv->fd, my_buf, sz + 1) < 0) {
			cs_deassert(priv);
			return -1;
		}
		/* Wait for ack */
		if (get_reply(priv, &reply, 1, 1000) <= 0) {
			dfu_err("%s: timeout/error from bus pirate\n", __func__);
			cs_deassert(priv);
			return -1;
		}
		if (reply != 1) {
			dfu_err("%s: unexpected reply 0x%02x\n", __func__,
				(unsigned int)reply);
			cs_deassert(priv);
			return -1;
		}
		/* Get read data */
		ptr = in_buf ? &in_buf[done] : dummy_reply;
		if (do_read(priv->fd, ptr, sz) < 0) {
			cs_deassert(priv);
			return -1;
		}
	}
	cs_deassert(priv);
	dfu_dbg("returning from %s, size = %lu\n", __func__, size);
	return size;
}

int linux_spi_bp_write(struct dfu_interface *iface, const char *buf,
		       unsigned long size)
{
	struct linux_spi_bp_data *priv = iface->priv;

	return _bulk_xfer(priv, buf, NULL, size);
}

int linux_spi_bp_write_read(struct dfu_interface *iface,
			    const char *out_buf, char *in_buf,
			    unsigned long size)
{
	struct linux_spi_bp_data *priv = iface->priv;

	return _bulk_xfer(priv, out_buf, in_buf, size);
}

int linux_spi_bp_fini(struct dfu_interface *iface)
{
	 struct linux_spi_bp_data *priv = iface->priv;

	if (close(priv->fd) < 0) {
		dfu_err("%s: error closing interface (%s)\n", __func__,
			strerror(errno));
		return -1;
	}
	return 0;
}
