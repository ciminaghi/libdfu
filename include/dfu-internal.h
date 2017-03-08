#ifndef __DFU_INTERNAL_H__
#define __DFU_INTERNAL_H__

#include "dfu-host.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif /* ARRAY_SIZE */

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif /* min */

#ifndef BIT
#define BIT(a) (1 << (a))
#endif

/* The following ones come from the kernel, but simplified */
#ifndef time_after
#define time_after(a,b)         \
        ((long)(b) - (long)(a) < 0)
#endif
#define time_before(a,b)        time_after(b,a)
#ifndef time_after_eq
#define time_after_eq(a,b)      \
         ((long)(a) - (long)(b) >= 0)
#endif
#define time_before_eq(a,b)     time_after_eq(b,a)

#define time_in_range(a,b,c) \
        (time_after_eq(a,b) && \
         time_before_eq(a,c))


/* 32 bits targets supported */
typedef uint32_t phys_addr_t;

typedef void (*dfu_interface_rx_cb)(struct dfu_interface *, int sz, void *priv);

struct dfu_interface_ops {
	int (*open)(struct dfu_interface *, const char *path, const void *pars);
	int (*write)(struct dfu_interface *, const char *, unsigned long size);
	int (*read)(struct dfu_interface *, char *, unsigned long size);
	int (*write_read)(struct dfu_interface *, const char *wr_buf,
			  char *rd_buf, unsigned long size);
	/* Do hw reset for target (maybe gpio ?) */
	int (*target_reset)(struct dfu_interface *);
	/* Optional: let the target run */
	int (*target_run)(struct dfu_interface *);
	/*
	 * Poll function, to be implemented in case interface event
	 * is missing. Returns DFU_INTERFACE_EVENT in case interface is ready
	 */
	int (*poll_idle)(struct dfu_interface *);
	/* Invoked on programming done */
	void (*done)(struct dfu_interface *);
	/* Finalization method */
	int (*fini)(struct dfu_interface *);
};

struct dfu_target_ops {
	int (*init)(struct dfu_target *, struct dfu_interface *);
	int (*probe)(struct dfu_target *);
	/*
	 * Chunk of binary data is available for writing
	 * buf is owned by the caller !
	 */
	int (*chunk_available)(struct dfu_target *,
			       phys_addr_t address,
			       const void *buf, unsigned long sz);
	/* Erase all memory */
	int (*erase_all)(struct dfu_target *);
	/* Reset and sync target */
	int (*reset_and_sync)(struct dfu_target *);
	/* Let target run */
	int (*run)(struct dfu_target *);
	/* Interface event */
	int (*on_interface_event)(struct dfu_target *);
	/* To be called on idle */
	int (*on_idle)(struct dfu_target *);
	/* Optional: returns mandatory size for write chunk (flash page size) */
	int (*get_write_chunk_size)(struct dfu_target *);
	/* Optional: returns true if chunk address alignment can be ignored */
	int (*ignore_chunk_alignment)(struct dfu_target *);
	/* Read memory area */
	int (*read_memory)(struct dfu_target *, void *buf, phys_addr_t addr,
			   unsigned long sz);
	/* Finalization method */
	int (*fini)(struct dfu_target *);
	/*
	 * Returns !0 if an erase operation must be performed to write @l
	 * bytes starting from @addr. If an erase must be performed, the
	 * operation is immediately started.
	 */
	int (*must_erase)(struct dfu_target *, phys_addr_t addr,
			  unsigned long l);
};

#ifndef CONFIG_MAX_CHUNKS
#define CONFIG_MAX_CHUNKS 32
#endif

/*
 * A chunk which can be written to flash (i.e. typically one or more flash
 * pages)
 */
struct dfu_write_chunk {
	phys_addr_t addr;
	int start;
	int len;
	/* !0 when a chunk is being filled */
	int pending;
	/* !0 when a chunk is being written */
	int write_pending;
};

struct dfu_timeout {
	int timeout;
	void (*cb)(struct dfu_data *, const void *);
	const void *priv;
};

struct dfu_binary_file {
	struct dfu_data *dfu;
	const struct dfu_format_ops *format_ops;
	const struct dfu_binary_file_ops *ops;
	const struct dfu_file_rx_method *rx_method;
	struct dfu_timeout rx_timeout;
	void *buf;
	void *decoded_buf;
	int head;
	int tail;
	/* All bytes flushed, still waiting for target to confirm write ok */
	int written;
	/* All bytes flushed and target said all bytes written */
	int really_written;
	int max_size;
	int rx_done;
	int flushing;
	int tot_appended;
	/* Head/tail of decoded buffer */
	/*
	 * The decoded buffer is managed as a strange circular buffer, with
	 * one head and two tails:
	 *
	 *                         decoded_head
	 *                               V
	 * +--------------------------------------+
	 * |                                      |
	 * |       decoded buffer                 |
	 * |                                      |
	 * +--------------------------------------+
	 *          ^          ^
	 *     decoded_tail  write_tail
	 *
	 * The binary format layer manages decoded_head (i.e. advances
	 * decoded_head every time a chunk is decoded).
	 * The binary file layer manages the two tails:
	 *
	 * + When at least a flash page is available between write_tail and
	 * decoded_head, it sets up all the write chunks needed for writing.
	 * Write chunk data structures are needed because each of them
	 * contains the physical address to which each chunk must be written.
	 * + When a write chunk has been written to flash, the binary file
	 * layer advances decoded_tail.
	 *
	 */
	int decoded_head;
	int decoded_tail;
	int write_tail;
	/* Actual size of decoded buffer */
	int decoded_size;
	/* Safe estimate of decoded chunk's size */
	int decoded_chunk_size;
	/* Size of a write chunk */
	int write_chunk_size;
	/* Decoded chunks data */
	struct dfu_write_chunk write_chunks[CONFIG_MAX_CHUNKS];
	/* Head/Tail of write chunks */
	int write_chunks_head;
	int write_chunks_tail;
	void *format_data;
	void *priv;
};

static inline int _count(int head, int tail, int size)
{
	return (head - tail) & (size - 1);
}

static inline int _space(int head, int tail, int size)
{
	return (tail - (head + 1)) & (size - 1);
}

static inline int _count_to_end(int head, int tail, int size)
{
	int end = size - tail;
	int n = (head + end) & (size - 1);
	return n < end ? n : end ;
}

static inline int _space_to_end(int head, int tail, int size)
{
	int end = size - 1 - head;
	int n = (end + tail) & (size - 1);
	return n <= end ? n : end + 1;
}

static inline int bf_count(struct dfu_binary_file *bf)
{
	return _count(bf->head, bf->tail, bf->max_size);
}

static inline int bf_space(struct dfu_binary_file *bf)
{
	return _space(bf->head, bf->tail, bf->max_size);
}

static inline int bf_count_to_end(struct dfu_binary_file *bf)
{
	return _count_to_end(bf->head, bf->tail, bf->max_size);
}

static inline int bf_space_to_end(struct dfu_binary_file *bf)
{
	return _space_to_end(bf->head, bf->tail, bf->max_size);
}

static inline int bf_dec_count(struct dfu_binary_file *bf)
{
	return _count(bf->decoded_head, bf->decoded_tail, bf->decoded_size);
}

static inline int bf_dec_space(struct dfu_binary_file *bf)
{
	return _space(bf->decoded_head, bf->decoded_tail, bf->decoded_size);
}

static inline int bf_dec_count_to_end(struct dfu_binary_file *bf)
{
	return _count_to_end(bf->decoded_head, bf->decoded_tail,
			     bf->decoded_size);
}

/* Returns number of bytes to be written */
static inline int bf_write_count_to_end(struct dfu_binary_file *bf)
{
	return _count_to_end(bf->decoded_head, bf->write_tail,
			     bf->decoded_size);
}

static inline int bf_dec_space_to_end(struct dfu_binary_file *bf)
{
	return _space_to_end(bf->decoded_head, bf->decoded_tail,
			     bf->decoded_size);
}

static inline int bf_wc_count(struct dfu_binary_file *bf)
{
	return _count(bf->write_chunks_head, bf->write_chunks_tail,
		      ARRAY_SIZE(bf->write_chunks));
}

static inline int bf_wc_space(struct dfu_binary_file *bf)
{
	return _space(bf->write_chunks_head, bf->write_chunks_tail,
		      ARRAY_SIZE(bf->write_chunks));
}

/*
 * Get a fresh write chunk.
 */
static inline struct dfu_write_chunk *
bf_get_write_chunk(struct dfu_binary_file *bf)
{
	struct dfu_write_chunk *out;

	if (!bf_wc_space(bf))
		return NULL;
	out = &bf->write_chunks[bf->write_chunks_head];
	bf->write_chunks_head++;
	bf->write_chunks_head &= (ARRAY_SIZE(bf->write_chunks) - 1);
	return out;
}

/*
 * Returns pointer to next write chunk ready for being passed on to target
 * and marks chunk as pending (for writing);
 * Does not advance tail, call bf_put_write_chunk instead when chunk has
 * actually been written
 */
static inline struct dfu_write_chunk *
bf_next_write_chunk(struct dfu_binary_file *bf, int ignore_pending)
{
	if (!bf_wc_count(bf))
		return NULL;
	/* First chunk to be written is already pending, nothing to do */
	if (bf->write_chunks[bf->write_chunks_tail].write_pending ||
	    (bf->write_chunks[bf->write_chunks_tail].pending &&
	     !ignore_pending))
		return NULL;
	bf->write_chunks[bf->write_chunks_tail].write_pending = 1;
	return &bf->write_chunks[bf->write_chunks_tail];
}


/*
 * Advances tail of write chunks, call this to free a write chunk when
 * target layer declares write operation done
 * Also updates decoded_tail.
 */
static inline void bf_put_write_chunk(struct dfu_binary_file *bf)
{
	struct dfu_write_chunk *wc = &bf->write_chunks[bf->write_chunks_tail];

	bf->decoded_tail = wc->start + wc->len;
	bf->decoded_tail &= (bf->decoded_size - 1);
	wc->pending = wc->write_pending = 0;
	bf->write_chunks_tail++;
	bf->write_chunks_tail &= (ARRAY_SIZE(bf->write_chunks) - 1);
}

struct dfu_host_ops {
	int (*init)(struct dfu_host *);
	void (*udelay)(struct dfu_host *, unsigned long us);
	/* Returns int with last events flags set */
	int (*idle)(struct dfu_host *, long next_timeout);
	int (*set_interface_event)(struct dfu_host *, void *);
	int (*set_binary_file_event)(struct dfu_host *, void *);
	unsigned long (*get_current_time)(struct dfu_host *);
	int (*fini)(struct dfu_host *);
};

struct dfu_format_ops {
	/*
	 * Returns zero if start_buf contains the beginning of a file encoded
	 * with this format
	 */
	int (*probe)(struct dfu_binary_file *);
	/*
	 * Decode file chunk starting from current tail and update tail
	 * Write decoded data into binary file's decoded [circular] buffer,
	 * and update decoded_head. Return number of bytes written into
	 * decoded buffer.
	 */
	int (*decode_chunk)(struct dfu_binary_file *, phys_addr_t *addr);
	/* Finalization method */
	int (*fini)(struct dfu_binary_file *);
};

#define declare_file_rx_method(n,o)				\
	struct dfu_file_rx_method dfu_rx_method_ ## n = {	\
		.name = #n,					\
		.ops = o,					\
	}

extern const struct dfu_format_ops registered_formats_start[],
    registered_formats_end[];

/*
 * Letting the linker automatically build binary formats tables is
 * complicated under the arduino build system. We work around this by
 * simply declaring pointers to operations and letting the
 * arduino/build_src_tar script do the rest.
 */
#ifndef ARDUINO
#define declare_dfu_format(n,p,d,f)					\
    static const struct							\
    dfu_format_ops format_ ## n						\
    __attribute__((section(".binary-formats"), used)) = {		\
	.probe = p,							\
	.decode_chunk = d,						\
	.fini = f,							\
    };
#else
#define declare_dfu_format(n,p,d,f)					\
    int (* n ## _probe_ptr)(struct dfu_binary_file *) = p;		\
    int (* n ## _decode_chunk_ptr)(struct dfu_binary_file *bf,		\
				   void *out_buf,			\
				   unsigned long out_sz,		\
				   phys_addr_t *addr) = d;		\
    int (* n ## _fini_ptr)(struct dfu_binary_file *) = f
#endif

struct dfu_interface {
	struct dfu_data *dfu;
	const struct dfu_interface_ops *ops;
	void *priv;
	int setup_done;
	const void *pars;
	const char *path;
	int (*start_cb)(void *);
	void *start_cb_data;
};

struct dfu_target {
	struct dfu_data *dfu;
	struct dfu_interface *interface;
	const struct dfu_target_ops *ops;
	int busy;
	unsigned long entry_point;
	const void *pars;
	void *priv;
};

static inline int dfu_target_busy(struct dfu_target *t)
{
	return t->busy;
}

static inline void dfu_target_set_busy(struct dfu_target *t)
{
	t->busy = 1;
}

static inline void dfu_target_set_ready(struct dfu_target *t)
{
	t->busy = 0;
}

struct dfu_host {
	const struct dfu_host_ops *ops;
	void *priv;
};

struct dfu_data {
	struct dfu_interface *interface;
	struct dfu_target *target;
	struct dfu_host *host;
	struct dfu_binary_file *bf;
	int busy;
	int error;
};

static inline void dfu_notify_error(struct dfu_data *dfu)
{
	dfu->error++;
}

static inline int dfu_error(struct dfu_data *dfu)
{
	return dfu->error;
}

extern int dfu_interface_open(struct dfu_interface *, const char *name,
			      const void *params);
extern int dfu_interface_fini(struct dfu_interface *);
extern int dfu_interface_poll_idle(struct dfu_interface *);
extern int dfu_interface_target_reset(struct dfu_interface *);
extern int dfu_interface_target_run(struct dfu_interface *);
extern int dfu_interface_read(struct dfu_interface *, char *, unsigned long);
extern int dfu_interface_write(struct dfu_interface *, const char *,
			       unsigned long);
extern int dfu_interface_write_read(struct dfu_interface *, const char *,
				    char *, unsigned long);


static inline int dfu_interface_has_fini(struct dfu_interface *iface)
{
	return iface->ops && iface->ops->fini;
}

static inline int dfu_interface_has_poll_idle(struct dfu_interface *iface)
{
	return iface->ops && iface->ops->poll_idle;
}

static inline int dfu_interface_has_target_reset(struct dfu_interface *iface)
{
	return iface->ops && iface->ops->target_reset;
}

static inline int dfu_interface_has_target_run(struct dfu_interface *iface)
{
	return iface->ops && iface->ops->target_run;
}

static inline int dfu_interface_has_write(struct dfu_interface *iface)
{
	return iface->ops && iface->ops->write;
}

static inline int dfu_interface_has_read(struct dfu_interface *iface)
{
	return iface->ops && iface->ops->read;
}

static inline int dfu_interface_has_write_read(struct dfu_interface *iface)
{
	return iface->ops && iface->ops->write_read;
}

extern struct dfu_target *
dfu_target_init(const struct dfu_target_ops *ops);

static inline int dfu_udelay(struct dfu_data *dfu, unsigned long us)
{
	struct dfu_host *h = dfu->host;

	if (!h->ops || !h->ops->udelay)
		return -1;
	h->ops->udelay(h, us);
	return 0;
}

extern struct dfu_format_ops *dfu_find_format(const void *start_bfu,
					      unsigned long buf_size);

static inline void dfu_target_set_entry(struct dfu_data *dfu,
					unsigned long addr)
{
	dfu->target->entry_point = addr;
}

extern int dfu_set_timeout(struct dfu_data *dfu, struct dfu_timeout *);

extern int dfu_cancel_timeout(struct dfu_timeout *);

extern unsigned long dfu_get_current_time(struct dfu_data *dfu);


/* To be invokedby target when a chunk has been written */
extern void dfu_binary_file_chunk_done(struct dfu_binary_file *,
				       phys_addr_t chunk_addr, int status);

extern int dfu_binary_file_on_idle(struct dfu_binary_file *bf);

extern int dfu_binary_file_fini(struct dfu_binary_file *bf);

#endif /* __DFU_INTERNAL_H__ */
