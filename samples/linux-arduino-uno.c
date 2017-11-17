/*
 * libdfu, usage sample (programming the arduino uno via serial port under linux)
 * Author Davide Ciminaghi, 2016
 * Public domain
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dfu.h>
#include <dfu-linux.h>
#include <dfu-stk500.h>

struct private_data {
	void *ptr;
	int file_size;
};

static void help(int argc, char *argv[])
{
	fprintf(stderr, "Use %s <fname> <serial_port>\n", argv[0]);
}

static void *map_file(const char *path, size_t len)
{
	int fd = open(path, O_RDONLY);
	void *out;

	if (fd < 0) {
		perror("open");
		return NULL;
	}
	out = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
	if (out == MAP_FAILED) {
		perror("mmap");
		return NULL;
	}
	close(fd);
	return out;
}

static int binary_file_poll_idle(struct dfu_binary_file *f)
{
	struct private_data *priv = dfu_binary_file_get_priv(f);
	int tot = dfu_binary_file_get_tot_appended(f);

	/* Always ready */
	return tot < priv->file_size ? DFU_FILE_EVENT : 0;
}

static int binary_file_on_event(struct dfu_binary_file *f)
{
	struct private_data *priv = dfu_binary_file_get_priv(f);
	int tot = dfu_binary_file_get_tot_appended(f), stat;

	if (!priv) {
		dfu_err("NO PRIVATE DATA FOR BINARY FILE");
		return -1;
	}
	dfu_dbg("tot = %d, appending %d\n", tot, priv->file_size - tot);
	stat = dfu_binary_file_append_buffer(f, &((char *)priv->ptr)[tot],
					     priv->file_size - tot);
	if (stat < 0)
		return stat;
	dfu_dbg("appended %d bytes\n", stat);
	tot = dfu_binary_file_get_tot_appended(f);
	if (tot == priv->file_size) {
		dfu_dbg("nothing more to append\n");
		dfu_binary_file_append_buffer(f, NULL, 0);
		return 0;
	}
	return 0;
}

static struct dfu_binary_file_ops binary_file_ops = {
	.poll_idle = binary_file_poll_idle,
	.on_event = binary_file_on_event,
};

int main(int argc, char *argv[])
{
	const char *fpath;
	char *port;
	int ret;
	struct stat s;
	struct dfu_data *dfu;
	struct dfu_binary_file *f;
	void *ptr;
	struct private_data priv;
	

	if (argc < 3) {
		help(argc, argv);
		exit(127);
	}
	fpath = argv[1];
	port = argv[2];

	/* Check whether file and port exist */
	ret = stat(fpath, &s);
	if (ret < 0) {
		perror("stat");
		exit(127);
	}
	dfu = dfu_init(&linux_serial_arduino_uno_interface_ops,
		       port,
		       NULL,
		       /* No interface start cb */
		       NULL,
		       NULL,
		       &stk500_dfu_target_ops,
		       &atmega328p_device_data,
		       &linux_dfu_host_ops,
		       NULL);
	if (!dfu) {
		fprintf(stderr, "Error initializing libdfu\n");
		exit(127);
	}
	ptr = map_file(fpath, s.st_size);
	priv.ptr = ptr;
	priv.file_size = s.st_size;
	f = dfu_new_binary_file(ptr, s.st_size, s.st_size, dfu, 0,
				&binary_file_ops, &priv);
	if (!f) {
		fprintf(stderr, "Error setting up binary file struct\n");
		exit(127);
	}
	/* Reset and probe target */
	if (dfu_target_reset(dfu) < 0) {
		fprintf(stderr, "Error resetting target\n");
		exit(127);
	}
	fprintf(stderr, "**** target reset *****\n");
	if (dfu_target_probe(dfu) < 0) {
		fprintf(stderr, "Error probing target\n");
		exit(127);
	}
	fprintf(stderr, "**** target probed ****\n");
	if (dfu_target_erase_all(dfu) < 0) {
		fprintf(stderr, "Error erasing target memory\n");
		exit(127);
	}
	/* Start programming data */
	fprintf(stderr, "**** file flush started ****\n");
	if (dfu_binary_file_flush_start(f) < 0) {
		fprintf(stderr, "Error programming file\n");
		exit(127);
	}
	/* Loop around waiting for events */
	do {
		ret = dfu_idle(dfu);
		switch (ret) {
		case DFU_ERROR:
			fprintf(stderr, "Error programming file\n");
			break;
		case DFU_ALL_DONE:
			fprintf(stderr, "Programming DONE\n");
			break;
		case DFU_CONTINUE:
			break;
		default:
			fprintf(stderr,
				"Invalid ret value %d from dfu_idle()\n", ret);
			break;
		}
	} while(ret == DFU_CONTINUE);
	/* Let target run */
	exit(dfu_target_go(dfu));
}
