/*
 * libdfu, usage sample (programming the nrf52 via spi, pc host)
 * Author Davide Ciminaghi, 2016
 * Public domain
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <dfu.h>
#include <dfu-linux.h>
#include <dfu-nordic-spi.h>

struct private_data {
	char buf[1024];
	int fd;
	int file_size;
};

static void help(int argc, char *argv[])
{
	fprintf(stderr, "Use %s <fname> <serial_port>\n", argv[0]);
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
	if (lseek(priv->fd, tot, SEEK_SET) < 0) {
		dfu_err("lseek: %s\n", strerror(errno));
		return -1;
	}
	stat = read(priv->fd, priv->buf, sizeof(priv->buf));
	if (stat < 0) {
		dfu_err("read: %s\n", strerror(errno));
		return -1;
	}
	dfu_dbg("%s: tot = %d, appending %d\n", __func__,
		tot, priv->file_size - tot);
	stat = dfu_binary_file_append_buffer(f, priv->buf, stat);
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
	dfu = dfu_init(&linux_spi_bp_nordic_target_interface_ops,
		       port,
		       NULL,
		       /* No interface start cb */
		       NULL,
		       NULL,
		       &nordic_spi_dfu_target_ops,
		       NULL,
		       &linux_dfu_host_ops,
		       &posix_fc_ops);
	if (!dfu) {
		fprintf(stderr, "Error initializing libdfu\n");
		exit(127);
	}
	priv.fd = open(fpath, O_RDONLY);
	if (priv.fd < 0) {
		perror("open");
		exit(127);
	}
	priv.file_size = s.st_size;
	ret = read(priv.fd, priv.buf, sizeof(priv.buf));
	if (ret < 0) {
		perror("read");
		exit(127);
	}
	f = dfu_new_binary_file(priv.buf, ret, s.st_size, dfu, 0,
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
	if (dfu_target_probe(dfu) < 0) {
		fprintf(stderr, "Error probing target\n");
		exit(127);
	}
	/* Start programming data */
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
