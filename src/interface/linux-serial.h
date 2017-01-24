/*
 * Internal header for linux serial interface
 */
#ifndef __LINUX_SERIAL_H__
#define __LINUX_SERIAL_H__

/*
 * Linux serial interface private data
 */
struct linux_serial_data {
	int fd;
};

extern int linux_serial_open(struct dfu_interface *iface,
			     const char *path, const void *pars);
extern int linux_serial_write(struct dfu_interface *iface,
			      const char *buf, unsigned long size);
extern int linux_serial_read(struct dfu_interface *iface, char *buf,
			     unsigned long size);
extern int linux_serial_fini(struct dfu_interface *iface);

#endif /* __LINUX_SERIAL_H__ */

