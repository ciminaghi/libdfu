/*
 * C api for arduino esp8266 web server
 * To be built under ARDUINO IDE only (as a part of libdfu for arduino)
 */
#include <ESP8266WebServer.h>
#include "arduino-server.h"

static ESP8266WebServer server(80);
static int tot_chunks, num_chunk, chunk_ready, error;
static String current_chunk;

static void handle_chunk(void)
{
	tot_chunks = server.arg(0).toInt();
	num_chunk = server.arg(1).toInt();
	current_chunk = server.arg(2);
	chunk_ready = 1;
}


extern "C" int arduino_server_send(int code, const char *msg)
{
	server.send(code, msg);
	return 0;
}

extern "C" int arduino_server_poll(int *_chunk_ready, int *_error,
				   int *_num_chunk, int *_tot_chunks)
{
	server.handleClient();
	*_chunk_ready = chunk_ready;
	*_error = error;
	*_num_chunk = num_chunk;
	*_tot_chunks = tot_chunks;
	return chunk_ready;
}

extern "C" int arduino_server_get_chunk(const void **ptr)
{
	*ptr = current_chunk.c_str();
	chunk_ready = 0;
	return current_chunk.length();
}

extern "C" int arduino_server_init(void)
{
	server.on("/otafile", HTTP_POST, handle_chunk);
	server.begin();
}
