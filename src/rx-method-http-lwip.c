
#ifdef HAVE_LWIP

#include <dfu.h>
#include <dfu-internal.h>
#include "lwip/opt.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include "tcp-conn-lwip-raw.h"
#include "picohttpparser.h"
#include "rx-method-http-lwip.h"

struct http_lwip_client_priv {
	struct dfu_binary_file *bf;
	struct http_connection *c;
	int request_ready;
	int serving_request;
};

#ifdef LWIP_TCP

static const char *http_status[] = {
	[HTTP_OK] = "HTTP/1.1 200 OK\r\n",
	[HTTP_BAD_REQUEST] = "HTTP/1.1 400 Bad Request\r\n",
	[HTTP_NOT_FOUND] = "HTTP/1.1 404 Not Found\r\n",
	[HTTP_METHOD_NOT_ALLOWED] = "HTTP/1.1 405 Method Not Allowed\r\n",
	[HTTP_NOT_IMPLEMENTED] = "HTTP/1.1 501 Not Implemented\r\n",
	[HTTP_TOO_MANY_REQUESTS] = "HTTP/1.1 429 Too Many Requests\r\n",
	[HTTP_INSUFFICIENT_STORAGE] = "HTTP/1.1 507 Insufficient Storage\r\n",
	[HTTP_INTERNAL_SERVER_ERROR] = "HTTP/1.1 500 Internal Server Error\r\n",
};

/* Just 1 connection at the moment */
struct http_connection http_connections[1];

/* Lwip raw "socket" operations */

static struct http_connection *find_connection(void)
{
	if (http_connections[0].cd)
		return NULL;
	return &http_connections[0];
}

static void free_connection(struct http_connection *c)
{
	dfu_log("%s\n", __func__);
	c->cd = NULL;
}

/*
 * Exported to url handlers
 */
struct phr_header *http_find_header(const char *h,
				    struct phr_header *headers,
				    int num_headers)
{
	int i;
	struct phr_header *ptr;

	for (i = 0, ptr = headers; i < num_headers; i++, ptr++) {
		if (!memcmp(h, ptr->name, strlen(h)))
			return ptr;
	}
	return NULL;
}

/*
 * Find contents in a post query's body
 */
const char *http_find_post_contents_start(const char *body)
{
	const char *ptr;

	/* Return pointer to first char after first empty line */
	ptr = strstr(body, "\r\n\r\n");
	if (!ptr)
		return NULL;
	return &ptr[4];
}

/*
 * New connection
 */
static int http_accept(struct tcp_server_socket_lwip_raw *r,
		       struct tcp_conn_data *cd)
{
	struct http_lwip_client_priv *priv = r->client_priv;
	struct http_connection *c = find_connection();

	if (!c) {
		dfu_err("%s: no memory for new connection\n", __func__);
		return -1;
	}
	c->cd = cd;
	c->curr_request_index = 0;
	c->request_buf[0] = 0;
	c->bf = priv->bf;
	c->can_close = 0;
	priv->c = c;
	/* Store connection pointer into binary file private data */
	c->bf->priv = c;
	dfu_dbg("%s: new connection ok, binary file = %p, head = %d\n",
		__func__, c->bf, c->bf->head);
	return 0;
}

int http_send_status(struct tcp_conn_data *cd, enum http_status s)
{
	const char *str;
	int ret, end = s != HTTP_OK;
	static const char *newl = "\r\n";

	if (s >= ARRAY_SIZE(http_status)) {
		dfu_err("%s: invalid status %d\n", __func__, s);
		return -1;
	}
	str = http_status[s];
	ret = tcp_server_socket_lwip_raw_send(cd, str, strlen(str));
	if (ret < 0 || !end)
		return ret;
	return tcp_server_socket_lwip_raw_send(cd, newl, strlen(newl));
}

int http_send_header(struct tcp_conn_data *cd, const char *key,
		     const char *value)
{
	static char buf[40];
	static const char *newl = "\r\n";
	int tot;

	tot = strlen(key) + strlen(value) + strlen(newl);
	if (tot >= sizeof(buf)) {
		dfu_err("Not enough memory for sending header, skipping\n");
		return 0;
	}
	memset(buf, 0, sizeof(buf));
	memcpy(buf, key, strlen(key));
	memcpy(&buf[strlen(key)], value, strlen(value));
	memcpy(&buf[strlen(key) + strlen(value)], newl, strlen(newl));
	dfu_log("%s: writing %s\n", __func__, buf);
	return tcp_server_socket_lwip_raw_send(cd, buf, tot);
}

int http_send_ctype(struct tcp_conn_data *cd, const char *ct)
{
	return http_send_header(cd, "Content-Type: ", ct);
}

int http_send_clen(struct tcp_conn_data *cd, int clen)
{
	static char buf[6];
	int i, divider;

	if (clen > 99999) {
		dfu_err("Unsupported content length\n");
		return -1;
	}

	memset(buf, 0, sizeof(buf));
	for (i = 0, divider = 10000; divider && i < sizeof(buf);
	     i++, divider /= 10)
		buf[i] = '0' + ((clen / divider) % 10);
	return http_send_header(cd, "Content-Lenght: ", buf);
}

static const struct http_url *find_url(const char *path)
{
	const union _http_url *ptr;

	for (ptr = http_urls_start; ptr != http_urls_end; ptr++) {
		if (!memcmp(path, ptr->url.path, strlen(ptr->url.path)))
			return &ptr->url;
	}
	return NULL;
}

static int http_send_headers_end(struct tcp_conn_data *cd)
{
	static const char *newl = "\r\n";

	return tcp_server_socket_lwip_raw_send(cd, newl, strlen(newl));
}

static int http_async_send_data(struct http_connection *c)
{
	int ret;

	ret = tcp_server_socket_lwip_raw_send(c->cd, c->outgoing_data,
					      c->outgoing_data_len);
	if (ret < 0)
		return ret;
	c->outgoing_data_len -= ret;
	c->outgoing_data = c->outgoing_data_len ?
		(char *)c->outgoing_data + ret : NULL;
	return ret;
}

static int http_start_sending_data(struct http_connection *c,
				   const void *data, int data_len)
{
	c->outgoing_data = data;
	c->outgoing_data_len = data_len;
	return http_async_send_data(c);
}

/*
 * Get method for regular files
 * Returns:
 *
 * -1  -> fatal error
 * -2  -> temporary error
 * > 0  -> ok
 */
int http_get_file(const struct http_url *u, struct http_connection *c,
		  struct phr_header *headers, int num_headers)
{
	int ret = 0, data_len;
	struct tcp_conn_data *cd = c->cd;

	c->curr_send_index = 0;
	ret = http_send_status(c->cd, HTTP_OK);
	if (ret < 0)
		goto end;
	ret = http_send_ctype(cd,
			      u->content_type ?
			      u->content_type : "application/octet-stream");
	if (ret < 0)
		goto end;
	data_len = (char *)u->data_end - (char *)u->data_start;
	ret = http_send_clen(cd, data_len);
	if (ret < 0)
		goto end;
	ret = http_send_headers_end(cd);
	if (ret < 0)
		goto end;
	ret = http_start_sending_data(c, u->data_start, data_len);
end:
	return ret < 0 ? HTTP_URL_FATAL_ERROR : 1;
}

/*
 * Returns:
 * < 0  -> error
 * == 0 -> could not process request, retry
 * > 0  -> request processed
 */
static int http_process_request(struct http_connection *c,
				const char *method, int method_len,
				const char *path, int path_len,
				struct phr_header *headers, int num_headers,
				const char *data, int data_len)
{
	const struct http_url *u;

	u = find_url(path);
	if (!u) {
		dfu_dbg("%s: requested url %s not found\n", __func__, path);
		return http_request_error(c, HTTP_NOT_FOUND);
	}
	if (!memcmp(method, "GET", 3) && u->get)
		return u->get(u, c, headers, num_headers);
	if (!memcmp(method, "POST", 4) && u->post)
		return u->post(u, c, headers, num_headers, data, data_len);
	return http_request_error(c, HTTP_METHOD_NOT_ALLOWED);
}

static void http_reset_request_data(struct http_connection *c)
{
	c->curr_request_index = 0;
	c->nheaders = ARRAY_SIZE(c->headers);
	c->method = NULL;
	c->method_len = 0;
	c->path = NULL;
	c->path_len = 0;
	c->minor_version = 0;
	c->end_of_headers = 0;
	c->outgoing_data = NULL;
	c->outgoing_data_len = 0;
}

/*
 * send error and close connection
 */
static int _http_request_error(struct http_connection *c, enum http_status s)
{
	http_request_error(c, s);
	c->can_close = 1;
	if (!tcp_server_socket_lwip_raw_close(c->cd))
		free_connection(c);
	return 1;
}

/*
 * must return > 0 because current request has been processed
 */
int http_request_error(struct http_connection *c, enum http_status s)
{
	http_send_status(c->cd, s);
	return 1;
}

static int simple_atoi(const char *s)
{
	int out = 0;

	for ( ; *s; s++) {
		if (*s < '0' || *s > '9') {
			dfu_err("%s: invalid number %s\n", __func__, s);
			return -1;
		}
		out = out * 10 + *s - '0';
	}
	return out;
}

/*
 * Data received
 */
int http_recv(struct tcp_server_socket_lwip_raw *r, const void *buf,
	      unsigned int len)
{
	struct http_lwip_client_priv *priv = r->client_priv;
	struct http_connection *c = priv->c;
	static struct phr_header *lh;
	const char *ptr = buf;
	int prevlen, stat;

	if (len > sizeof(c->request_buf) - c->curr_request_index) {
		_http_request_error(c, HTTP_INSUFFICIENT_STORAGE);
		return len;
	}
	if (!buf) {
		_http_request_error(c, HTTP_INTERNAL_SERVER_ERROR);
		return len;
	}

	dfu_log("%s: curr_request_index = %d, buf = %p (0x%02x %02x %02x ...), "
		"len = %d\n", __func__, c->curr_request_index, buf, ptr[0],
		ptr[1], ptr[2], len);

	memcpy(&c->request_buf[c->curr_request_index], buf, len);
	prevlen = c->curr_request_index;
	c->curr_request_index += len;
	if (!c->method) {
		char tmp[6];

		c->nheaders = ARRAY_SIZE(c->headers);
		stat = phr_parse_request(c->request_buf,
					 c->curr_request_index,
					 &c->method,
					 &c->method_len,
					 &c->path,
					 &c->path_len,
					 &c->minor_version,
					 c->headers,
					 &c->nheaders,
					 prevlen);
		if (stat < 0) {
			_http_request_error(c, HTTP_BAD_REQUEST);
			return len;
		}

		c->end_of_headers = stat;
		dfu_log("%s: parse ok, headers end @%d\n", __func__, stat);

		lh = http_find_header("Content-Length",
				      c->headers, c->nheaders);
		if (lh) {
			if (lh->value_len >= sizeof(tmp)) {
				dfu_err("Content-length value too big\n");
				_http_request_error(c, HTTP_BAD_REQUEST);
				return len;
			}
			memcpy(tmp, lh->value, lh->value_len);
			tmp[lh->value_len] = 0;
			c->content_length = simple_atoi(tmp);
			if (c->content_length < 0) {
				dfu_err("invalid content length\n");
				_http_request_error(c, HTTP_BAD_REQUEST);
				return len;
			}
		}
	} else
		stat = c->curr_request_index;
	switch (stat) {
	case -1:
		/* Incorrect request */
		dfu_err("Invalid http request\n");
		_http_request_error(c, HTTP_BAD_REQUEST);
		break;
	case -2:
		/* Incomplete request */
		dfu_dbg("Incomplete request, waiting\n");
		break;
	default:
	{
		if (c->curr_request_index <
		    c->end_of_headers + c->content_length) {
			dfu_log("curr len = %d, headers = %d, clen = %d\n",
				c->curr_request_index, c->end_of_headers,
				c->content_length);
			dfu_log("Must wait\n");
			break;
		}
		dfu_log("%s: got all body\n", __func__);
		dfu_dbg("%s: ptr = %p, data = %p, %c %c %c %c %c\n",
			__func__, ptr, &ptr[c->end_of_headers],
			ptr[c->end_of_headers], ptr[c->end_of_headers + 1],
			ptr[c->end_of_headers + 2], ptr[c->end_of_headers + 3],
			ptr[c->end_of_headers + 4]);
		priv->request_ready = 1;
		break;
	}
	}
	return len;
}

/*
 * This must return 0 if we can close the connection, !0 otherwise
 */
int http_poll(struct tcp_server_socket_lwip_raw *r)
{
	struct http_lwip_client_priv *priv = r->client_priv;
	struct http_connection *c = priv->c;
	int ret = c->can_close;

	dfu_log("c = %p, can_close = %d\n", c, ret);
	if (ret)
		free_connection(c);
	return !ret;
}

void http_closed(struct tcp_server_socket_lwip_raw *r)
{
	struct http_lwip_client_priv *priv = r->client_priv;
	struct http_connection *c = priv->c;

	dfu_log("%s: freeing connection %p\n", __func__, c);
	free_connection(c);
}

static const struct tcp_server_socket_lwip_raw_ops http_socket_ops = {
	.accept = http_accept,
	.recv = http_recv,
	.poll = http_poll,
	.closed = http_closed,
};

static struct tcp_server_socket_lwip_raw http_socket_raw = {
	.ops = &http_socket_ops,
};

/* DFU file rx methods */

static struct http_lwip_client_priv client_priv;

static int http_poll_idle(struct dfu_binary_file *bf)
{
	struct http_connection *c = client_priv.c;

	return c && (client_priv.request_ready || c->outgoing_data_len) ?
		DFU_FILE_EVENT : 0;
}

static int http_on_event(struct dfu_binary_file *bf)
{
	struct http_connection *c = client_priv.c;
	int stat, ret = 0;

	if (!client_priv.request_ready && !c->outgoing_data_len) {
		dfu_err("%s: BUG, request is not ready\n", __func__);
		return DFU_ERROR;
	}

	if (client_priv.request_ready && !client_priv.serving_request) {
		dfu_dbg("%s: request ready\n", __func__);
		c->outgoing_data = NULL;
		c->outgoing_data_len = 0;
		/*
		 * Process request may imply calling dfu_idle() again, so we
		 * reset the flag here and set it again if
		 * http_process_reequest() returns 0
		 */
		client_priv.request_ready = 0;
		client_priv.serving_request = 1;
		stat = http_process_request(c,
					    c->method,
					    c->method_len,
					    c->path,
					    c->path_len,
					    c->headers,
					    c->nheaders,
					    &c->request_buf[c->end_of_headers],
					    c->curr_request_index - 1 -
					    c->end_of_headers);
		switch (stat) {
		case HTTP_URL_PROCESSING:
			/* Request not finished, retry on next idle loop */
			client_priv.request_ready = 1;
			dfu_log("%s: request not processed\n", __func__);
			return 0;
		case HTTP_URL_TEMP_ERROR:
			/* Temporary error, retry later on */
			client_priv.request_ready = 1;
			client_priv.serving_request = 0;
			dfu_log("%s: temporary error on request\n", __func__);
			return 0;
		default:
			/* Fatal error */
			if (stat < 0)
				dfu_err("%s: fatal error\n", __func__);
			break;
		}
	} else if (c->outgoing_data_len) {
		/* outgoing_data_len > 0 */
		stat = http_async_send_data(c);
		if (stat < 0) {
			ret = DFU_ERROR;
			goto end;
		}
	}
	if (c->outgoing_data_len)
		/* Still some data to be sent */
		return 0;
end:
	dfu_log("%s: request done\n", __func__);
	client_priv.serving_request = 0;
	client_priv.request_ready = 0;
	c->can_close = 1;
	tcp_server_socket_lwip_raw_close(c->cd);
	http_reset_request_data(c);
	return ret;
}

static const struct dfu_binary_file_ops http_rx_method_ops = {
	.poll_idle = http_poll_idle,
	.on_event = http_on_event,
};

static int http_rx_init(struct dfu_binary_file *bf, void *arg)
{
	int stat;
	struct rx_method_http_lwip_data *data = arg;

	client_priv.bf = bf;
	http_socket_raw.client_priv = &client_priv;
	http_socket_raw.netif_idle = data->netif_idle_fun;
	http_socket_raw.netif = data->netif;
	stat = tcp_server_socket_lwip_raw_init(&http_socket_raw, 1080);
	bf->ops = &http_rx_method_ops;
	return stat;
}

static const struct dfu_file_rx_method_ops http_rx_ops = {
	.init = http_rx_init,
};

declare_file_rx_method(http, &http_rx_ops);

#endif /* LWIP_TCP */

#endif /* HAVE_LWIP */
