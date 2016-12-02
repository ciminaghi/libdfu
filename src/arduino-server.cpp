/*
 * C api for arduino esp8266 web server
 * To be built under ARDUINO IDE only (as a part of libdfu for arduino)
 */
#include <ESP8266WebServer.h>
#include "arduino-server.h"

static ESP8266WebServer server(80);
static int first_chunk, error, chunk_ready, last_chunk;
static uint8_t *current_chunk;
static int current_chunk_len;
static int polled;

static void handle_ota_post(void)
{
	HTTPUpload& upload = server.upload();
	int n, tot;

	tot = server.arg("totchunk").toInt();
	n = server.arg("numchunk").toInt();
	first_chunk = n == 1;
	last_chunk = n == tot;
	chunk_ready = 1;
	current_chunk = upload.buf;
	current_chunk_len = upload.currentSize;
	polled = 0;
}


extern "C" int arduino_server_send(int code, const char *msg)
{
	server.send(code, msg);
	return 0;
}

extern "C" int arduino_server_poll(int *_chunk_ready, int *_error,
				   int *_first_chunk, int *_last_chunk)
{
	server.handleClient();
	if (polled) {
		*_chunk_ready = 0;
		*_error = 0;
		*_first_chunk = 0;
		*_last_chunk = 0;
		return 0;
	}
	*_chunk_ready = chunk_ready;
	*_error = error;
	*_first_chunk = first_chunk;
	*_last_chunk = last_chunk;
	polled = 1;
	return chunk_ready;
}

extern "C" int arduino_server_get_chunk(const void **ptr)
{
	*ptr = current_chunk;
	return current_chunk_len;
}

extern "C" int arduino_server_init(void)
{
	server.on("/otafile", HTTP_POST, handle_ota_post);
	server.begin();
}
