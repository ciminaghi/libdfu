/*
 * Internal header for spi interface via bus pirate under linux
 */
#ifndef __LINUX_SPI_BUS_PIRATE_H__
#define __LINUX_SPI_BUS_PIRATE_H__

extern int linux_spi_bp_open(struct dfu_interface *, const char *,
			     const void *);
extern int linux_spi_bp_write(struct dfu_interface *, const char *,
			      unsigned long);
extern int linux_spi_bp_write_read(struct dfu_interface *,
				   const char *, char *i,
				   unsigned long);
extern int linux_spi_bp_fini(struct dfu_interface *);


#endif /* __LINUX_SPI_BUS_PIRATE_H__ */

