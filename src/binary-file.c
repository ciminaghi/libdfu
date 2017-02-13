
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-host.h"

#ifndef CONFIG_BINARY_FILE_BUFSIZE
#define CONFIG_BINARY_FILE_BUFSIZE 2048
#endif

#ifndef CONFIG_DECODED_BINARY_FILE_BUFSIZE
#define CONFIG_DECODED_BINARY_FILE_BUFSIZE 2048
#endif

static char bf_buf[CONFIG_BINARY_FILE_BUFSIZE];
static char bf_decoded_buf[CONFIG_DECODED_BINARY_FILE_BUFSIZE];

static struct dfu_binary_file bfile;

static int _bf_init(struct dfu_binary_file *bf, char *b, char *db,
		    int db_size, struct dfu_data *dfu)
{
	struct dfu_target *tgt = dfu ? dfu->target : NULL;
	int cs;

	bf->buf = b;
	bf->head = bf->tail = 0;
	bf->decoded_buf = db;
	bf->decoded_head = bf->decoded_tail = bf->write_tail = 0;
	bf->decoded_size = db_size;
	bf->write_chunk_size = db_size;
	if (tgt && tgt->ops->get_write_chunk_size) {
		/*
		 * If target asks for a fixed chunk size, actual decoded
		 * buffer size must be an integer multiple of chunk
		 * size: we don't want wrap arounds in the middle of a
		 * flash page
		 */
		cs = tgt->ops->get_write_chunk_size(tgt);
		if (cs > db_size) {
			dfu_err("%s: chunk size is too big\n", __func__);
			return -1;
		}
		bf->write_chunk_size = cs;
		bf->decoded_size = (db_size / cs) * cs;
	}
	bf->write_chunks_head = bf->write_chunks_tail = 0;
	memset(bf->write_chunks, 0, sizeof(bf->write_chunks));
	bf->written = 0;
	bf->really_written = 0;
	bf->rx_done = 0;
	bf->flushing = 0;
	bf->format_data = NULL;
	bf->format_ops = NULL;
	bf->rx_method = NULL;
	bf->max_size = sizeof(bf_buf);
	bf->tot_appended = 0;
	bf->dfu = dfu;
	if (dfu)
		dfu->bf = bf;
	bf->format_data = NULL;
	bf->priv = NULL;
	dfu_cancel_timeout(&bf->rx_timeout);
	return 0;
}

static void _bf_fini(struct dfu_binary_file *bf, struct dfu_data *dfu)
{
	_bf_init(bf, NULL, NULL, 0, dfu);
}

int dfu_binary_file_fini(struct dfu_binary_file *bf)
{
	int ret = 0;

	if (!bf)
		return -1;
	if (bf->rx_method && bf->rx_method->ops->fini) {
		ret = bf->rx_method->ops->fini(bf);
		if (ret < 0)
			return ret;
	}
	if (bf->format_ops && bf->format_ops->fini) {
		ret = bf->rx_method->ops->fini(bf);
		if (ret < 0)
			return ret;
	}
	_bf_fini(bf, NULL);
	return 0;
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

/*
 * Enqueue @len bytes for writing, starting from physical address @addr
 */
static int bf_enqueue_for_writing(struct dfu_binary_file *bf, int len,
				  phys_addr_t addr)
{
	struct dfu_write_chunk *wc;
	/* Ignore chunk alignment */
	int ign_al = bf->dfu->target->ops->ignore_chunk_alignment &&
		bf->dfu->target->ops->ignore_chunk_alignment(bf->dfu->target);

	dfu_dbg("%s: len = %d, addr = 0x%08x\n", __func__, len,
		(unsigned int)addr);
	do {
		wc = NULL;
		if (!ign_al && bf_wc_count(bf)) {
			/*
			 * Check whether last write chunk before head is
			 * pending
			 */
			int w = bf->write_chunks_head - 1;

			if (w < 0)
				w = ARRAY_SIZE(bf->write_chunks) - 1;
			if (bf->write_chunks[w].pending)
				wc = &bf->write_chunks[w];
		}
		if (!wc)
			wc = bf_get_write_chunk(bf);
		if (!wc) {
			dfu_log("%s: no write chunk available\n", __func__);
			/* No write chunks available */
			return -1;
		}
		if (wc->pending && addr == wc->addr + wc->len) {
			int l;
			
			/* Pending contiguous chunk */
			dfu_dbg("%s: pending contiguous chunk\n", __func__);
			if (wc->len + len <= bf->write_chunk_size) {
				/* No further chunks needed */
				wc->len += len;
				dfu_dbg("%s: new length = %d\n", __func__,
					wc->len);
				if (wc->len == bf->write_chunk_size) {
					dfu_dbg("%s: resetting pending flag\n",
						__func__);
					wc->pending = 0;
				}
				bf->write_tail = (bf->write_tail + len) &
				    (bf->decoded_size - 1);
				return 0;
			}
			/* Further chunk(s) needed */
			dfu_dbg("%s: further chunks needed\n", __func__);
			l = bf->write_chunk_size - wc->len;
			len -= l;
			/* write chunk filled, mark it as no more pending */
			wc->len = bf->write_chunk_size;
			wc->pending = 0;
			bf->write_tail = (bf->write_tail + l) &
				(bf->decoded_size - 1);
			addr += l;
			continue;
		}
		if (wc->pending) {
			/*
			 * Pending non-contiguous chunk: close pending chunk
			 * and get a new one
			 */
			dfu_dbg("%s: pending non-contiguous chunk\n", __func__);
			wc->pending = 0;
			continue;
		}
		/* New chunk */
		wc->start = bf->write_tail;
		wc->len = min(len, bf->write_chunk_size);
		if (ign_al)
			/*
			 * In case alignment is ignored, we're not guaranteed
			 * against a write chunk wrapping over, so let's limit
			 * length to the end of the decoded buffer
			 */
			wc->len = min(wc->len, bf->decoded_size - wc->start);
		wc->addr = addr;
		if (bf->write_chunk_size && !ign_al &&
		    (wc->addr & (bf->write_chunk_size - 1))) {
			/* This cannot happen, it is an error */
			dfu_err("%s %d: UNALIGNED wc ADDR\n",
				__func__, __LINE__);
			dfu_err("len = %d, write_chunk_size = %d, addr = 0x%08x\n", len, bf->write_chunk_size, (unsigned int)addr);
			return -1;
		}
		/*
		 * We can't have pending chunks in case alignment is ignored.
		 * In such case write_chunk_size is just a maximum chunk size,
		 * we can write less
		 */
		wc->pending = !ign_al && (wc->len < bf->write_chunk_size);
		dfu_dbg("%s: new chunk: start = %d, len = %d, addr = 0x%08x, pending = %d\n", __func__, wc->start, wc->len, (unsigned int)wc->addr,
			wc->pending);
		len -= wc->len;
		addr += wc->len;
		bf->write_tail = (bf->write_tail + wc->len) &
		    (bf->decoded_size - 1);
		dfu_dbg("%s remaining length = %d\n", __func__, len);
	} while(len);
	return 0;
}

static void _bf_rx_timeout(struct dfu_data *dfu, const void *data)
{
	dfu_err("BINARY FILE RX TIMEOUT\n");
	dfu_notify_error(dfu);
}

static void _set_rx_timeout(struct dfu_binary_file *bf, int moveit)
{
	if (moveit)
		dfu_cancel_timeout(&bf->rx_timeout);
	bf->rx_timeout.timeout = 10000;
	bf->rx_timeout.cb = _bf_rx_timeout;
	bf->rx_timeout.priv = NULL;
	if (dfu_set_timeout(bf->dfu, &bf->rx_timeout) < 0)
		dfu_err("WARNING: unable to set rx timeout\n");
}

/* Send first available write chunk to target for writing */
static int _bf_do_write(struct dfu_binary_file *bf)
{
	struct dfu_target *tgt = bf->dfu->target;
	const struct dfu_target_ops *tops = tgt->ops;
	int stat;
	struct dfu_write_chunk *wc;

	if (dfu_target_busy(tgt))
		/* Target is busy */
		return 0;
	/*
	 * Get next non-pending write chunk. Be happy with a pending chunk
	 * if this is the last one
	 */
	wc = bf_next_write_chunk(bf, bf->written && bf_wc_count(bf) == 1);
	if (!wc)
		/* Nothing to write */
		return 0;

	if (tops->must_erase && tops->must_erase(tgt, wc->addr, wc->len)) {
		wc->write_pending = 0;
		/* Must erase sector */
		return 0;
	}

	dfu_dbg("%s: writing chunk %d @0x%08x, size = %d\n",
		__func__, (int)(wc - bf->write_chunks), (unsigned)wc->addr,
		wc->len);
	_set_rx_timeout(bf, 1);
	stat = tops->chunk_available(tgt,
				     wc->addr,
				     &((char *)bf->decoded_buf)[wc->start],
				     wc->len);
	if (stat < 0) {
		dfu_dbg("%s: error from chunk_available(), throwing away write cchunk\n", __func__);
		bf_put_write_chunk(bf);
	}
	return stat;
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

	if (!bf->format_ops) {
		if (_bf_find_format(bf) < 0)
			return -1;
		_set_rx_timeout(bf, 0);
	}
	if (bf_dec_space(bf) < 2 * bf->decoded_chunk_size) {
		dfu_dbg("no space enough in decode buffer\n");
		return 0;
	}

	stat = bf->format_ops->decode_chunk(bf, &addr);
	if (stat <= 0) {
		if (stat < 0)
			dfu_err("%s: error in decode_chunk\n", __func__);
		return stat;
	}
	dfu_dbg("%s: chunk decoded, addr = 0x%08x, len = %d\n", __func__,
		(unsigned int)addr, stat);
	if (stat > bf->decoded_chunk_size)
		/* Stay on the safe side */
		bf->decoded_chunk_size = stat;

	stat = bf_enqueue_for_writing(bf, stat, addr);
	if (stat < 0) {
		dfu_err("%s: error enqueueing\n", __func__);
		return -1;
	}
	return 0;
}

static int _bf_append_data(struct dfu_binary_file *bf, const void *buf,
			   unsigned long buf_sz)
{
	int sz, tot = 0, ret;
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
	if (_bf_init(&bfile, bf_buf, bf_decoded_buf,
		     sizeof(bf_decoded_buf), dfu) < 0)
		return NULL;
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
	return 0;
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
	/* Free written chunk */
	bf_put_write_chunk(bf);
	/* All done ? */
	if (bf->written && !bf_wc_count(bf)) {
		struct dfu_interface *iface = bf->dfu->interface;

		bf->really_written = 1;
		dfu_cancel_timeout(&bf->rx_timeout);
		if (bf->rx_method->ops->done)
			bf->rx_method->ops->done(bf, status);
		if (iface->ops->done)
			iface->ops->done(iface);
	}
}

int dfu_binary_file_on_idle(struct dfu_binary_file *bf)
{
	if (!bf)
		return 0;
	if (_bf_do_write(bf) < 0)
		return -1;
	if (bf->flushing)
		return _bf_do_flush(bf);
	return 0;
}
