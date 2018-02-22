/*
 * libdfu file container implementation
 * LGPL v2.1
 * Copyright CC Logistics S.r.l. 2017
 * Author Davide Ciminaghi 2017
 */
#include "dfu.h"
#include "dfu-internal.h"

#define MAX_DFU_FILES 8

static struct dfu_simple_file files[MAX_DFU_FILES];

static struct dfu_simple_file *__find_free_file_slot(const char *path)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(files); i++) {
		if (!files[i].path) {
			files[i].path = path;
			return &files[i];
		}
	}
	return NULL;
}

static void __free_file(struct dfu_simple_file *f)
{
	f->path = NULL;
}

int dfu_file_open(struct dfu_data *dfu, const char *path,
		  int create_if_not_found, unsigned long max_size)
{
	struct dfu_simple_file *f;
	int ret;
	
	if (!dfu->fc || !dfu->fc->ops->open_file)
		return -1;
	f = __find_free_file_slot(path);
	if (!f)
		return -1;
	ret = dfu->fc->ops->open_file(dfu->fc, f, path, create_if_not_found,
				      max_size);
	if (ret < 0) {
		__free_file(f);
		return ret;
	}
	f->fileptr = 0;
	return f - files;
}

int dfu_file_close(struct dfu_data *dfu, int fd)
{
	struct dfu_simple_file *f;
	int ret = 0;

	if (fd < 0 || fd >= MAX_DFU_FILES)
		return -1;
	f = &files[fd];
	if (f->ops->close)
		ret = f->ops->close(f);
	return ret;
}

int dfu_file_read(struct dfu_data *dfu, int fd, void *buf, unsigned long sz)
{
	struct dfu_simple_file *f;
	int ret;

	if (fd < 0 || fd >= MAX_DFU_FILES)
		return -1;
	f = &files[fd];
	if (!f->ops->read)
		return -1;
	ret = f->ops->read(f, buf, sz);
	if (ret < 0)
		return ret;
	f->fileptr += ret;
	return ret;
}

int dfu_file_write(struct dfu_data *dfu, int fd, const void *buf,
		   unsigned long sz)
{
	struct dfu_simple_file *f;
	int ret;

	if (fd < 0 || fd >= MAX_DFU_FILES)
		return -1;
	f = &files[fd];
	if (!f->ops->read)
		return -1;
	ret = f->ops->write(f, buf, sz);
	if (ret < 0)
		return ret;
	f->fileptr += ret;
	return ret;
}

int dfu_file_remove(struct dfu_data *dfu, const char *path)
{
	if (!dfu->fc || !dfu->fc->ops->remove_file)
		return -1;
	return dfu->fc->ops->remove_file(dfu->fc, path);
}
