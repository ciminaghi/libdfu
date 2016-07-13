#include "dfu.h"
#include "dfu-internal.h"

/* Binary format, anything is ok */
static int binary_probe(struct dfu_binary_file *f)
{
	return 0;
}

/* Fix this */
static int binary_decode_chunk(struct dfu_binary_file *bf, void *out_buf,
			       unsigned long *addr, unsigned long *out_sz)
{
	return -1;
}

declare_dfu_format(binary, binary_probe, binary_decode_chunk);
