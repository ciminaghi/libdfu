/*
 * libdfu, implementation of file container for posix file calls
 * LGPL v2.1
 * Copyright Arduino S.r.l. 2016
 * Copyright What’s Next GmbH 2017
 * Author Davide Ciminaghi 2016 2017
 */
#include <dfu.h>
#include <dfu-internal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

struct posix_simple_file_data {
	int fd;
};

static int posix_simple_file_close(struct dfu_simple_file *f)
{
	struct posix_simple_file_data *priv = f->priv;

	return close(priv->fd);
}

static int posix_simple_file_read(struct dfu_simple_file *f, char *buf,
				  unsigned long sz)
{
	struct posix_simple_file_data *priv = f->priv;

	return read(priv->fd, buf, sz);
}

static int posix_simple_file_write(struct dfu_simple_file *f, const char *buf,
				   unsigned long sz)
{
	struct posix_simple_file_data *priv = f->priv;

	return write(priv->fd, buf, sz);
}

static int posix_simple_file_seek(struct dfu_simple_file *f, unsigned long ptr)
{
	struct posix_simple_file_data *priv = f->priv;

	return lseek(priv->fd, SEEK_SET, ptr);
}

static struct dfu_simple_file_ops posix_simple_file_ops = {
	.close = posix_simple_file_close,
	.read = posix_simple_file_read,
	.write = posix_simple_file_write,
	.seek = posix_simple_file_seek,
};

static int posix_fc_open(struct dfu_file_container *fc,
			 struct dfu_simple_file *f,
			 const char *name,
			 int create_if_not_found,
			 unsigned long max_size_ignored)
{
	int flags = O_RDWR | (create_if_not_found ? O_CREAT : 0);
	struct posix_simple_file_data *data = malloc(sizeof(*data));

	if (!data)
		return -1;
	data->fd = open(name, flags,  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	f->priv = data;
	f->ops = &posix_simple_file_ops;
	return 0;
}

static int posix_fc_remove(struct dfu_file_container *fc, const char *name)
{
	return unlink(name);
}

struct dfu_file_container_ops posix_fc_ops = {
	.open_file = posix_fc_open,
	.remove_file = posix_fc_remove,
};
