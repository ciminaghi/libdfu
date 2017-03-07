/*
 * This rx method uses the arduino web server, build it under arduino only
 */
#ifdef ARDUINO

#include <dfu.h>
#include <dfu-internal.h>
#include "arduino-server.h"

struct http_arduino_client_priv {
	struct dfu_binary_file *bf;
	int busy;
};

/* DFU file rx methods */

static struct http_arduino_client_priv client_priv;

static int http_arduino_poll_idle(struct dfu_binary_file *bf)
{
	if (client_priv.busy)
		return 0;
	return arduino_server_poll() ? DFU_FILE_EVENT : 0;
}

static int http_arduino_on_event(struct dfu_binary_file *bf)
{
	struct arduino_server_data sd;
	int stat, appended;
	void *ptr;

	arduino_server_get_data(&sd);
	if (sd.error)  {
		arduino_server_send(500, "Programming error");
		arduino_server_ack();
		return 0;
	}
	if (sd.first_chunk) {
		client_priv.busy = 1;
		dfu_log("Resetting target ... ");
		if (dfu_target_reset(bf->dfu) < 0)
			goto error;
		dfu_log("OK\n");
		dfu_log("Probing target ... ");
		if (dfu_target_probe(bf->dfu) < 0)
			goto error;
		dfu_log("OK\n");
		dfu_log("Erasing flash ... ");
		if (dfu_target_erase_all(bf->dfu) < 0)
			goto error;
		dfu_log("OK\n");
	}
	client_priv.busy = 0;
	dfu_dbg("%s: appending %d bytes\n", __func__, stat);
	appended = dfu_binary_file_append_buffer(bf, sd.chunk_ptr,
						 sd.chunk_len);
	if (appended < 0) {
		dfu_log("Error appending current chunk\n");
		goto error;
	}
	dfu_dbg("%s: %d bytes appended\n", __func__, appended);
	if (!appended)
		/* No space enough, do nothing */
		return appended;
	if (appended < stat) {
		dfu_log("Error: partially appended chunk\n");
		goto error;
	}
	arduino_server_ack();
	/* OK */
	if (!sd.last_chunk)
		arduino_server_send(200, "Partial content");
	return 0;

error:
	dfu_log("ERROR\n");
	client_priv.busy = 0;
	arduino_server_ack();
	return DFU_ERROR;
}

static const struct dfu_binary_file_ops http_arduino_rx_method_ops = {
	.poll_idle = http_arduino_poll_idle,
	.on_event = http_arduino_on_event,
};

static int http_arduino_rx_init(struct dfu_binary_file *bf, void *arg)
{
	memset(&client_priv, 0, sizeof(client_priv));
	client_priv.bf = bf;
	bf->ops = &http_arduino_rx_method_ops;
	return arduino_server_init(arg);
}

static void http_arduino_rx_done(struct dfu_binary_file *bf, int status)
{
	int code = status < 0 ? 500 : 200;
	const char *msg = status < 0 ? "Programming error" : "All done";

	arduino_server_send(code, msg);
}

static int http_arduino_rx_fini(struct dfu_binary_file *bf)
{
	arduino_server_fini();
	return 0;
}

static const struct dfu_file_rx_method_ops http_arduino_rx_ops = {
	.init = http_arduino_rx_init,
	.done = http_arduino_rx_done,
	.fini = http_arduino_rx_fini,
};

declare_file_rx_method(http_arduino, &http_arduino_rx_ops);

#endif /* ARDUINO */
