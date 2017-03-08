
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"

static int _do_setup(struct dfu_interface *iface)
{
	int ret;

	if (iface->start_cb)
		if (iface->start_cb(iface->start_cb_data) < 0)
			return -1;
	ret = iface->ops->open(iface, iface->path, iface->pars);
	iface->setup_done = 1;
	return ret;
}

int dfu_interface_open(struct dfu_interface *iface, const char *name,
		       const void *params)
{
	if (!iface->ops || !iface->ops->open)
		return -1;
	iface->path = name;
	iface->pars = params;
	iface->setup_done = 0;
	return 0;
}

int dfu_interface_fini(struct dfu_interface *iface)
{
	iface->path = NULL;
	iface->pars = NULL;
	if (!iface->setup_done)
		return 0;
	iface->setup_done = 0;
	if (!iface->ops->fini)
		return -1;
	return iface->ops->fini(iface);
}

int dfu_interface_poll_idle(struct dfu_interface *iface)
{
	if (!iface->ops->poll_idle)
		return -1;
	if (!iface->setup_done)
		return 0;
	return iface->ops->poll_idle(iface);
}

int dfu_interface_target_reset(struct dfu_interface *iface)
{
	if (!iface->ops->target_reset)
		return -1;
	if (!iface->setup_done)
		if (_do_setup(iface) < 0)
			return -1;
	return iface->ops->target_reset(iface);
}

int dfu_interface_target_run(struct dfu_interface *iface)
{
	if (!iface->ops->target_run)
		return -1;
	if (!iface->setup_done)
		if (_do_setup(iface) < 0)
			return -1;
	return iface->ops->target_run(iface);
}

int dfu_interface_read(struct dfu_interface *iface, char *buf,
		       unsigned long sz)
{
	if (!iface->ops->read)
		return -1;
	if (!iface->setup_done)
		if (_do_setup(iface) < 0)
			return -1;
	return iface->ops->read(iface, buf, sz);
}

int dfu_interface_write(struct dfu_interface *iface, const char *buf,
			       unsigned long sz)
{
	if (!iface->ops->write)
		return -1;
	if (!iface->setup_done)
		if (_do_setup(iface) < 0)
			return -1;
	return iface->ops->write(iface, buf, sz);
}

int dfu_interface_write_read(struct dfu_interface *iface, const char *wr_buf,
			     char *rd_buf, unsigned long sz)
{
	if (!iface->ops->write_read)
		return -1;
	if (!iface->setup_done)
		if (_do_setup(iface) < 0)
			return -1;
	return iface->ops->write_read(iface, wr_buf, rd_buf, sz);
}
