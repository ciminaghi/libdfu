#ifndef __ARDUINO_SERVER_H__
#define __ARDUINO_SERVER_H__

/*
 * Header file for arduino http server functions. Only build under ARDUINO
 */
#ifdef ARDUINO

#ifdef __cplusplus
extern "C" {
#endif

struct arduino_server_data {
	int chunk_ready;
	int first_chunk;
	int last_chunk;
	int error;
	const void *chunk_ptr;
	int chunk_len;
};

extern int arduino_server_send(int code, const char *msg);
extern int arduino_server_poll(void);
extern void arduino_server_ack(void);
extern void arduino_server_get_data(struct arduino_server_data *);
extern int arduino_server_init(void *arg);
extern void arduino_server_fini(void);


#ifdef __cplusplus
}
#endif

#endif /* ARDUINO */

#endif /* __ARDUINO_SERVER_H__ */
