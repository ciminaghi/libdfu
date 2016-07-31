
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
	bf->format_data = NULL;
	bf->format_ops = NULL;
	bf->max_size = sizeof(bf_buf);
	bf->tot_appended = 0;
	bf->dfu = dfu;
	dfu->bf = bf;
}

static void _bf_fini(struct dfu_binary_file *bf, struct dfu_data *dfu)
{
	_bf_init(bf, NULL, dfu);
}

static int _bf_append_data(struct dfu_binary_file *bf, const void *buf,
			   unsigned long buf_sz)
{
	int sz, tot;
	char *ptr = bf->buf;

	sz = min(bf_space_to_end(bf), buf_sz);
	if (sz <= 0)
		return sz;
	memcpy(&ptr[bf->head], buf, sz);
	bf->head = (bf->head + sz) & (ARRAY_SIZE(bf_buf) - 1);
	buf_sz -= sz;
	if (!buf_sz)
		return sz;
	tot = sz;
	sz = min(bf_space_to_end(bf), buf_sz);
	tot += sz;
	memcpy(&ptr[bf->head], buf, sz);
	bf->head = (bf->head + sz) & (ARRAY_SIZE(bf_buf) - 1);
	return tot;
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

	stat = bf->format_ops->decode_chunk(bf, bf_decoded_buf,
					    sizeof(bf_decoded_buf), &addr);
	if (stat < 0)
		return -1;
	stat = tops->chunk_available(bf->dfu->target, addr,
				     bf_decoded_buf, stat);
	return stat < 0 ? stat : 0;
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
	if (_bf_find_format(&bfile) < 0) {
		_bf_fini(&bfile, dfu);
		return NULL;
	}
	bfile.ops = ops;
	bfile.priv = priv;
	return &bfile;
}

struct dfu_binary_file *dfu_binary_file_start_rx(const char *method,
						 struct dfu_data *dfu)
{
	struct dfu_binary_file *out = NULL;

	if (!dfu->host->ops->start_file_rx)
		return out;
	out = dfu_new_binary_file(NULL, 0, 0, dfu, 0, NULL, NULL);
	if (!out)
		return out;
	if (dfu->host->ops->start_file_rx(out, method) < 0) {
		_bf_fini(out);
		return NULL;
	}
	return out;
}

int dfu_binary_file_append_buffer(struct dfu_binary_file *f,
				  const void *buf,
				  unsigned long buf_sz)
{
	int cnt, stat, prev_cnt;

	prev_cnt = bf_count(f);
	cnt = _bf_append_data(f, buf, buf_sz);
	if (cnt < 0)
		return cnt;
	if (!prev_cnt && bf_count(f)) {
		stat = _bf_find_format(&bfile);
		if (stat < 0)
			return stat;
	}
	return cnt;
}

int dfu_binary_file_flush_start(struct dfu_binary_file *bf)
{
	bf->flushing = 1;
	if (!bf->format_ops)
		if (_bf_find_format(bf) < 0)
			return -1;
	return _bf_do_flush(bf);
}

int dfu_binary_file_written(struct dfu_binary_file *f)
{
	return f->written;
}

int dfu_binary_file_get_tot_appended(struct dfu_binary_file *f)
{
	return f->tot_appended;
}

