
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"

struct dfu_binary_file *dfu_new_binary_file(const void *buf,
					    unsigned long buf_sz,
					    unsigned long totsz,
					    struct dfu_data *dfu,
					    unsigned long addr)
{
	return NULL;
}

struct dfu_binary_file *dfu_binary_file_start_rx(const char *method,
						 struct dfu_data *dfu)
{
	return NULL;
}

int dfu_binary_file_append_buffer(struct dfu_binary_file *f,
				  const void *buf,
				  unsigned long buf_sz)
{
	return -1;
}

int dfu_binary_file_flush_start(struct dfu_binary_file *f)
{
	return -1;
}

int dfu_binary_file_written(struct dfu_binary_file *f)
{
	return 0;
}
