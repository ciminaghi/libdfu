
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"

int dfu_target_reset(struct dfu_data *dfu)
{
	if (dfu->target->ops->reset_and_sync)
		return dfu->target->ops->reset_and_sync(dfu->target);
	return 0;
}

int dfu_target_probe(struct dfu_data *dfu)
{
	if (dfu->target->ops->probe)
		return dfu->target->ops->probe(dfu->target);
	return 0;
}

int dfu_target_go(struct dfu_data *dfu)
{
	if (dfu->target->ops->run)
		return dfu->target->ops->run(dfu->target);
	return -1;
}

int dfu_target_erase_all(struct dfu_data *dfu)
{
	if (dfu->target->ops->erase_all)
		return dfu->target->ops->erase_all(dfu->target);
	return 0;
}
