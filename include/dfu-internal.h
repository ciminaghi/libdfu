#ifndef __DFU_INTERNAL_H__
#define __DFU_INTERNAL_H__

struct dfu_format;

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
	struct dfu_format_ops *format_ops;
	void *host_data;
};

struct dfu_host_ops {
	int (*init)(struct dfu_host *);
	void (*udelay)(struct dfu_host *, unsigned long us);
	int (*log)(struct dfu_host *, const char *);
	void (*idle)(struct dfu_host *);
	void (*start_file_rx)(struct dfu_host *, const char *method);
};

struct dfu_format_ops {
	/*
	 * Returns zero if start_buf contains the beginning of a file encoded
	 * with this format
	 */
	int (*probe)(struct dfu_format *, const void *start_buf,
		     unsigned long buf_size);
	/*
	 * Decode file chunk. in_sz is a pointer because it is written
	 * with the actual number of decoded input bytes
	 * out_sz contains max output buffer length in input and is filled
	 * with actual number of bytes in out_buf
	 */
	int (*decode_chunk)(struct dfu_format *, const void *in_buf,
			    unsigned long *in_sz, void *out_buf,
			    unsigned long *addr, unsigned long *out_sz);
};


struct dfu_interface {
	const struct dfu_interface_ops *ops;
	void *priv;
};

struct dfu_target {
	struct dfu_interface *interface;
	const struct dfu_target_ops *ops;
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

extern int dfu_log(struct dfu_data *data, const char *, ...);
extern int dfu_udelay(struct dfu_data *data, unsigned long us);

extern struct dfu_format_ops *dfu_find_format(const void *start_bfu,
					      unsigned long buf_size);

#endif /* __DFU_INTERNAL_H__ */
