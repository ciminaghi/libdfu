/*
 * This rx method uses the arduino web server, build it under arduino only
 */
#ifdef ARDUINO

#include <dfu.h>
#include <dfu-internal.h>

struct http_arduino_client_priv {
	struct dfu_binary_file *bf;
	int chunk_ready;
	int error;
	int num_chunk;
	int tot_chunks;
};

/* DFU file rx methods */

static struct http_arduino_client_priv client_priv;

static int http_arduino_poll_idle(struct dfu_binary_file *bf)
{
	arduino_server_poll(&client_priv.chunk_ready, &client_priv.error,
			    &client_priv.num_chunk, &client_priv.tot_chunks);
	return client_priv.chunk_ready ? DFU_FILE_EVENT : 0;
}

static int http_arduino_on_event(struct dfu_binary_file *bf)
{
	int stat, appended;
	void *ptr;

	if (!client_priv.chunk_ready) {
		dfu_err("%s: BUG, chunk is not ready\n", __func__);
		return DFU_ERROR;
	}
	if (client_priv.error)  {
		arduino_server_send(500, "text/plain", "Programming error");
		client_priv.chunk_ready = 0;
		return 0;
	}
	if (client_priv.num_chunk == 1) {
		dfu_log("Resetting target ... ");
		if (dfu_target_reset(bf->dfu) < 0) {
			dfu_log("ERROR !\n");
			return DFU_ERROR;
		}
		dfu_log("OK\n");
		dfu_log("Probing target ... ");
		if (dfu_target_probe(bf->dfu) < 0) {
			dfu_log("ERROR !\n");
			return DFU_ERROR;
		}
		dfu_log("OK\n");
		dfu_log("Erasing flash ... ");
		if (dfu_target_erase_all(bf->dfu) < 0) {
			dfu_log("ERROR !\n");
			return DFU_ERROR;
		}
		dfu_log("OK\n");
	}
	stat = arduino_server_get_chunk(&ptr);
	if (stat < 0) {
		dfu_log("Error getting pointer to current chunk\n");
		return DFU_ERROR;
	}
	appended = dfu_binary_file_append_buffer(bf, ptr, stat);
	if (appended < 0) {
		dfu_log("Error appending current chunk\n");
		return DFU_ERROR;
	}
	if (!appended)
		/* No space enough, do nothing */
		return appended;
	if (appended < stat) {
		dfu_log("Error: partially appended chunk\n");
		return DFU_ERROR;
	}
	/* OK */
	if (client_priv.num_chunk < client_priv.tot_chunks)
		arduino_server_send(200, "text/plain", "Partial content");
	return 0;
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
	return arduino_server_init();
}

static const struct dfu_file_rx_method_ops http_arduino_rx_ops = {
	.init = http_arduino_rx_init,
};

declare_file_rx_method(http_arduino, &http_arduino_rx_ops);

#endif /* ARDUINO */
