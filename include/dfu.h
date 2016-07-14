#ifndef __DFU_H__
#define __DFU_H__

/*
 * libdfu main interface header
 * LGPL v2.1
 * Copyright Arduino S.r.l.
 * Author Davide Ciminaghi 2016
 */

#include "dfu-host.h"

struct dfu_interface;
struct dfu_target;
struct dfu_host;
struct dfu_data;
struct dfu_binary_file;

struct dfu_target_ops;
struct dfu_interface_ops;
struct dfu_host_ops;


extern struct dfu_data *dfu_init(const struct dfu_interface_ops *iops,
				 const char *interface_path,
				 const void *interface_pars,
				 const struct dfu_target_ops *tops,
				 const struct dfu_host_ops *hops);


extern struct dfu_binary_file *dfu_binary_file_start_rx(const char *method,
							struct dfu_data *data);

/*
 * If totsz == 0, total size is unknown
 * addr is the starting load address, not needed if file format is not
 * binary (so load addr is encoded in the file itself).
 */
extern struct dfu_binary_file *dfu_new_binary_file(const void *buf,
						   unsigned long buf_sz,
						   unsigned long totsz,
						   struct dfu_data *dfu,
						   unsigned long addr);

extern int dfu_binary_file_append_buffer(struct dfu_binary_file *,
					 const void *buf,
					 unsigned long buf_sz);

extern int dfu_binary_file_flush_start(struct dfu_binary_file *);

extern int dfu_binary_file_written(struct dfu_binary_file *);

extern int dfu_target_reset(struct dfu_data *dfu);

extern int dfu_target_go(struct dfu_data *dfu);

extern void dfu_idle(struct dfu_data *dfu);

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


#endif /* __DFU_H__ */

