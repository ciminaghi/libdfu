#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"

struct dfu_data *dfu_init(const struct dfu_interface_ops *iops,
			  const char *interface_path,
			  const void *interface_pars,
			  const struct dfu_target_ops *tops,
			  const struct dfu_host_ops *hops)
{
	return NULL;
}

void dfu_idle(struct dfu_data *data)
{
}
