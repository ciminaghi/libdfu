#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"

#ifndef CONFIG_DFU_MAX_TIMEOUTS
#define CONFIG_DFU_MAX_TIMEOUTS 4
#endif

static struct dfu_data dfu;
static struct dfu_interface interface;
static struct dfu_target target;
static struct dfu_host host;

static struct dfu_timeout *timeouts[CONFIG_DFU_MAX_TIMEOUTS];

/*
 * We just support one dfu instance at a time for the moment, that should
 * be enough
 * It shouldn't be too difficult extending to more than dfu at the same time
 */
struct dfu_data *dfu_init(const struct dfu_interface_ops *iops,
			  const char *interface_path,
			  const void *interface_pars,
			  int (*start_cb)(void *),
			  void *start_cb_data,
			  const struct dfu_target_ops *tops,
			  const void *target_pars,
			  const struct dfu_host_ops *hops)
{
	int stat;

	if (!iops || !tops || !hops)
		return NULL;
	if (dfu.busy)
		return NULL;
	dfu.busy = 1;
	dfu.error = 0;
	dfu.interface = &interface;
	dfu.target = &target;
	dfu.host = &host;
	interface.dfu = &dfu;
	interface.ops = iops;
	interface.start_cb = start_cb;
	interface.start_cb_data = start_cb_data;
	target.dfu = &dfu;
	target.ops = tops;
	target.pars = target_pars;
	target.busy = 0;
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

int dfu_fini(struct dfu_data *dfu)
{
	int ret = 0, i;

	if (!dfu)
		return -1;
	/*
	 * Cancel all timeouts
	 */
	for (i = 0; i < ARRAY_SIZE(timeouts); i++)
		if (timeouts[i])
			timeouts[i] = NULL;
	/* Finalize everything */
	if (dfu->interface && dfu_interface_has_fini(dfu->interface)) {
		ret = dfu_interface_fini(dfu->interface);
		if (ret < 0)
			return ret;
	}
	if (dfu->target && dfu->target->ops->fini) {
		ret = dfu->target->ops->fini(dfu->target);
		if (ret < 0)
			return ret;
	}
	if (dfu->host && dfu->host->ops->fini) {
		ret = dfu->host->ops->fini(dfu->host);
		if (ret < 0)
			return ret;
	}
	/* Reset data structure */
	dfu->busy = dfu->error = 0;
	return ret;
}

static int _insert_timeout(struct dfu_data *dfu, struct dfu_timeout *to)
{
	int i, j;
	struct dfu_timeout *ptr;
	unsigned long now = dfu_get_current_time(dfu);

	for (i = 0, to->timeout += now; i < ARRAY_SIZE(timeouts); i++) {
		ptr = timeouts[i];
		if (!ptr) {
			/* Last element */
			timeouts[i] = to;
			return 0;
		}
		if (time_after(to->timeout, ptr->timeout)) {
			to->timeout -= ptr->timeout;
			continue;
		}
		/*
		 * Shift everything right by one and make slot available for
		 * new timeout
		 */
		if (timeouts[ARRAY_SIZE(timeouts) - 1])
			/* No space */
			return -1;
		for (j = ARRAY_SIZE(timeouts) - 1; j >= i; j--)
			timeouts[j] = timeouts[j - 1];
		timeouts[i] = to;
		timeouts[i+1]->timeout -= to->timeout;
		return 0;
	}
	/* No space */
	return -1;
}

static int _remove_timeout(struct dfu_timeout *to)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(timeouts) && timeouts[i] != to; i++);
	if (i == ARRAY_SIZE(timeouts))
		return -1;
	if (i < (ARRAY_SIZE(timeouts) - 1) && timeouts[i+1])
		timeouts[i+1]->timeout += timeouts[i]->timeout;
	/* Timeout found, shift everything left  */
	for ( ; i < (ARRAY_SIZE(timeouts) - 1); i++)
		timeouts[i] = timeouts[i+1];
	/* Make sure unused slots always contain a NULL pointer */
	/* Note that i should be = ARRAY_SIZE(timeouts) - 1 */
	timeouts[i] = NULL;
	return 0;
}

int dfu_set_timeout(struct dfu_data *dfu, struct dfu_timeout *to)
{
	dfu_dbg("%s: inserting timeout %p\n", __func__, to);
	return _insert_timeout(dfu, to);
}

int dfu_cancel_timeout(struct dfu_timeout *to)
{
	dfu_dbg("%s: removing timeout %p\n", __func__, to);
	return _remove_timeout(to);
}

static int _trigger_timeout(struct dfu_data *dfu, struct dfu_timeout *to)
{
	dfu_dbg("%s: triggering timeout %p\n", __func__, to);
	timeouts[0]->cb(dfu, timeouts[0]->priv);
	return _remove_timeout(to);
}

static void _trigger_interface_event(struct dfu_data *dfu)
{
	if (dfu->target->ops->on_interface_event)
		dfu->target->ops->on_interface_event(dfu->target);
}

static void _trigger_file_event(struct dfu_data *dfu)
{
	if (!dfu->bf || !dfu->bf->ops || !dfu->bf->ops->on_event)
		return;
	dfu->bf->ops->on_event(dfu->bf);
}

static void _poll_interface(struct dfu_data *dfu)
{
	switch(dfu_interface_poll_idle(dfu->interface)) {
	case 0:
		/* No event */
		break;
	case DFU_INTERFACE_EVENT:
		_trigger_interface_event(dfu);
		break;
	default:
		dfu_log("%s: unexpected retval from interface poll_idle()\n",
			__func__);
		break;
	}
}

static inline int _bf_is_pollable(struct dfu_binary_file *bf)
{
	return (bf->ops && bf->ops->poll_idle);
}

static void _poll_file(struct dfu_data *dfu)
{
	struct dfu_binary_file *bf = dfu->bf;

	switch(bf->ops->poll_idle(bf)) {
	case 0:
		/* No event */
		break;
	case DFU_FILE_EVENT:
		_trigger_file_event(dfu);
		break;
	default:
		dfu_log("%s: unexpected retval from file poll_idle()\n",
			__func__);
		break;
	}
}

/*
 * Idle loop: either rely on host's idle operation or poll everything to
 * check whether some event has happened
 */
int dfu_idle(struct dfu_data *dfu)
{
	unsigned long now;
	int next_timeout, stat;

	if (!dfu || !dfu->busy)
		/* Uninitialized data structure, cannot call dfu_idle */
		return DFU_ERROR;
	if (dfu_error(dfu))
		/* An asynchronous error occurred, tell the user */
		return DFU_ERROR;
	if (dfu_interface_has_poll_idle(dfu->interface))
		_poll_interface(dfu);
	if (_bf_is_pollable(dfu->bf))
		_poll_file(dfu);
	next_timeout = !timeouts[0] ? -1 : timeouts[0]->timeout;
	now = dfu_get_current_time(dfu);
	if (timeouts[0] && time_after(now, next_timeout))
		if (_trigger_timeout(dfu, timeouts[0]) < 0) {
			dfu_err("removing timeout");
			return DFU_ERROR;
		}
	if (dfu->target->ops->on_idle)
		dfu->target->ops->on_idle(dfu->target);
	if (dfu_binary_file_on_idle(dfu->bf) < 0)
		return DFU_ERROR;
	if (dfu->host->ops->idle) {
		stat = dfu->host->ops->idle(dfu->host, next_timeout);
		if (stat < 0)
			return stat;
		if (stat & DFU_TIMEOUT)
			if (_trigger_timeout(dfu, timeouts[0]) < 0) {
				dfu_err("removing timeout");
				return DFU_ERROR;
			}
		if (stat & DFU_FILE_EVENT)
			_trigger_file_event(dfu);
		if (stat & DFU_INTERFACE_EVENT)
			_trigger_interface_event(dfu);
	}
	if (dfu_binary_file_written(dfu->bf))
		return DFU_ALL_DONE;
	return DFU_CONTINUE;
}

unsigned long dfu_get_current_time(struct dfu_data *dfu)
{
	if (dfu->host->ops->get_current_time)
		return dfu->host->ops->get_current_time(dfu->host);
	return 0xffffffffUL;
}

int dfu_set_binary_file_event(struct dfu_data *dfu, void *event_data)
{
	if (!dfu->host->ops->set_binary_file_event)
		return -1;
	return dfu->host->ops->set_binary_file_event(dfu->host, event_data);
}

int dfu_set_interface_event(struct dfu_data *dfu, void *event_data)
{
	if (!dfu->host->ops->set_interface_event)
		return -1;
	return dfu->host->ops->set_interface_event(dfu->host, event_data);
}
