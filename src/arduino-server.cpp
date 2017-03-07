/*
 * C api for arduino esp8266 web server
 * To be built under ARDUINO IDE only (as a part of libdfu for arduino)
 */
#include <ESP8266WebServer.h>
#include "arduino-server.h"

static ESP8266WebServer *server;
static int first_chunk, error, chunk_ready, last_chunk;
static uint8_t *current_chunk;
static int current_chunk_len;
static int finalized;

static void handle_ota_post(void)
{
	HTTPUpload& upload = server->upload();
	int n, tot;

	tot = server->arg("totchunk").toInt();
	n = server->arg("numchunk").toInt();
	first_chunk = n == 1;
	last_chunk = n == tot;
	chunk_ready = 1;
	current_chunk = upload.buf;
	current_chunk_len = upload.currentSize;
}


extern "C" int arduino_server_send(int code, const char *msg)
{
	server->send(code, "text/plain", msg);
	return 0;
}

extern "C" int arduino_server_poll(void)
{
	server->handleClient();
	return chunk_ready;
}

extern "C" void arduino_server_ack(void)
{
	chunk_ready = 0;
}

extern "C" void arduino_server_get_data(struct arduino_server_data *sd)
{
	sd->chunk_ready = chunk_ready;
	sd->first_chunk = first_chunk;
	sd->last_chunk = last_chunk;
	sd->error = error;
	sd->chunk_ptr = current_chunk;
	sd->chunk_len = current_chunk_len;
}

extern "C" int arduino_server_init(void *_srv)
{
	server = (ESP8266WebServer *)_srv;
	if (!finalized) {
		server->on("/otafile", HTTP_POST, handle_ota_post);
		server->begin();
	}
	finalized = 0;
}

extern "C" void arduino_server_fini(void)
{
	/*
	 * ESP8266WebServer has no end() method.
	 * We just take note that the server has been finalized and avoid
	 * trying to re-initialize it next time
	 */
	finalized = 1;
}
