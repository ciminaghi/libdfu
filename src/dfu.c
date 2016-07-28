#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"

static struct dfu_data dfu;
static struct dfu_interface interface;
static struct dfu_target target;
static struct dfu_host host;

/*
 * We just support one dfu instance at a time for the moment, that should
 * be enough
 * It shouldn't be too difficult extending to more than dfu at the same time
 */
struct dfu_data *dfu_init(const struct dfu_interface_ops *iops,
			  const char *interface_path,
			  const void *interface_pars,
			  const struct dfu_target_ops *tops,
			  const struct dfu_host_ops *hops)
{
	int stat;

	if (!iops || !tops || !hops)
		return NULL;
	if (dfu.busy)
		return NULL;
	dfu.busy = 1;
	dfu.interface = &interface;
	dfu.target = &target;
	dfu.host = &host;
	interface.ops = iops;
	target.ops = tops;
	host.ops = hops;
	if (hops->init) {
		stat = hops->init(&host);
		if (stat < 0)
			goto error;
	}
	if (tops->init) {
		stat = tops->init(&target, &interface);
		if (stat < 0)
			goto error;
	}
	stat = iops->open(&interface, interface_path, interface_pars);
	if (stat < 0)
		goto error;
	return &dfu;

error:
	dfu.busy = 0;
	return NULL;
}

void dfu_idle(struct dfu_data *dfu)
{
	if (dfu->host->ops->idle)
		dfu->host->ops->idle(dfu->host);
}

unsigned long dfu_get_current_time(struct dfu_data *dfu)
{
	if (dfu->host->ops->get_current_time)
		return dfu->host->ops->get_current_time(dfu->host);
	return 0xffffffffUL;
}

