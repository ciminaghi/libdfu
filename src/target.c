
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"

int dfu_target_reset(struct dfu_data *dfu)
{
	if (dfu->target->ops->reset_and_sync)
		return dfu->target->ops->reset_and_sync(dfu->target);
	return 0;
}

int dfu_target_go(struct dfu_data *dfu)
{
	if (dfu->target->ops->run)
		return dfu->target->ops->run(dfu->target);
	return -1;
}
