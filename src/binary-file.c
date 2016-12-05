
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
	bf->curr_write_size = 0;
	bf->curr_write_src = 0;
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
		if (!ptr->probe && !ptr->decode_chunk)
			/*
			 * Avoid using linker scripts under arduino, so
			 * we haven't a reliable registered_formats_end there.
			 * See arduino/build_src_zip
			 */
			return -1;
		if (!ptr->probe(bf)) {
			bf->format_ops = ptr;
			return 0;
		}
	}
	return -1;
}

/* Write a sub-chunk (possibly the whole decoded chunk) */
static void _bf_write_subchunk(struct dfu_binary_file *bf)
{
	struct dfu_target *tgt = bf->dfu->target;
	const struct dfu_target_ops *tops = tgt->ops;
	int stat;

	if (bf->curr_write_size)
		/* Already writing sub-chunk */
		return;
	if (dfu_target_busy(tgt))
		/* Target is busy */
		return;
	bf->curr_write_size = bf->curr_decoded_len;
	if (tops->get_max_chunk_size)
		bf->curr_write_size = min(bf->curr_decoded_len,
					  tops->get_max_chunk_size(tgt));
	dfu_dbg("%s: writing subchunk @0x%08x, size = %lu\n",
		__func__, bf->curr_addr, bf->curr_write_size);
	stat = tops->chunk_available(tgt,
				     bf->curr_addr,
				     bf->curr_write_src,
				     bf->curr_write_size);
	if (stat < 0 && bf->decoded_buf_busy) {
		dfu_dbg("%s: error from chunk_available(), throwing away buf\n",
			__func__);
		bf->curr_decoded_len = 0;
		bf->curr_write_size = 0;
		bf->decoded_buf_busy--;
	}
}

/*
 * Decode chunk and start writing it
 * Assumes interface can write to target
 */
static int _bf_do_flush(struct dfu_binary_file *bf)
{
	int stat;
	phys_addr_t addr;

	if (!bf_count(bf))
		/* Nothing to flush */
		return 0;

	if (bf->decoded_buf_busy)
		return 0;

	if (!bf->format_ops)
		if (_bf_find_format(bf) < 0)
			return -1;

	stat = bf->format_ops->decode_chunk(bf, bf_decoded_buf,
					    sizeof(bf_decoded_buf), &addr);
	if (stat <= 0) {
		if (stat < 0)
			dfu_err("%s: error in decode_chunk\n", __func__);
		return stat;
	}
	dfu_dbg("%s: chunk decoded, addr = 0x%08x\n", __func__,
		(unsigned int)addr);
	bf->curr_addr = addr;
	bf->curr_decoded_len = stat;
	bf->decoded_buf_busy++;
	/* We can start writing the subchunk immediately */
	bf->curr_write_size = 0;
	bf->curr_write_src = bf_decoded_buf;
	/* Start writing subchunks */
	_bf_write_subchunk(bf);
	return 0;
}

static int _bf_append_data(struct dfu_binary_file *bf, const void *buf,
			   unsigned long buf_sz)
{
	int sz, tot = 0, ret, stat = 0;
	char *ptr = bf->buf;
	const char *src = buf;

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
	memcpy(&ptr[bf->head], src, sz);
	bf->head = (bf->head + sz) & (ARRAY_SIZE(bf_buf) - 1);
	buf_sz -= sz;
	src += sz;
	tot = sz;
	ret = tot;
	if (!buf_sz) {
		ret = sz;
		goto end;
	}
	sz = min(bf_space(bf), buf_sz);
	tot += sz;
	memcpy(&ptr[bf->head], src, sz);
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
	/* Update curr_decoded_len and data src/dst pointers */
	bf->curr_decoded_len -= bf->curr_write_size;
	bf->curr_write_src += bf->curr_write_size;
	bf->curr_addr += bf->curr_write_size;
	/* Subchunk write is done, go on with next one if needed */
	bf->curr_write_size = 0;
	if (bf->curr_decoded_len) {
		_bf_write_subchunk(bf);
		return;
	}
	if (bf->written) {
		bf->really_written = 1;
		if (bf->rx_method->ops->done)
			bf->rx_method->ops->done(bf, status);
	}
	if (bf->decoded_buf_busy) {
		dfu_dbg("freeing decoded buffer\n");
		bf->decoded_buf_busy--;
	}
}

void dfu_binary_file_on_idle(struct dfu_binary_file *bf)
{
	if (!bf)
		return;
	if (bf->decoded_buf_busy)
		_bf_write_subchunk(bf);
	if (!bf->decoded_buf_busy && bf->flushing)
		_bf_do_flush(bf);
}
