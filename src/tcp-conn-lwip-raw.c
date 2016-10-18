
#ifdef HAVE_LWIP

#include <dfu.h>
#include <dfu-internal.h>
#include "lwip/opt.h"
#include "lwip/debug.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include "tcp-conn-lwip-raw.h"

#ifdef LWIP_TCP

#define MAX_CONNECTIONS 4

enum tcp_conn_state
{
	ES_FREE = 0,
	ES_NONE,
	ES_CONNECTED,
	ES_CLOSING
};

struct tcp_conn_data
{
	enum tcp_conn_state state;
	struct tcp_server_socket_lwip_raw *raw_socket;
	struct tcp_pcb *pcb;
	uint16_t written;
	uint16_t acknowledged;
};

static struct tcp_conn_data connections[MAX_CONNECTIONS];

static struct tcp_conn_data *alloc_connection(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(connections); i++)
		if (connections[i].state == ES_FREE) {
			connections[i].state = ES_NONE;
			return &connections[i];
		}
	return NULL;
}

static void free_connection(struct tcp_conn_data *c)
{
	c->state = ES_FREE;
}

static void tcp_conn_free(struct tcp_conn_data *es)
{
	free_connection(es);
}

static void tcp_conn_close(struct tcp_pcb *tpcb, struct tcp_conn_data *es)
{
	tcp_arg(tpcb, NULL);
	tcp_sent(tpcb, NULL);
	tcp_recv(tpcb, NULL);
	tcp_err(tpcb, NULL);
	tcp_poll(tpcb, NULL, 0);
	tcp_conn_free(es);
	tcp_close(tpcb);
}

err_t tcp_conn_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	struct tcp_conn_data *es;
	struct tcp_server_socket_lwip_raw *r;
	struct pbuf *ptr;
	int stat = 0;

	LWIP_ASSERT("arg != NULL", arg);
	es = (struct tcp_conn_data *)arg;
	r = es->raw_socket;
	LWIP_ASSERT("raw_socket != NULL", r);
	
	if (err != ERR_OK) {
		if (p)
			/* cleanup, for unkown reason */
			pbuf_free(p);
		return err;
	}

	if (!p) {
		/* remote host closed connection */
		es->state = ES_CLOSING;
		if (r->ops->poll)
			stat = r->ops->poll(r);
		if(!stat)
			/* we're done sending, close it */
			tcp_conn_close(tpcb, es);
		return ERR_OK;
	}

	for (ptr = p; ; ptr = ptr->next) {
		tcp_recved(es->pcb, ptr->len);
		if (r->ops->recv) {
			stat = r->ops->recv(r, ptr->payload, ptr->len);
			if (stat < 0)
				break;
		}
		if (ptr->tot_len == ptr->len)
			break;
	}
	pbuf_free(p);
	return ERR_OK;
}

void tcp_conn_error(void *arg, err_t err)
{
	struct tcp_conn_data *es;

	LWIP_UNUSED_ARG(err);

	es = (struct tcp_conn_data *)arg;
	dfu_log("%s\n", __func__);

	tcp_conn_free(es);
}

static err_t tcp_conn_poll(void *arg, struct tcp_pcb *tpcb)
{
	struct tcp_conn_data *es;
	struct tcp_server_socket_lwip_raw *r;

	es = (struct tcp_conn_data *)arg;
	if (!es || !es->raw_socket) {
		/* nothing to be done */
		tcp_abort(tpcb);
		return ERR_ABRT;
	}

	r = es->raw_socket;
	if (!r->ops->poll || !r->ops->poll(r))
		tcp_conn_close(tpcb, es);

	return ERR_OK;
}

static err_t tcp_conn_sent(void *arg, struct tcp_pcb *tpcb, u16_t len)
{
	struct tcp_conn_data *es;
	struct tcp_server_socket_lwip_raw *r;

	LWIP_UNUSED_ARG(len);

	es = arg;
	es->acknowledged += len;
	dfu_dbg("%s: written = %u, acknowledged = %u\n", __func__,
		es->written, es->acknowledged);
	r = es->raw_socket;
	if (r->ops->sent)
		r->ops->sent(r);
	return ERR_OK;
}

static err_t tcp_conn_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	struct tcp_conn_data *es;
	struct tcp_server_socket_lwip_raw *r = arg;
	int stat = 0;

	if ((err != ERR_OK) || (newpcb == NULL) || !r)
		return ERR_VAL;
	/*
	 * Unless this pcb should have NORMAL priority, set its priority now.
	 * When running out of pcbs, low priority pcbs can be aborted to create
	 * new pcbs of higher priority.
	 */
	tcp_setprio(newpcb, TCP_PRIO_MIN);

	es = alloc_connection();
	if (!es)
		return ERR_MEM;
	es->state = ES_CONNECTED;
	es->pcb = newpcb;
	es->raw_socket = r;
	es->acknowledged = es->written = 0;
	if (r->ops->accept)
		stat = r->ops->accept(r, es);
	if (stat) {
		free_connection(es);
		return ERR_MEM;
	}
	/* pass newly allocated es to our callbacks */
	tcp_arg(newpcb, es);
	tcp_recv(newpcb, tcp_conn_recv);
	tcp_err(newpcb, tcp_conn_error);
	tcp_poll(newpcb, tcp_conn_poll, 0);
	tcp_sent(newpcb, tcp_conn_sent);
	tcp_nagle_disable(newpcb);
	return ERR_OK;
}

int tcp_server_socket_lwip_raw_init(struct tcp_server_socket_lwip_raw *r,
				    unsigned short port)
{
	err_t err;
	struct tcp_pcb *tcp_conn_pcb;

	if (!r->ops) {
		dfu_err("%s: ops is NULL\n", __func__);
		return -1;
	}
	tcp_conn_pcb = tcp_new();
	if (!tcp_conn_pcb) {
		dfu_err("%s: tcp_new() returns error\n", __func__);
		return -1;
	}
	tcp_arg(tcp_conn_pcb, r);
	dfu_log("%s: binding to port %u\n", __func__, port);
	err = tcp_bind(tcp_conn_pcb, IP_ADDR_ANY, port);
	if (err != ERR_OK) {
		dfu_err("%s: tcp_bind() to port %u returns error %d\n", __func__, port, err);
		return -1;
	}
	tcp_conn_pcb = tcp_listen(tcp_conn_pcb);
	if (!tcp_conn_pcb) {
		dfu_err("%s: tcp_listen() returns NULL\n", __func__);
		return -1;
	}
	tcp_accept(tcp_conn_pcb, tcp_conn_accept);
	return 0;
}

extern void my_lwip_idle(void);

int tcp_server_socket_lwip_raw_send(struct tcp_conn_data *es,
				    const void *buf, unsigned int len)
{
	err_t stat;
	uint16_t l;
	const char *ptr = buf;

	es->written = 0;
	do {

		l = min(len - es->written, tcp_sndbuf(es->pcb));
		if (!l) {
			dfu_log("%s: no more space in output buffer\n",
				__func__);
			return es->written;
		}
		dfu_log("%s: tcp_write(%p, len = %u, written = %d, l = %d)\n",
			__func__, &ptr[es->written], len, es->written, l);
		stat = tcp_write(es->pcb, &ptr[es->written], l,
				 TCP_WRITE_FLAG_COPY);
		if (stat != ERR_OK) {
			dfu_err("%s: tcp_write returned error\n", __func__);
			break;
		}
		es->written += l;
		dfu_log("%s: written = %d\n", __func__, es->written);
		if (es->written >= len)
			break;
		stat = tcp_output(es->pcb);
	} while(stat == ERR_OK);

	return stat == ERR_OK ? es->written : -1;
}

int tcp_server_socket_lwip_raw_close(struct tcp_conn_data *es)
{
	return tcp_close(es->pcb);
}


#endif /* LWIP_TCP */

#endif /* HAVE_LWIP */
