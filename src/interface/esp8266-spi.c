/*
 * libdfu spi interface for esp8266 host
 *
 * Author Davide Ciminaghi <davide@linino.org>
 * Copyright (C) Arduino S.r.l. 2017
 */
#include <eagle_soc.h>
#include <stdlib.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "esp8266-spi.h"

#define REG_SPI_BASE(i)		(0x60000200 - (i << 8))

/* Registers' offset */
#define SPI_CMD		0x00
#  define SPI_BUSY	BIT(18)

#define SPI_ADDR	0x04
#define SPI_CTRL	0x08
#define SPI_RD_STATUS	0x10
#define SPI_CTRL2	0x14
#define SPI_CLOCK	0x18
#  define SPI_CLOCK_PRESCALER_SHIFT 18
#  define SPI_CLOCK_DIVIDER_SHIFT 12
#define SPI_USER	0x1c
#define SPI_USER1	0x20
#  define MOSI_BITLEN_MASK 0x1ff
#  define MOSI_BITLEN_SHIFT 17
#  define MISO_BITLEN_MASK 0x1ff
#  define MISO_BITLEN_SHIFT 8

#define SPI_USER2	0x24
#define SPI_PIN		0x2c
#define SPI_SLAVE	0x30
#  define SLAVE_MODE	BIT(30)

#define SPI_W(j)	(0x40 + ((j) << 2))

/* These depend on the sdk we're building for :-( */
#ifndef FUNC_HSPIQ_MISO
#define FUNC_HSPIQ_MISO 2
#endif
#ifndef FUNC_HSPID_MOSI
#define FUNC_HSPID_MOSI 2
#endif
#ifndef FUNC_HSPI_CLK
#define FUNC_HSPI_CLK 2
#endif

#define SPI_USER_MISO BIT(28)
#define SPI_USER_MOSI BIT(27)
#define SPI_CK_I_EDGE BIT(6)

static inline uint32_t spi_read(unsigned offs)
{
	return readl(REG_SPI_BASE(1) + offs);
}

static inline void spi_write(uint32_t v, unsigned offs)
{
	writel(v, REG_SPI_BASE(1) + offs);
}

static inline int _busy(void)
{
	return (spi_read(SPI_CMD) & SPI_BUSY);
}

static inline void _start(void)
{
	uint32_t v = spi_read(SPI_CMD);
	spi_write(v | SPI_BUSY, SPI_CMD);
}

/*
 * FIXME: currently ignores path and parameters. Works with SPI1 @1MHz, bytemode
 * only
 */
int esp8266_spi_open(struct dfu_interface *iface,
		     const char *path, const void *pars)
{
	uint32_t v;
	
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_HSPIQ_MISO);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_HSPID_MOSI);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_HSPI_CLK);
	/* MSB first */
	spi_write(0, SPI_CTRL);
	/* Master mode */
	v = spi_read(SPI_SLAVE);
	v &= ~SLAVE_MODE;
	spi_write(v, SPI_SLAVE);
	/* 8 bits mode */
	v = spi_read(SPI_USER1);
	v &= ~((MOSI_BITLEN_MASK << MOSI_BITLEN_SHIFT) |
	       (MISO_BITLEN_MASK << MISO_BITLEN_SHIFT));
	v |= ((7 << MOSI_BITLEN_SHIFT) | (7 << MISO_BITLEN_SHIFT));
	spi_write(v, SPI_USER1);
	/* 1MHz, divider shall be 80: prescaler 8, divider 10 */
	spi_write(7 << SPI_CLOCK_PRESCALER_SHIFT |
		  9 << SPI_CLOCK_DIVIDER_SHIFT |
		  ((7 + 1) / 2), SPI_CLOCK);
	/* No direct sysclk to spiclk */
	v = readl(PERIPHS_IO_MUX);
	v &= ~(1 << 9);
	writel(v, PERIPHS_IO_MUX);

	return 0;
}

static int _putc(const char *c, char *in)
{
	uint32_t v;

	if (c) {
		while (_busy());
		spi_write(*c, SPI_W(0));
		_start();
		while (_busy());
	}
	if (!in)
		return c ? 1 : 0;
	if (!c)
		_start();
	while (_busy());
	v = (uint8_t)spi_read(SPI_W(0));
	*in = v;
	if (in)
		*in = v;
	return 1;
}

static int _write_read(struct dfu_interface *iface,
			   const char *out_buf, char *in_buf,
			   unsigned long size)
{
	unsigned long i;

	for (i = 0; i < size; i++)
		if (_putc(out_buf ? &out_buf[i] : NULL,
			  in_buf ? &in_buf[i] : NULL) < 0)
			return -1;
	return size;
}

int esp8266_spi_write(struct dfu_interface *iface,
		      const char *buf, unsigned long size)
{
	/* Enable MOSI only */
	spi_write(SPI_USER_MOSI | SPI_CK_I_EDGE, SPI_USER);
	return _write_read(iface, buf, NULL, size);
}

int esp8266_spi_write_read(struct dfu_interface *iface,
			   const char *out_buf, char *in_buf,
			   unsigned long size)
{
	/* Enable MOSI AND MISO */
	spi_write(SPI_USER_MISO | SPI_USER_MOSI | SPI_CK_I_EDGE, SPI_USER);
	return _write_read(iface, out_buf, in_buf, size);
}

int esp8266_spi_read(struct dfu_interface *iface,
		     char *in_buf,
		     unsigned long size)
{
	/* Enable MISO only */
	spi_write(SPI_USER_MISO | SPI_CK_I_EDGE, SPI_USER);
	return _write_read(iface, NULL, in_buf, size);
}

int esp8266_spi_poll_idle(struct dfu_interface *iface)
{
	return 0;
}
