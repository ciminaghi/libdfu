#include "dfu.h"
#include "dfu-internal.h"

struct binary_format_data {
	unsigned long curr_addr;
};

/* Just one instance for the moment */
static struct binary_format_data bfdata;

/* Binary format, anything is ok */
int binary_probe(struct dfu_binary_file *f)
{
	dfu_log("raw binary format probed\n");
	f->format_data = &bfdata;
	/* FIXME: GET DEFAULT START ADDR */
	bfdata.curr_addr = 0;
	return 0;
}

static int _subcopy(struct dfu_binary_file *bf, void *out_buf, int out_sz)
{
	int sz = min(out_sz, bf_count_to_end(bf)), tot = sz;
	char *ptr = bf->buf, *dst = out_buf;

	if (!sz)
		return 0;
	memcpy(dst, &ptr[bf->tail], sz);
	bf->tail = (bf->tail + sz) & (bf->max_size - 1);
	if (sz == out_sz)
		return sz;
	sz = min(out_sz - tot, bf_count(bf));
	memcpy(&dst[tot], &ptr[bf->tail], sz);
	bf->tail = (bf->tail + sz) & (bf->max_size - 1);
	tot += sz;

	return tot;
}

/* Fix this */
int binary_decode_chunk(struct dfu_binary_file *bf, phys_addr_t *addr)
{
	int tot = 0, sz, out_sz = bf_dec_space_to_end(bf);
	struct binary_format_data *data = bf->format_data;

	if (!out_sz)
		return out_sz;

	sz = _subcopy(bf, &((char *)bf->decoded_buf)[bf->decoded_head], out_sz);
	bf->decoded_head = (bf->decoded_head + sz) & (bf->decoded_size - 1);
	tot += sz;

	out_sz = bf_dec_space(bf);
	if (!out_sz)
		goto end;
	sz = _subcopy(bf, &((char *)bf->decoded_buf)[bf->decoded_head], out_sz);
	bf->decoded_head = (bf->decoded_head + sz) & (bf->decoded_size - 1);
	tot += sz;

end:
	*addr = data->curr_addr;
	data->curr_addr += tot;
	return tot;
}

int binary_fini(struct dfu_binary_file *bf)
{
	return 0;
}

declare_dfu_format(binary, binary_probe, binary_decode_chunk, binary_fini);
