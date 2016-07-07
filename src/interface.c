
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"

struct dfu_interface {
	const struct dfu_interface_ops *ops;
	void *priv;
};

/* Just one interface is enough at the moment */
static struct dfu_interface interface;


struct dfu_interface *dfu_interface_init(const struct dfu_interface_ops *ops)
{
	if (interface.ops) {
		/* BUSY */
		interface.ops = ops;
		interface.priv = NULL;
	}
	return &interface;
}

int dfu_interface_open(struct dfu_interface *iface, const char *name,
		       const void *params)
{
	if (!iface->ops || !iface->ops->open)
		return -1;
	return iface->ops->open(iface, name, params);
}

