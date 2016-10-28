#include <dfu.h>
#include <dfu-internal.h>
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include "tcp-conn-lwip-raw.h"
#include "rx-method-http-lwip.h"

#define POST_FORM_DATA "multipart/form-data"

static int http_flash_upload_post(const struct http_url *u,
				  struct http_connection *c,
				  struct phr_header *headers, int num_headers,
				  const char *data, int data_len)
{
	struct phr_header *h;
	int stat, ret;
	struct tcp_conn_data *cd = c->cd;
	const char *contents, *ptr;

	h = http_find_header("Content-Type", headers, num_headers);
	if (!h) {
		dfu_err("%s: missing Content-Type header\n", __func__);
		/* FIXME: IS THIS OK ? */
		return http_request_error(c, HTTP_BAD_REQUEST);
	}
	if (memcmp(h->value, POST_FORM_DATA, strlen(POST_FORM_DATA))) {
		dfu_err("%s: unsupported Content-Type\n", __func__);
		/* FIXME: IS THIS OK ? */
		return http_request_error(c, HTTP_NOT_IMPLEMENTED);
	}
	/* Actual contents start */
	contents = http_find_post_contents_start(data);
	if (!contents) {
		dfu_err("%s: missing contents in data\n", __func__);
		/* FIXME: IS THIS OK ? */
		return http_request_error(c, HTTP_BAD_REQUEST);
	}
	/*
	 * Look for the beginning of last line, which contains the boundary
	 * The boundary line (and the preceding end of line) is __not__ part of
	 * the buffer to be appended
	 */
	ptr = strstr(contents, "\r\n--");
	stat = dfu_binary_file_append_buffer(c->bf,
					     contents,
					     ptr ? ptr - contents :
					     /* No boundary found, all data */
					     data_len - (contents - data));
	if (!stat) {
		/* No space enough, just tell the server to retry processing */
		dfu_dbg("%s: no space enough for appending buffer\n", __func__);
		return stat;
	}
	if (stat < 0) {
		dfu_err("%s: error appending data\n", __func__);
		http_request_error(c, HTTP_INTERNAL_SERVER_ERROR);
		ret = -1;
	} else  {
		dfu_dbg("%s: SENDING OK\n", __func__);
		ret = http_send_status(cd, HTTP_OK);
	}
	return ret;
}

declare_http_url(flash_upload, NULL, http_flash_upload_post);
