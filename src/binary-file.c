
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-host.h"

#ifndef CONFIG_BINARY_FILE_BUFSIZE
#define CONFIG_BINARY_FILE_BUFSIZE 1024
#endif

#ifndef CONFIG_DECODED_BINARY_FILE_BUFSIZE
#define CONFIG_DECODED_BINARY_FILE_BUFSIZE 1024
#endif

static char bf_buf[CONFIG_BINARY_FILE_BUFSIZE];
static char bf_decoded_buf[CONFIG_DECODED_BINARY_FILE_BUFSIZE];

static struct dfu_binary_file bfile;

static void _bf_init(struct dfu_binary_file *bf, char *b, struct dfu_data *dfu)
{
	bf->buf = b;
	bf->head = bf->tail = 0;
	bf->written = 0;
	bf->really_written = 0;
	bf->format_data = NULL;
	bf->format_ops = NULL;
	bf->rx_method = NULL;
	bf->max_size = sizeof(bf_buf);
	bf->tot_appended = 0;
	bf->decoded_buf_busy = 0;
	bf->dfu = dfu;
	dfu->bf = bf;
}

static void _bf_fini(struct dfu_binary_file *bf, struct dfu_data *dfu)
{
	_bf_init(bf, NULL, dfu);
}

static int _bf_find_format(struct dfu_binary_file *bf)
{
	const struct dfu_format_ops *ptr;

	for (ptr = registered_formats_start;
	     ptr != registered_formats_end; ptr++) {
		if (!ptr->probe(bf)) {
			bf->format_ops = ptr;
			return 0;
		}
	}
	return -1;
}

/*
 * Decode chunk and start writing it
 * Assumes interface can write to target
 */
static int _bf_do_flush(struct dfu_binary_file *bf)
{
	const struct dfu_target_ops *tops = bf->dfu->target->ops;
	int stat;
	phys_addr_t addr;
	int max_chunk_size = sizeof(bf_decoded_buf);

	if (bf->decoded_buf_busy)
		return 0;

	if (!bf->format_ops)
		if (_bf_find_format(bf) < 0)
			return -1;

	if (tops->get_max_chunk_size)
		max_chunk_size = min(max_chunk_size,
				     tops->get_max_chunk_size(bf->dfu->target));

	stat = bf->format_ops->decode_chunk(bf, bf_decoded_buf,
					    max_chunk_size, &addr);
	if (stat < 0) {
		dfu_err("%s: error in decode_chunk\n", __func__);
		return -1;
	}
	bf->curr_addr = addr;
	bf->curr_decoded_len = stat;
	bf->decoded_buf_busy++;
	if (stat && !dfu_target_busy(bf->dfu->target)) {
		stat = tops->chunk_available(bf->dfu->target, addr,
					     bf_decoded_buf, stat);
		if (stat >= 0)
			bf->decoded_buf_busy--;
	}
	return stat < 0 ? stat : 0;
}

static int _bf_append_data(struct dfu_binary_file *bf, const void *buf,
			   unsigned long buf_sz)
{
	int sz, tot = 0, ret;
	char *ptr = bf->buf;

	if (!buf_sz) {
		/* size is 0, file written */
		bf->written = 1;
		return 0;
	}
	sz = min(bf_space_to_end(bf), buf_sz);
	if (sz <= 0) {
		ret = sz;
		goto end;
	}
	memcpy(&ptr[bf->head], buf, sz);
	bf->head = (bf->head + sz) & (ARRAY_SIZE(bf_buf) - 1);
	buf_sz -= sz;
	tot = sz;
	ret = tot;
	goto end;
	if (!buf_sz) {
		ret = sz;
		goto end;
	}
	sz = min(bf_space(bf), buf_sz);
	tot += sz;
	memcpy(&ptr[bf->head], buf, sz);
	bf->head = (bf->head + sz) & (ARRAY_SIZE(bf_buf) - 1);
	ret = tot;
end:
	bf->tot_appended += tot;
	dfu_dbg("%s: flushing = %d, appended = %d, tot_appended = %d\n",
		__func__, bf->flushing, tot, bf->tot_appended);
	if (bf->flushing)
		ret = _bf_do_flush(bf);
	return ret;
}

struct dfu_binary_file *
dfu_new_binary_file(const void *buf,
		    unsigned long buf_sz,
		    unsigned long totsz,
		    struct dfu_data *dfu,
		    unsigned long addr,
		    const struct dfu_binary_file_ops *ops,
		    void *priv)
{
	if (bfile.buf)
		/* Busy, one bfile at a time allowed at present */
		return NULL;
	_bf_init(&bfile, bf_buf, dfu);
	if (!buf || !buf_sz)
		return &bfile;
	if (_bf_append_data(&bfile, buf, buf_sz) < 0) {
		_bf_fini(&bfile, dfu);
		return NULL;
	}
	bfile.ops = ops;
	bfile.priv = priv;
	return &bfile;
}

static int _bf_find_rx_method(struct dfu_binary_file *bf, const char *name,
			      void *arg)
{
	const struct dfu_file_rx_method *ptr;

	for (ptr = registered_rx_methods_start;
	     ptr != registered_rx_methods_end; ptr++) {
		if (strcmp(ptr->name, name))
			continue;
		if (ptr->ops && ptr->ops->init(bf, arg) < 0)
			return -1;
		bf->rx_method = ptr;
		return 0;
	}
	return -1;
}

struct dfu_binary_file *dfu_binary_file_start_rx(const char *method,
						 struct dfu_data *dfu,
						 void *arg)
{
	struct dfu_binary_file *out = NULL;

	out = dfu_new_binary_file(NULL, 0, 0, dfu, 0, NULL, NULL);
	if (!out)
		return out;
	if (_bf_find_rx_method(out, method, arg) < 0) {
		_bf_fini(out, dfu);
		return NULL;
	}
	return out;
}

int dfu_binary_file_append_buffer(struct dfu_binary_file *f,
				  const void *buf,
				  unsigned long buf_sz)
{
	int cnt;

	/*
	 * Check whether the whole buffer can be appended
	 */
	dfu_dbg("%s: bf_space(f) = %u, buf_sz = %lu\n", __func__, bf_space(f),
		buf_sz);
	if (bf_space(f) < buf_sz)
		return 0;
	cnt = _bf_append_data(f, buf, buf_sz);
	if (cnt < 0)
		return cnt;
	return cnt;
}

int dfu_binary_file_flush_start(struct dfu_binary_file *bf)
{
	bf->flushing = 1;
	if (!bf->tot_appended)
		return 0;
	return _bf_do_flush(bf);
}

int dfu_binary_file_written(struct dfu_binary_file *f)
{
	return f->really_written;
}

int dfu_binary_file_get_tot_appended(struct dfu_binary_file *f)
{
	return f->tot_appended;
}

void *dfu_binary_file_get_priv(struct dfu_binary_file *f)
{
	return f->priv;
}

/*
 * Target is ready. If we have some decoded data in the relevant buffer,
 * write such data to target and free the buffer
 */
void dfu_binary_file_target_ready(struct dfu_binary_file *f)
{
	const struct dfu_target_ops *tops = f->dfu->target->ops;
	int stat;

	if (!f->decoded_buf_busy)
		/* Nothing to do */
		return;

	stat = tops->chunk_available(f->dfu->target, f->curr_addr,
				     bf_decoded_buf, f->curr_decoded_len);
	if (stat < 0)
		return;
	f->decoded_buf_busy--;
}

/* Target is telling us that a chunk is done */
void dfu_binary_file_chunk_done(struct dfu_binary_file *bf,
				phys_addr_t chunk_addr, int status)
{
	if (status) {
		/* ERROR: do nothing, the target will signal this */
		return;
	}
	dfu_log_noprefix(".");
	if (bf->written)
		bf->really_written = 1;
}
