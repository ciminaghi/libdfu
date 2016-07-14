#ifndef __DFU_INTERNAL_H__
#define __DFU_INTERNAL_H__

#include "dfu-host.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif /* ARRAY_SIZE */

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif /* min */

typedef void (*dfu_interface_rx_cb)(struct dfu_interface *, int sz, void *priv);

struct dfu_interface_ops {
	int (*open)(struct dfu_interface *, const char *path, const void *pars);
	int (*write)(struct dfu_interface *, const char *, unsigned long size);
	int (*read)(struct dfu_interface *, char *, unsigned long size);
	/* Do hw reset for target (maybe gpio ?) */
	int (*target_reset)(struct dfu_interface *);
};

struct dfu_target_ops {
	int (*init)(struct dfu_target *, struct dfu_interface *);
	int (*probe)(struct dfu_target *);
	/* Chunk of binary data is available for writing */
	int (*chunk_available)(struct dfu_target *,
			       unsigned long address,
			       const void *buf, unsigned long sz);
	/* Reset and sync target */
	int (*reset_and_sync)(struct dfu_target *);
	/* Let target run */
	int (*run)(struct dfu_target *);
};

struct dfu_binary_file_ops {
	int (*init)(struct dfu_binary_file *, unsigned long tot_size);
	int (*append_buf)(struct dfu_binary_file *, const void *,
			  unsigned long);
	int (*flush)(struct dfu_binary_file *);
};

struct dfu_binary_file {
	struct dfu_data *dfu;
	const struct dfu_format_ops *format_ops;
	void *buf;
	int head;
	int tail;
	int written;
	int max_size;
	void *format_data;
};

static inline int bf_count(struct dfu_binary_file *bf)
{
	return (bf->head - bf->tail) & (bf->max_size - 1);
}

static inline int bf_space(struct dfu_binary_file *bf)
{
	return (bf->tail - (bf->head + 1)) & (bf->max_size - 1);
}

static inline int bf_count_to_end(struct dfu_binary_file *bf)
{
	int end = bf->max_size - bf->tail;
	int n = (bf->head + end) & (bf->max_size - 1);
	return n < end ? n : end ;
}

static inline int bf_space_to_end(struct dfu_binary_file *bf)
{
	int end = bf->max_size - 1 - bf->head;
	int n = (end + bf->tail) & (bf->max_size - 1);
	return n <= end ? n : end + 1;
}


struct dfu_host_ops {
	int (*init)(struct dfu_host *);
	void (*udelay)(struct dfu_host *, unsigned long us);
	void (*idle)(struct dfu_host *);
	int (*start_file_rx)(struct dfu_binary_file *, const char *method);
	int (*file_flush_start)(struct dfu_binary_file *);
};

struct dfu_format_ops {
	/*
	 * Returns zero if start_buf contains the beginning of a file encoded
	 * with this format
	 */
	int (*probe)(struct dfu_binary_file *);
	/*
	 * Decode file chunk starting from current tail and update tail
	 * out_sz contains max output buffer length
	 * *addr is filled with start address of current chunk
	 * Returns number of bytes stored into out_buf
	 */
	int (*decode_chunk)(struct dfu_binary_file *, void *out_buf,
			    unsigned long out_sz, unsigned long *addr);
};

extern const struct dfu_format_ops registered_formats_start[],
    registered_formats_end[];

#define declare_dfu_format(n,p,d)					\
    static const struct							\
    dfu_format_ops format_ ## n						\
    __attribute__((section(".binary-formats"), used)) = {		\
	.probe = p,							\
	.decode_chunk = d,						\
    };

struct dfu_interface {
	const struct dfu_interface_ops *ops;
	void *priv;
};

struct dfu_target {
	struct dfu_interface *interface;
	const struct dfu_target_ops *ops;
	unsigned long entry_point;
	void *priv;
};

struct dfu_host {
	const struct dfu_host_ops *ops;
	void *priv;
};

struct dfu_data {
	struct dfu_interface *interface;
	struct dfu_target *target;
	struct dfu_host *host;
	int busy;
};

extern struct dfu_interface *
dfu_interface_init(const struct dfu_interface_ops *ops);

extern int dfu_interface_open(struct dfu_interface *, const char *name,
			      const void *params);

extern struct dfu_target *
dfu_target_init(const struct dfu_target_ops *ops);

extern int dfu_udelay(struct dfu_data *data, unsigned long us);

extern struct dfu_format_ops *dfu_find_format(const void *start_bfu,
					      unsigned long buf_size);

static inline void dfu_target_set_entry(struct dfu_data *dfu,
					unsigned long addr)
{
	dfu->target->entry_point = addr;
}

#endif /* __DFU_INTERNAL_H__ */
