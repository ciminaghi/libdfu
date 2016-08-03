/* This rx method uses tftp and lwip raw (no sockets) */

#ifdef HAVE_LWIP

#include "dfu.h"
#include "dfu-internal.h"

static int dfu_rx_tftp_init(struct dfu_binary_file *bf, void *arg)
{
	return -1;
}

static int dfu_rx_tftp_poll_idle(struct dfu_binary_file *bf)
{
	return 0;
}

static const struct dfu_file_rx_method_ops tftp_rx_ops = {
	.init = dfu_rx_tftp_init,
	.poll_idle = dfu_rx_tftp_poll_idle,
};

declare_file_rx_method(tftp, &tftp_rx_ops);

#endif /* HAVE_LWIP */
