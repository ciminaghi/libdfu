
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-host.h"

#ifndef CONFIG_BINARY_FILE_BUFSIZE
#define CONFIG_BINARY_FILE_BUFSIZE 2048
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

	if (!bf_count(bf))
		/* Nothing to flush */
		return 0;

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
	dfu_dbg("%s: chunk decoded, addr = 0x%08x\n", __func__,
		(unsigned int)addr);
	bf->curr_addr = addr;
	bf->curr_decoded_len = stat;
	bf->decoded_buf_busy++;
	dfu_dbg("%s %d, stat = %d, dfu_target_busy = %d\n",
		__func__, __LINE__, stat, dfu_target_busy(bf->dfu->target));
	if (stat && !dfu_target_busy(bf->dfu->target)) {
		stat = tops->chunk_available(bf->dfu->target, addr,
					     bf_decoded_buf, stat);
		if (stat < 0 && bf->decoded_buf_busy)
			bf->decoded_buf_busy--;
	}
	return stat < 0 ? stat : 0;
}

static int _bf_append_data(struct dfu_binary_file *bf, const void *buf,
			   unsigned long buf_sz)
{
	int sz, tot = 0, ret, stat = 0;
	char *ptr = bf->buf;

	if (!buf_sz) {
		/* size is 0, file written */
		bf->written = 1;
		return 0;
	}
	if (bf_space(bf) < buf_sz) {
		ret = 0;
		goto end;
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
		stat = _bf_do_flush(bf);
	return stat < 0 ? stat : ret;
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

struct dfu_binary_file *
dfu_binary_file_start_rx(struct dfu_file_rx_method *method,
			 struct dfu_data *dfu,
			 void *method_arg)
{
	struct dfu_binary_file *out = NULL;

	if (!method || !method->ops)
		return out;
	out = dfu_new_binary_file(NULL, 0, 0, dfu, 0, NULL, NULL);
	if (!out)
		return out;
	if (method->ops->init(out, method_arg) < 0) {
		_bf_fini(out, dfu);
		return NULL;
	}
	out->rx_method = method;
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
int dfu_binary_file_target_ready(struct dfu_binary_file *f)
{
	const struct dfu_target_ops *tops = f->dfu->target->ops;
	int stat;

	if (!f->decoded_buf_busy)
		/* Nothing to do */
		return 0;

	stat = tops->chunk_available(f->dfu->target, f->curr_addr,
				     bf_decoded_buf, f->curr_decoded_len);
	if (stat < 0)
		return stat;
	return stat;
}

/* Target is telling us that a chunk is done */
void dfu_binary_file_chunk_done(struct dfu_binary_file *bf,
				phys_addr_t chunk_addr, int status)
{
	dfu_dbg("%s, status = %d\n", __func__, status);
	if (status) {
		/* ERROR: do nothing, the target will signal this */
		return;
	}
	dfu_log_noprefix(".");
	if (bf->written)
		bf->really_written = 1;
	if (bf->decoded_buf_busy) {
		dfu_dbg("freeing decoded buffer\n");
		bf->decoded_buf_busy--;
	}
}

void dfu_binary_file_on_idle(struct dfu_binary_file *bf)
{
	if (!bf)
		return;
	if (!bf->decoded_buf_busy && bf->flushing)
		_bf_do_flush(bf);
}
