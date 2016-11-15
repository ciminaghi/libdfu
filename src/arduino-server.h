#ifndef __ARDUINO_SERVER_H__
#define __ARDUINO_SERVER_H__

/*
 * Header file for arduino http server functions. Only build under ARDUINO
 */
#ifdef ARDUINO

#ifdef __cplusplus
extern "C" {
#endif

extern int arduino_server_send(int code, const char *msg);
extern int arduino_server_poll(int *chunk_ready, int *error, int *num_chunk,
			       int *tot_chunks);
extern int arduino_server_get_chunk(const void **ptr);
extern int arduino_server_init(void);


#ifdef __cplusplus
}
#endif

#endif /* ARDUINO */

#endif /* __ARDUINO_SERVER_H__ */
