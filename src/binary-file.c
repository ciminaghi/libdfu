
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "dfu-host.h"

#ifndef CONFIG_BINARY_FILE_BUFSIZE
#define CONFIG_BINARY_FILE_BUFSIZE 1024
#endif

static char bf_buf[CONFIG_BINARY_FILE_BUFSIZE];

static struct dfu_binary_file bfile;

static inline int _bf_count(struct dfu_binary_file *bf)
{
	return (bf->head - bf->tail) & ARRAY_SIZE(bf_buf);
}

static inline int _bf_space(struct dfu_binary_file *bf)
{
	return (bf->tail - (bf->head + 1)) & ARRAY_SIZE(bf_buf);
}

static inline int _bf_count_to_end(struct dfu_binary_file *bf)
{
	int end = ARRAY_SIZE(bf_buf) - bf->tail;
	int n = (bf->head + end) & (ARRAY_SIZE(bf_buf) - 1);
	return n < end ? n : end ;
}

static inline int _bf_space_to_end(struct dfu_binary_file *bf)
{
	int end = ARRAY_SIZE(bf_buf) - 1 - bf->head;
	int n = (end + bf->tail) & (ARRAY_SIZE(bf_buf) - 1);
	return n <= end ? n : end + 1;
}

static void _bf_init(struct dfu_binary_file *bf, char *b, struct dfu_data *dfu)
{
	bf->buf = b;
	bf->head = bf->tail = 0;
	bf->written = 0;
	bf->host_data = NULL;
	bf->format_ops = NULL;
	bf->dfu = dfu;
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

	sz = min(_bf_space_to_end(bf), buf_sz);
	if (sz <= 0)
		return sz;
	memcpy(&ptr[bf->head], buf, sz);
	bf->head = (bf->head + sz) & (ARRAY_SIZE(bf_buf) - 1);
	buf_sz -= sz;
	if (!buf_sz)
		return sz;
	tot = sz;
	sz = min(_bf_space_to_end(bf), buf_sz);
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

struct dfu_binary_file *dfu_new_binary_file(const void *buf,
					    unsigned long buf_sz,
					    unsigned long totsz,
					    struct dfu_data *dfu,
					    unsigned long addr)
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
	return &bfile;
}

struct dfu_binary_file *dfu_binary_file_start_rx(const char *method,
						 struct dfu_data *dfu)
{
	struct dfu_binary_file *out = NULL;

	if (!dfu->host->ops->start_file_rx)
		return out;
	out = dfu_new_binary_file(NULL, 0, 0, dfu, 0);
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

	prev_cnt = _bf_count(f);
	cnt = _bf_append_data(f, buf, buf_sz);
	if (cnt < 0)
		return cnt;
	if (!prev_cnt && _bf_count(f)) {
		stat = _bf_find_format(&bfile);
		if (stat < 0)
			return stat;
	}
	return cnt;
}

int dfu_binary_file_flush_start(struct dfu_binary_file *f)
{
	return f->dfu->host->ops->file_flush_start ?
		f->dfu->host->ops->file_flush_start(f) : -1;
}

int dfu_binary_file_written(struct dfu_binary_file *f)
{
	return f->written;
}
