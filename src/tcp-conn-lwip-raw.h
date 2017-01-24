#ifndef __TCP_CONN_LWIP_RAW_H__
#define __TCP_CONN_LWIP_RAW_H__

/* Forward decl */
struct tcp_server_socket_lwip_raw;

/* Opaque struct representing a tcp connection */
struct tcp_conn_data;

struct tcp_server_socket_lwip_raw_ops {
	/* Returns 0 if OK, -1 if ERROR */
	int (*accept)(struct tcp_server_socket_lwip_raw *,
		      struct tcp_conn_data *);
	/* Returns number of stored bytes or -1 if not enough memory */
	int (*recv)(struct tcp_server_socket_lwip_raw *, const void *buf,
		    unsigned int len);
	/* Invoked on bytes sent */
	int (*sent)(struct tcp_server_socket_lwip_raw *);
	/*
	 * Returns 0 if we can close connection, !0 if we still have stuff
	 * to send
	 */
	int (*poll)(struct tcp_server_socket_lwip_raw *);
	/* Invoked on actual connection close */
	void (*closed)(struct tcp_server_socket_lwip_raw *);
};

struct tcp_server_socket_lwip_raw {
	const struct tcp_server_socket_lwip_raw_ops *ops;
	void (*netif_idle)(struct netif *);
	struct netif *netif;
	void *client_priv;
};

int tcp_server_socket_lwip_raw_init(struct tcp_server_socket_lwip_raw *,
				    unsigned short port);

int tcp_server_socket_lwip_raw_send(struct tcp_conn_data *,
				    const void *buf, unsigned int len);

int tcp_server_socket_lwip_raw_close(struct tcp_conn_data *);

int tcp_server_socket_lwip_raw_abort(struct tcp_conn_data *);

int tcp_server_socket_lwip_raw_fini(struct tcp_server_socket_lwip_raw *);

#endif /* __TCP_CONN_LWIP_RAW_H__ */
