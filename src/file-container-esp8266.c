/*
 * libdfu, implementation of file container for esp8266 spi flash
 * LGPL v2.1
 * Copyright What's Next GmbH 2018
 * Author Davide Ciminaghi 2018
 */
#include <dfu.h>
#include <dfu-internal.h>

struct esp8266_spi_flash_file_data {
	int fd;
};

static int esp8266_spi_flash_file_close(struct dfu_simple_file *f)
{
	return -1;
}

static int esp8266_spi_flash_file_read(struct dfu_simple_file *f, char *buf,
				       unsigned long sz)
{
	return -1;
}

static int esp8266_spi_flash_file_write(struct dfu_simple_file *f,
					const char *buf,
					unsigned long sz)
{
	return -1;
}

static struct dfu_simple_file_ops esp8266_spi_flash_file_ops = {
	.close = esp8266_spi_flash_file_close,
	.read = esp8266_spi_flash_file_read,
	.write = esp8266_spi_flash_file_write,
};

static int esp8266_spi_flash_fc_init(struct dfu_file_container *fc)
{
	return -1;
}

static int esp8266_spi_flash_fc_open(struct dfu_file_container *fc,
			 struct dfu_simple_file *f,
			 const char *name,
			 int create_if_not_found,
			 unsigned long max_size)
{
	f->ops = &esp8266_spi_flash_file_ops;
	return -1;
}

static int esp8266_spi_flash_fc_remove(struct dfu_file_container *fc,
				       const char *name)
{
	return -1;
}

struct dfu_file_container_ops esp8266_spi_flash_fc_ops = {
	.init = esp8266_spi_flash_fc_init,
	.open_file = esp8266_spi_flash_fc_open,
	.remove_file = esp8266_spi_flash_fc_remove,
};
