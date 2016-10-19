#ifndef __RX_METHOD_HTTP_LWIP_H__
#define __RX_METHOD_HTTP_LWIP_H__

#include "picohttpparser.h"

struct tcp_conn_data;

struct http_connection {
	struct tcp_conn_data *cd;
	struct dfu_binary_file *bf;
	int curr_request_index;
	int can_close;
	char request_buf[3000];
	/* Used to send constant data */
	int curr_send_index;
	struct phr_header headers[20];
	size_t nheaders;
	const char *method;
	size_t method_len;
	const char *path;
	size_t path_len;
	int minor_version;
	int end_of_headers;
	int content_length;
	const void *outgoing_data;
	int outgoing_data_len;
};

struct http_url {
	const char *path;
	const char *content_type;
	/* Pointers to const data start and end (for GET queries) */
	const void *data_start;
	const void *data_end;
	/*
	 * Must return:
	 * < 0  -> error
	 * == 0 -> request could not be processed, retry
	 * > 0  -> OK
	 */
	int (*get)(const struct http_url *, struct http_connection *,
		   struct phr_header *headers, int num_headers);
	/*
	 * Must return:
	 * < 0  -> error
	 * == 0 -> request could not be processed, retry
	 * > 0  -> OK
	 */
	int (*post)(const struct http_url *, struct http_connection *,
		    struct phr_header *headers, int num_headers,
		    const char *data, int data_len);
};

/* Force size to 32 bytes */
union _http_url {
	struct http_url url;
	char dummy[32];
};

#define declare_http_url(p, gt, pt)					\
	static const union _http_url					\
	http_url_ ## p __attribute__((section(".http-urls"), used, aligned(16))) = { \
		.url = {						\
			.path = "/"#p,					\
			.get = gt,					\
			.post = pt,					\
		},							\
	}

#define declare_http_file_url(p, st, end, ct)				\
	static const union _http_url					\
	http_url_ ## p __attribute__((section(".http-urls"), used, aligned(16))) = { \
		.url = {						\
			.path = "/"#p,					\
			.data_start = st,				\
			.data_end = end,				\
			.post = NULL,					\
			.get = http_get_file,				\
			.content_type = ct,				\
		},							\
	}

extern const union _http_url http_urls_start[], http_urls_end[];

extern struct phr_header *http_find_header(const char *h,
					   struct phr_header *headers,
					   int num_headers);

extern const char *http_find_post_contents_start(const char *buf);

enum http_status {
	HTTP_OK = 0,
	HTTP_BAD_REQUEST,
	HTTP_NOT_FOUND,
	HTTP_METHOD_NOT_ALLOWED,
	HTTP_NOT_IMPLEMENTED,
	HTTP_TOO_MANY_REQUESTS,
	HTTP_INSUFFICIENT_STORAGE,
	HTTP_INTERNAL_SERVER_ERROR,
};

extern int http_send_status(struct tcp_conn_data *cd, enum http_status s);
extern int http_request_error(struct http_connection *c, enum http_status s);
extern int http_get_file(const struct http_url *u, struct http_connection *c,
			 struct phr_header *headers, int num_headers);


#endif /* __RX_METHOD_HTTP_LWIP_H__ */

