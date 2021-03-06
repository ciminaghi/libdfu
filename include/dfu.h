#ifndef __DFU_H__
#define __DFU_H__

/*
 * libdfu main interface header
 * LGPL v2.1
 * Copyright Arduino S.r.l. 2016
 * Copyright What’s Next GmbH 2017
 * Author Davide Ciminaghi 2016 2017
 */

#include "dfu-host.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dfu_interface;
struct dfu_target;
struct dfu_host;
struct dfu_data;
struct dfu_binary_file;
struct dfu_file_container;

struct dfu_target_ops;
struct dfu_interface_ops;
struct dfu_host_ops;

#define DFU_INTERFACE_EVENT	1
#define DFU_FILE_EVENT		2
#define DFU_TIMEOUT		4

struct dfu_file_rx_method_ops {
	int (*init)(struct dfu_binary_file *, void *arg);
	void (*done)(struct dfu_binary_file *, int status);
	int (*fini)(struct dfu_binary_file *);
};

struct dfu_file_rx_method {
	const struct dfu_file_rx_method_ops *ops;
	const char *name;
	void *priv;
};


struct dfu_binary_file_ops {
	/*
	 * Invoked on idle if host has no idle operation.
	 * Must return DFU_FILE_EVENT in case an event has occurred for the
	 * file
	 */
	int (*poll_idle)(struct dfu_binary_file *);
	/*
	 * This is invoked when an event has been detected on the file
	 */
	int (*on_event)(struct dfu_binary_file *);
};

/* This represents an open file */
struct dfu_simple_file;


struct dfu_simple_file_ops {
	int (*close)(struct dfu_simple_file *);
	int (*read)(struct dfu_simple_file *, char *buf, unsigned long sz);
	int (*write)(struct dfu_simple_file *, const char *buf,
		     unsigned long sz);
	int (*seek)(struct dfu_simple_file *, unsigned long fileptr);
};

struct dfu_simple_file {
	const char *path;
	const struct dfu_simple_file_ops *ops;
	unsigned long fileptr;
	void *priv;
};

/*
 * File container operation: this is like a filesystem with no directories
 *
 * @init: initialize (@path and @data are optional)
 * @fini: finalize
 * @open_file: open file with given name.
 * @remove_file: delete file with given name
 */	
struct dfu_file_container_ops {
	int (*init)(struct dfu_file_container *, const void *args);
	int (*fini)(struct dfu_file_container *);
	int (*open_file)(struct dfu_file_container *,
			 struct dfu_simple_file *,
			 const char *name, int create_if_not_found,
			 unsigned long max_size);
	int (*remove_file)(struct dfu_file_container *, const char *name);
};

extern struct dfu_file_container_ops posix_fc_ops;
extern struct dfu_file_container_ops spi_flash_fc_ops;

/* Placed here to know the size of it */
struct spi_flash_sector {
	struct spi_flash_sector *prev;
	struct spi_flash_sector *next;
};

struct dfu_fc_esp8226_args {
	unsigned long nsectors;
	unsigned long flash_first_sector;
	unsigned long sectsize;
	struct spi_flash_sector *sectors;
};


extern struct dfu_data *dfu_init(const struct dfu_interface_ops *iops,
				 const char *interface_path,
				 const void *interface_pars,
				 int (*start_cb)(void *),
				 void *start_cb_data,
				 const struct dfu_target_ops *tops,
				 const void *target_pars,
				 const struct dfu_host_ops *hops,
				 const struct dfu_file_container_ops *fcops,
				 const void *fcargs);

/*
 * Finalize and free all dfu data (dfu itself, target, interface, ...),
 * except for the binary file
 */
extern int dfu_fini(struct dfu_data *);

extern struct dfu_binary_file *
dfu_binary_file_start_rx(struct dfu_file_rx_method *method,
			 struct dfu_data *data,
			 void *method_arg);

/*
 * If totsz == 0, total size is unknown
 * addr is the starting load address, not needed if file format is not
 * binary (so load addr is encoded in the file itself).
 */
extern struct dfu_binary_file *
dfu_new_binary_file(const void *buf,
		    unsigned long buf_sz,
		    unsigned long totsz,
		    struct dfu_data *dfu,
		    unsigned long addr,
		    const struct dfu_binary_file_ops *,
		    void *priv);

/*
 * Append buf to file's buffer. Data are appended only if the whole buffer
 * fits, otherwise 0 is returned. On success, the number of appended bytes
 * is returned. -1 is returned on error.
 * If file is being flushed (dfu_binary_file_written() already called),
 * appending data also triggers data flush to target.
 */
extern int dfu_binary_file_append_buffer(struct dfu_binary_file *,
					 const void *buf,
					 unsigned long buf_sz);

extern int dfu_binary_file_flush_start(struct dfu_binary_file *);

extern int dfu_binary_file_written(struct dfu_binary_file *);

extern int dfu_binary_file_get_tot_appended(struct dfu_binary_file *);

extern void *dfu_binary_file_get_priv(struct dfu_binary_file *);

extern int dfu_binary_file_fini(struct dfu_binary_file *);

extern int dfu_set_binary_file_event(struct dfu_data *, void *event_data);

extern int dfu_set_interface_event(struct dfu_data *, void *event_data);

extern int dfu_target_reset(struct dfu_data *dfu);

extern int dfu_target_probe(struct dfu_data *dfu);

extern int dfu_target_go(struct dfu_data *dfu);

extern int dfu_target_erase_all(struct dfu_data *dfu);


/*
 * File rx methods
 */
extern struct dfu_file_rx_method dfu_rx_method_http;
extern struct dfu_file_rx_method dfu_rx_method_tftp;
extern struct dfu_file_rx_method dfu_rx_method_http_arduino;

#define DFU_ALL_DONE 1
#define DFU_CONTINUE 2
#define DFU_ERROR   -1

extern int dfu_idle(struct dfu_data *dfu);

#ifndef dfu_log
#error HOST MUST DEFINE A dfu_log MACRO
#endif

#ifndef dfu_err
#error HOST MUST DEFINE A dfu_err MACRO
#endif

#ifndef DEBUG
#define dfu_dbg(a,args...)
#else
#define dfu_dbg(a,args...) dfu_log(a, ##args)
#endif

/*
 * Rx methods private data structures
 */
struct netif;

struct rx_method_http_lwip_data {
	void (*netif_idle_fun)(struct netif *);
	struct netif *netif;
};


/*
 * Only parity is supported at the moment
 */
struct dfu_serial_pars {
#define PARITY_NONE 0
#define PARITY_EVEN 1
#define PARITY_ODD  2
	uint8_t parity;
};

#ifdef __cplusplus
}
#endif

#endif /* __DFU_H__ */

