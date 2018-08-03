/*
 * libdfu, implementation of file container for esp8266 spi flash
 * LGPL v2.1
 * Copyright What's Next GmbH 2018
 * Author Davide Ciminaghi 2018
 */
#include <dfu.h>
#include <dfu-internal.h>

#include <spi_flash.h>

#define MAX_FILES 8

/* Provided by linker script */
extern unsigned long sectsize, flash_first_sector;

/* This is in RAM, allocated by the linker */
extern struct spi_flash_sector sectors_start[], sectors_end[];

struct spiflash_container_data {
	struct spi_flash_sector free_head;
	/* Size of free sectors area */
	int nsectors;
};

static struct spiflash_container_data fcdata;

/* Trivial layout in flash */

#define NO_SECTOR_ADDR ((uint32)-1)
#define NO_SECTOR_INDEX ((unsigned int)-1)
#define NO_FLASH_ADDR ((uint32)-1)
#define NO_FLASH_SIZE ((uint32)-1)
#define INVALID_OFFS  ((unsigned long)-1)

/*
 * File related structure: to simplify things, for the moment we get
 * [at least] a sector per file
 * Each sector has this header on top.
 */
union spi_flash_sector_header {
	uint32 v[0];
	struct {
		/* File name */
		char name[64];
		/* File offset in this sector */
		uint32_t start_offset;
		uint32_t data_size;
		/* FIXME: crc IS CURRENTLY UNUSED */
		uint32_t crc;
		unsigned char data[0];
	} s;
};

#define sectdatasize (sectsize - sizeof(union spi_flash_sector_header))

struct spi_flash_file_data {
	struct spi_flash_sector sectors;
	unsigned long written;
	unsigned long max_size;
};

static inline int sect_ptr_to_index(struct spi_flash_sector *s)
{
	return s - sectors_start;
}

static inline int flash_sect_ptr_to_index(struct spi_flash_sector *s)
{
	return sect_ptr_to_index(s) + flash_first_sector;
}

static inline uint32 flash_sect_index_to_addr(unsigned int fsi)
{
	return fsi * sectsize;
}

static inline uint32 flash_sect_ptr_to_addr(struct spi_flash_sector *s)
{
	return flash_sect_index_to_addr(flash_sect_ptr_to_index(s));
}

static inline uint32 flash_sect_index_to_data_addr(unsigned int fsi)
{
	return flash_sect_index_to_addr(fsi) +
		sizeof(union spi_flash_sector_header);
}



/*
 * Add sector to list of sectors.
 *
 * @prev: pointer to sector list head
 * @sector: pointer sector to be appended
 */
static inline void sector_add(struct spi_flash_sector *prev,
			      struct spi_flash_sector *sector)
{
	struct spi_flash_sector *next = prev->next;

	sector->prev = prev;
	sector->next = prev->next;
	prev->next = sector;
	next->prev = sector;
}

/*
 * Delete sector from list
 *
 * @sector: sector to be removed
 */
static inline void sector_del(struct spi_flash_sector *sector)
{
	struct spi_flash_sector *next = sector->next;
	struct spi_flash_sector *prev = sector->prev;

	prev->next = next;
	next->prev = prev;
}

/*
 * Initialize empty list of sectors
 */
static inline void sector_list_init(struct spi_flash_sector *sector)
{
	sector->next = sector;
	sector->prev = sector;
}

/*
 * Returns !0 if sectors list is empty
 */
static inline int sectors_list_empty(struct spi_flash_sector *sector)
{
	return sector->next == sector && sector->prev == sector;
}

#define for_each_sector(s, l)						\
	for (s = (l)->next; (s) != l; s = s->next)

#define for_each_sector_safe(s, t, l)		\
	for (s = (l)->next, t = s->next; s != l; s = t, t = s->next)

static inline int sectors_list_count(struct spi_flash_sector *l)
{
	struct spi_flash_sector *s;
	int out = 0;

	dfu_dbg("%s %d\n", __func__, __LINE__);
	for_each_sector(s, l)
		out++;
	dfu_dbg("%s returns %d\n", __func__, out);
	return out;
}

static inline uint32 _align_next(uint32 a)
{
	uint32 mask = sizeof(a) - 1;

	return (a + mask) & ~mask;
}

static inline uint32 *_align_next_ptr(void *a)
{
	return (uint32 *)_align_next((uint32)a);
}

static inline uint32 _align_prev(uint32 a)
{
	uint32 mask = sizeof(a) - 1;

	return a & ~mask;
}

static int _ptr_diff(void *a, void *b)
{
	return (char *)a - (char *)b;
}

#define TMP_BUF_SIZE 512

static union {
	/* Force alignment */
	uint32   dw[0];
	uint8_t  c[TMP_BUF_SIZE];
} flash_tmpbuf;

static SpiFlashOpResult _spi_flash_erase_sector(uint16 sec)
{
	SpiFlashOpResult ret = spi_flash_erase_sector(sec);

	dfu_dbg("spi_flash_erase_sec(0x%04x) = %d\n", sec, ret);
	return ret;
}

static SpiFlashOpResult _spi_flash_write(uint32 dst, uint32 *src, uint32 size)
{
	SpiFlashOpResult ret;

	ret = spi_flash_write(dst, src, size);
	dfu_dbg("spi_flash_write(0x%08x, %p, %u) = %d\n", dst, src, size, ret);
	return ret;
}

static SpiFlashOpResult _spi_flash_read(uint32 src, uint32 *dst, uint32 size)
{
	SpiFlashOpResult ret;

	ret = spi_flash_read(src, dst, size);
	dfu_dbg("spi_flash_read(0x%08x, %p, %u) = %d\n", src, dst, size, ret);
	return ret;
}

static void *_memcpy(void *dst, const void *src, unsigned int sz)
{
	dfu_dbg("memcpy(%p, %p, %u)\n", dst, src, sz);
	return memcpy(dst, src, sz);
}

/*
 * slow flash read: both src and dst are unaligned and have different
 * alignments: use temporary buffer
 *
 * example 1:
 *      _src = 0, _dst = 3, _sz = 14
 *      aligned_src = 0
 *      aligned_end = _align_next(0 + 14 - 1) - 1 = 15
 *      aligned_sz = min(512, 15 + 1) = 16
 *      flash_read(0, tmpbuf, 16)
 *      sz = min(14, 512 - 0) = 14
 *      memcpy(3, &tmpbuf[0], 14)
 *
 * example 2:
 *      _src = 1, _dst = 3, _sz = 15
 *      aligned_src = 0
 *      aligned_end = _align_next(1 + 15 - 1) - 1 = 15
 *      aligned_sz = min(512, 15 + 1) = 16
 *      flash_read(0, tmpbuf, 16)
 *      sz = min(15, (512 - (1 - 0)) = 15
 *      memcpy(3, &tmpbuf[1], 15)
 *
 * example 3:
 *      _src = 2, _dst = 3, _sz = 1302
 *      aligned_src = 0
 *      aligned_end = _align_next(2 + 1302 - 1) - 1 = 1303
 *      aligned_sz = min(512, 1303 + 1) = 512
 *      flash_read(0, tmpbuf, 512)
 *      sz = min(13, 512 - 2) = 510
 *      memcpy(3, &tmpbuf[2], 510)
 *
 * example 4:
 *      _src = 2, _dst = 0, _sz = 1501
 *      aligned_src = 0
 *      aligned_end = _align_next(2 + 1501 - 1) - 1 = 1503
 *      aligned_sz = min(512, 1503 + 1)  = 512
 *      flash_read(0, tmpbuf, 512)
 *      sz = min(1501, 512 - 2) = 510
 *      memcpy(0, &tmpbuf[2], 510
 */
static int _slow_flash_read(uint32 _src, void *_dst, uint32 _sz)
{
	uint32 aligned_src, aligned_end, aligned_sz, sz;
	int stat;

	aligned_src = _align_prev(_src);
	aligned_end = _align_next(_src + _sz - 1) - 1;
	aligned_sz = min(sizeof(flash_tmpbuf), aligned_end - aligned_src + 1);
	stat = _spi_flash_read(aligned_src, flash_tmpbuf.dw, aligned_sz);
	if (stat != SPI_FLASH_RESULT_OK)
		return -1;
	sz = min(_sz, (sizeof(flash_tmpbuf) - (_src - aligned_src)));
	_memcpy(_dst, &flash_tmpbuf.c[_src - aligned_src], sz);
	return sz;
}

/*
 * Simplest and fastest case, both src and dst are correctly
 * aligned.
 */
static int _aligned_flash_read(void *dst, uint32 *aligned_dst,
			       uint32 src, uint32 aligned_src,
			       uint32 sz)
{
	int stat;
	uint32 v, aligned_sz;

	aligned_sz = _align_prev(sz);
	stat = _spi_flash_read(src, dst, aligned_sz);
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;
	if (aligned_sz == sz)
		return SPI_FLASH_RESULT_OK;
	/* size is unaligned, read the last dword */
	stat = _spi_flash_read(src + aligned_sz, &v, sizeof(v));
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;
	_memcpy((char *)dst + aligned_sz, &v, sz - aligned_sz);
	return SPI_FLASH_RESULT_OK;
}

/*
 * src and dst are "equally unaligned" (same misalignment on src and dst)
 *
 * aligned_src and aligned_dst are both 1 to 3 bytes ahead of
 * their unaligned counterparts.
 *
 * aligned_src/dst                 aligned_src/dst + sz - 1 - 3
 *      |                                |
 *      |                                |
 *  src/dst                              | src/dst + sz - 1
 *  |   |                                |   |
 * 0|   4 .............................. |   |
 *  1
 *  1
 *
 * Example 1:
 * src = 1, dst = 1, sz = 13 (interval 1 -> 13)
 * aligned_src = 4, aligned_dst = 4,
 * aligned_sz = align_prev(13 - (4 - 1)) = 8
 * aligned interval 4 -> 11
 * First read 4 -> 11
 * Second read 0 -> 4, then copy bytes 1-3 to dst
 * Third read 12 -> 15, then copy byte 12-13 to dst
 *
 * Example 2:
 * src = 1, dst = 1, sz = 15 (interval = 1 -> 15)
 * aligned_src = 4, aligned_dst = 4,
 * aligned_sz = align_prev(15 - (4 - 1)) = 12
 * aligned_interval = 4 -> 15
 * First read 4 -> 15
 * Second read 0 -> 4, then copy bytes 1-3 to dst
 */
static int _equally_unaligned_flash_read(void *dst, uint32 *aligned_dst,
					 uint32 src, uint32 aligned_src,
					 uint32 sz)
{
	uint32 v, src_end = src + sz - 1, aligned_src_end, aligned_sz;
	unsigned int start_delta, end_delta;
	int stat;

	start_delta = aligned_src - src;
	aligned_sz = _align_prev(sz - start_delta);
	aligned_src_end = aligned_src + aligned_sz - 1;
	end_delta = src_end - aligned_src_end;

	stat = _spi_flash_read(aligned_src, aligned_dst, aligned_sz);
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;
	/* Now read the first dword and get the needed 1 to 3 bytes */
	stat = _spi_flash_read(aligned_src - sizeof(uint32), &v, sizeof(v));
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;
	_memcpy(dst, ((char *)&v) + sizeof(v) - start_delta, start_delta);
	/*
	 * If needed, finally read the last dword and get the needed
	 * 3 to 1 bytes
	 */
	if (!end_delta)
		return SPI_FLASH_RESULT_OK;
	stat = _spi_flash_read(aligned_src + aligned_sz, &v, sizeof(v));
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;

	_memcpy((char *)aligned_dst + aligned_sz, ((char *)&v), end_delta);
	return SPI_FLASH_RESULT_OK;
}

static int _flash_read(uint32 src, void *dst, uint32 sz)
{
	uint32 *aligned_dst, aligned_src;
	int stat, done;

	aligned_src = _align_next(src);
	aligned_dst = _align_next_ptr(dst);

	if (!(_ptr_diff(aligned_dst, dst)) && !(aligned_src - src))
		return _aligned_flash_read(dst, aligned_dst,
					   src, aligned_src,
					   sz);

	if ((_ptr_diff(aligned_dst, dst)) == (aligned_src - src))
		return _equally_unaligned_flash_read(dst, aligned_dst,
						     src, aligned_src,
						     sz);
	/*
	 * src and dst have different (un)alignments: use temporary buffer
	 * based read
	 */
	for (done = 0; done < sz; done += stat) {
		stat = _slow_flash_read(src + done, (char *)dst + done,
					sz - done);
		if (stat < 0)
			return SPI_FLASH_RESULT_ERR;
	}
	return SPI_FLASH_RESULT_OK;
}

static int _slow_flash_write(uint32 _dst, void *_src, uint32 _sz)
{
	uint32 aligned_dst, aligned_end, aligned_sz, start_delta, end,
		end_delta;
	int stat;

	/*
	 * This simplifies the code below
	 */
	if (_sz > sizeof(flash_tmpbuf))
		_sz = sizeof(flash_tmpbuf);

	aligned_dst = _align_next(_dst);
	aligned_sz = min(sizeof(flash_tmpbuf), _align_prev(_sz));
	/*
	 * Aligned destination end, src can be unaligned (since flash_tmpbuf
	 * __is__ aligned)
	 */
	aligned_end = aligned_dst + aligned_sz - 1;
	start_delta = aligned_dst - _dst;
	end = _dst + _sz - 1;
	if (aligned_end > end) {
		aligned_sz -= sizeof(uint32);
		aligned_end -= sizeof(uint32);
	}
	/* aligned_end should always be <= end here */
	if (aligned_end > end) {
		dfu_err("OH OH, UNEXPECTED aligned_end value\n");
		return -1;
	}
	/* end_delta refers to __destination__ */
	end_delta = end - aligned_end;

	if (end_delta >= sizeof(uint32)) {
		dfu_err("OH OH, UNEXPECTED end_delta value\n");
		return -1;
	}

	dfu_dbg("%s: _sz = %u, aligned_sz = %u, end = 0x%08x, aligned_end = 0x%08x, start_delta = %u, end_delta = %u\n", __func__, _sz, aligned_sz, end, aligned_end, start_delta, end_delta);

	_memcpy(flash_tmpbuf.dw, (char *)_src + start_delta, aligned_sz);
	stat = _spi_flash_write(aligned_dst, flash_tmpbuf.dw, aligned_sz);
	if (stat != SPI_FLASH_RESULT_OK)
		return -1;
	if (start_delta) {
		stat = _spi_flash_read(_align_prev(_dst), flash_tmpbuf.dw,
				      sizeof(uint32));
		if (stat != SPI_FLASH_RESULT_OK)
			return -1;
		_memcpy(&flash_tmpbuf.c[sizeof(uint32) - start_delta],
		       _src, start_delta);

		stat = _spi_flash_write(_align_prev(_dst), flash_tmpbuf.dw,
				       sizeof(uint32));
		if (stat != SPI_FLASH_RESULT_OK)
			return -1;
	}
	if (end_delta) {
		stat = _spi_flash_read(aligned_end + 1, flash_tmpbuf.dw,
				      sizeof(uint32));
		if (stat != SPI_FLASH_RESULT_OK)
			return -1;
		_memcpy(flash_tmpbuf.c,
			(char *)_src + aligned_sz + start_delta, end_delta);
		stat = _spi_flash_write(aligned_end + 1, flash_tmpbuf.dw,
				       sizeof(uint32));
		if (stat != SPI_FLASH_RESULT_OK)
			return -1;
	}
	dfu_dbg("%s returns %d\n", __func__, _sz);
	return _sz;
}

static int _aligned_flash_write(uint32 dst, uint32 aligned_dst,
				void *src, uint32 *aligned_src,
				uint32 sz)
{
	uint32 v;
	uint32 aligned_sz;
	int stat;

	dfu_dbg("both src and dst are aligned\n");
	/*
	 * Simplest and fastest case, both src and dst are correctly
	 * aligned.
	 */
	aligned_sz = _align_prev(sz);
	stat = _spi_flash_write(dst, src, aligned_sz);
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;
	if (aligned_sz == sz)
		return SPI_FLASH_RESULT_OK;
	/* read the last dword, replace some bytes and write back */
	stat = _spi_flash_read(dst + aligned_sz, &v, sizeof(v));
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;

	_memcpy(&v, (char *)src + aligned_sz, sz - aligned_sz);

	return _spi_flash_write(dst + aligned_sz, &v, sizeof(v));
}

/*
 * src and dst are not aligned, but are "equally unaligned"
 */
/*
 * aligned_src and aligned_dst are both 1 to 3 bytes ahead of
 * their unaligned counterparts.
 *
 * aligned_src/dst                 aligned_src/dst + sz - 1 - 3
 *      |                                |
 *      |                                |
 *  src/dst                              | src/dst + sz - 1
 *  |   |                                |   |
 * 0|   4 .............................. |   |
 *  1
 *  1
 *
 * Example 1:
 * src = 1, dst = 1, sz = 13 (interval 1 -> 13)
 * aligned_src = 4, aligned_dst = 4,
 * aligned_sz = align_prev(13 - (4 - 1)) = 8
 * aligned interval 4 -> 11
 * First write 4 -> 11
 * Read bytes 0 -> 4, replace bytes 1-3 and write back
 * Read bytes 12 -> 15, replace bytes 12-13 and write back
 *
 * Example 2:
 * src = 1, dst = 1, sz = 15 (interval = 1 -> 15)
 * aligned_src = 4, aligned_dst = 4,
 * aligned_sz = align_prev(15 - (4 - 1)) = 12
 * aligned_interval = 4 -> 15
 * First write 4 -> 15
 * Read bytes 0 -> 4, replace bytes 1-3 and write back
 */
static int _equally_unaligned_flash_write(uint32 dst, uint32 aligned_dst,
					  void *src, uint32 *aligned_src,
					  uint32 sz)
{
	uint32 v, dst_end = dst + sz - 1, aligned_dst_end, aligned_sz;
	unsigned int start_delta, end_delta;
	int stat;

	dfu_dbg("src and dst equally unaligned\n");
	start_delta = aligned_dst - dst;
	aligned_sz = _align_prev(sz - start_delta);
	aligned_dst_end = aligned_dst + aligned_sz - 1;
	end_delta = dst_end - aligned_dst_end;

	stat = _spi_flash_write(aligned_dst, aligned_src, aligned_sz);
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;
	/*
	 * Now read the first dword, replace 1 to 3 bytes and write
	 * back
	 */
	stat = _spi_flash_read(aligned_dst - sizeof(uint32), &v, sizeof(v));
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;

	_memcpy(((char *)&v) + sizeof(v) - start_delta, src, start_delta);
	stat = _spi_flash_write(aligned_dst, &v, sizeof(v));
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;
	/*
	 * If needed, finally read the last dword, replace
	 * 3 to 1 bytes and write back
	 */
	if (!end_delta)
		return SPI_FLASH_RESULT_OK;
	stat = _spi_flash_read(aligned_dst + aligned_sz, &v, sizeof(v));
	if (stat != SPI_FLASH_RESULT_OK)
		return stat;

	_memcpy((char *)&v, (char *)aligned_dst + aligned_sz, end_delta);
	return SPI_FLASH_RESULT_OK;
}

static int _flash_write(uint32 dst, void *src, uint32 sz)
{
	uint32 *aligned_src, aligned_dst;
	int stat, done;

	aligned_dst = _align_next(dst);
	aligned_src = _align_next_ptr(src);

	dfu_dbg("src = %p, aligned_src = %p, dst = 0x%08x, aligned_dst = 0x%08x\n", src, aligned_src, dst, aligned_dst);

	if (!(_ptr_diff(aligned_src, src)) && !(aligned_dst - dst))
		return _aligned_flash_write(dst, aligned_dst, src, aligned_src,
					    sz);

	if ((_ptr_diff(aligned_src, src)) == (aligned_dst - dst))
		return _equally_unaligned_flash_write(dst, aligned_dst, src,
						      aligned_src, sz);

	/*
	 * src and dst have different alignments: use temporary buffer
	 * based write
	 */
	dfu_dbg("src and dst have different alignments\n");
	for (done = 0; done < sz; done += stat) {
		stat = _slow_flash_write(dst + done, (char *)src + done,
					 sz - done);
		if (stat < 0)
			return SPI_FLASH_RESULT_ERR;
	}
	return SPI_FLASH_RESULT_OK;
}


static int _read_file_header(struct spi_flash_sector *s,
			     union spi_flash_sector_header *out)
{
	uint32 a = flash_sect_ptr_to_addr(s);

	if (a == NO_SECTOR_ADDR)
		return -1;
	return _flash_read(a, out->v, sizeof(*out)) != SPI_FLASH_RESULT_OK ?
	    -1 : 0;
}

static int _write_file_header(struct spi_flash_sector *s,
			      union spi_flash_sector_header *in)
{
	uint32 a = flash_sect_ptr_to_addr(s);

	if (a == NO_SECTOR_ADDR)
		return -1;
	return _flash_write(a, in->v, sizeof(*in)) != SPI_FLASH_RESULT_OK ?
	    -1 : 0;
}

static int _file_name_cmp(struct spi_flash_file_data *f, const char *name)
{
	union spi_flash_sector_header h;

	if (_read_file_header(f->sectors.next, &h) < 0)
		return -1;
	dfu_dbg("%s: h.s.name = %s, name = %s\n", __func__, h.s.name, name);
	return strcmp(h.s.name, name);
}

static int sectors_allocate_on_flash(struct spi_flash_file_data *f,
				     const char *name)
{
	int fsi;
	union spi_flash_sector_header h;
	struct spi_flash_sector *s;

	/* Set counters to all 0xff's and copy name */
	memset(&h, 0xff, sizeof(h));
	strncpy(h.s.name, name, sizeof(h.s.name) - 1);

	for_each_sector(s, &f->sectors) {

		fsi = flash_sect_ptr_to_index(s);

		if (_spi_flash_erase_sector(fsi) != SPI_FLASH_RESULT_OK)
			return -1;
		if (_flash_write(flash_sect_index_to_addr(fsi),
				 h.v, sizeof(h)) != SPI_FLASH_RESULT_OK)
			return -1;
	}
	return 0;
}

static int sector_deallocate_on_flash(struct spi_flash_file_data *f)
{
	int fsi;
	union spi_flash_sector_header h;
	struct spi_flash_sector *s;

	/* Set counters to all 0xff's and copy name */
	memset(&h, 0, sizeof(h));

	for_each_sector(s, &f->sectors) {
		fsi = flash_sect_ptr_to_index(s);
		if (_flash_write(flash_sect_index_to_addr(fsi),
				 h.v, sizeof(h)) != SPI_FLASH_RESULT_OK)
		    return -1;
	}
	return 0;
}

static struct spi_flash_file_data spifiles[MAX_FILES];

static int spi_flash_file_close(struct dfu_simple_file *f)
{
	struct spi_flash_file_data *_f = f->priv;
	union spi_flash_sector_header h;
	struct spi_flash_sector *s;
	unsigned long offs;

	if (!_f)
		return -1;
	if (!_f->written)
		return 0;
	/* Written something, let's store length if sector header(s) */
	offs = 0;
	for_each_sector(s, &_f->sectors) {
		if (_read_file_header(s, &h) < 0)
			return -1;
		h.s.start_offset = offs;
		h.s.data_size = min(_f->written, sectsize);
		if (_write_file_header(s, &h) <0)
			return -1;
		offs += h.s.data_size;
		if (offs >= _f->written)
			break;
	}
	return 0;
}

static int _spi_flash_file_wrrd(struct dfu_simple_file *f, const char *wrbuf,
				char *rdbuf, unsigned long _sz)
{
	struct spi_flash_file_data *_f = f->priv;
	struct spi_flash_sector *s;
	unsigned long offs;
	union spi_flash_sector_header h;
	unsigned long done, _rdwrsz;
	int i, first_sector, offset_in_sector, fsi;
	uint32 hdr_addr, data_addr;

	/* Either rdbuf or wrbuf must be non NULL */
	if (!((uint32_t)rdbuf ^ (uint32_t)wrbuf))
		return -1;

	first_sector = f->fileptr / sectdatasize;
	dfu_dbg("%s, first_sector = %u, sectdatasize = %lu\n", __func__,
		first_sector, sectdatasize);

	/*
	 * offs -> offset of current sector
	 * done -> actually read bytes
	 * i -> sector counter
	 */
	offs = 0; done = 0; i = 0;
	for_each_sector(s, &_f->sectors) {
		if (i < first_sector) {
			dfu_dbg("%s: skipping sector %d\n", __func__, i);
			offs += sectdatasize;
			i++;
			continue;
		}
		offset_in_sector = (i == first_sector) ? f->fileptr - offs : 0;
		dfu_dbg("%s: offset_in_sector = %d\n", __func__,
			offset_in_sector);
		fsi = flash_sect_ptr_to_index(s);
		dfu_dbg("%s: flash sector index: %d\n", __func__, fsi);
		if (rdbuf) {
			/* read operation only */
			hdr_addr = flash_sect_index_to_addr(fsi);
			dfu_dbg("%s: hdr_addr = %x\n", __func__, hdr_addr);
			if (_flash_read(hdr_addr, h.v, sizeof(h)) !=
			    SPI_FLASH_RESULT_OK)
				return -1;
			if (h.s.data_size == NO_FLASH_SIZE)
				/* File not completely written */
				return -1;
		}
		data_addr = flash_sect_index_to_data_addr(fsi) +
			offset_in_sector;
		dfu_dbg("%s: data_addr = %x\n", __func__, data_addr);
		_rdwrsz = min(_sz - done, sectdatasize - offset_in_sector);
		dfu_dbg("%s: done = %lu, _rdwrsz = %lu\n", __func__, done,
			_rdwrsz);
		/* FIXME: ALIGN BUFFER TO DOUBLE WORD ADDRESS ? */
		if (rdbuf) {
			if (_flash_read(data_addr, (uint32 *)(&rdbuf[done]),
					_rdwrsz) != SPI_FLASH_RESULT_OK)
				goto err;
		} else {
			dfu_dbg("src addr = %p\n", &wrbuf[done]);
			if (_flash_write(data_addr, (uint32 *)(&wrbuf[done]),
					    _rdwrsz) != SPI_FLASH_RESULT_OK)
				goto err;
		}
		done += _rdwrsz;
		if (done >= _sz)
			break;
		offs += sectdatasize;
		i++;
	}
	if (wrbuf)
		_f->written += done;
	return done;

err:
	dfu_err("%s: error accessing spi flash\n", __func__);
	return -1;
}

static int spi_flash_file_read(struct dfu_simple_file *f, char *buf,
			       unsigned long sz)
{
	struct spi_flash_file_data *_f = f->priv;
	unsigned long _sz;

	dfu_dbg("%s, path = %s, fileptr = %lu, written = %lu\n", __func__,
		f->path, f->fileptr, _f->written);

	/* _sz is actual size which can be read */
	_sz = min(sz, _f->written - f->fileptr);
	if (!_sz) {
		dfu_dbg("%s: size is 0\n", __func__);
		return 0;
	}
	return _spi_flash_file_wrrd(f, NULL, buf, _sz);
}

static int spi_flash_file_write(struct dfu_simple_file *f,
					const char *buf,
					unsigned long sz)
{
	struct spi_flash_file_data *_f = f->priv;

	dfu_dbg("%s, path = %s, fileptr = %lu, written = %lu\n", __func__,
		f->path, f->fileptr, _f->written);
	if (f->fileptr < _f->written) {
		dfu_err("trying to overwrite file\n");
		return -1;
	}
	if (_f->written + sz > _f->max_size) {
		dfu_err("not enough space for file\n");
		return -1;
	}
	return _spi_flash_file_wrrd(f, buf, NULL, sz);
}

static struct dfu_simple_file_ops spi_flash_file_ops = {
	.close = spi_flash_file_close,
	.read = spi_flash_file_read,
	.write = spi_flash_file_write,
};

static int spi_flash_fc_init(struct dfu_file_container *fc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(spifiles); i++)
		sector_list_init(&spifiles[i].sectors);
	fcdata.nsectors = sectors_end - sectors_start;
	dfu_dbg("%s: nsectors = %d\n", __func__, fcdata.nsectors);
	/* Build list of free sectors */
	sector_list_init(&fcdata.free_head);
	for (i = 0; i < fcdata.nsectors; i++)
		sector_add(&fcdata.free_head, &sectors_start[i]);
	dfu_dbg("%s: initialized free list (%d elements)\n",
		__func__, sectors_list_count(&fcdata.free_head));
	return 0;
}

static int _get_sectors(unsigned long max_size, const char *name,
			struct spi_flash_sector *out)
{
	int i;
	int num = max_size / sectdatasize + ((max_size % sectdatasize) ? 1 : 0);
	struct spi_flash_sector *s;

	dfu_dbg("%s: max_size = %lu, num = %d\n", __func__, max_size, num);
	sector_list_init(out);
	if (sectors_list_count(&fcdata.free_head) < num) {
		dfu_err("%s: not enough free sectors\n", __func__);
		return -1;
	}

	for (i = 0; i < num; i++) {
		s = fcdata.free_head.next;
		/* Move sector to output list */
		sector_del(s);
		sector_add(out, s);
		dfu_dbg("%s: adding sector %d, flash index %d\n", __func__,
			sect_ptr_to_index(s), flash_sect_ptr_to_index(s));
	}
	dfu_dbg("%s: SUMMARY for file %s\n", __func__, name);
	for_each_sector(s, out)
		dfu_dbg("\tsector %d, flash index %d\n",
			sect_ptr_to_index(s), flash_sect_ptr_to_index(s));
	return num;
}

static struct spi_flash_file_data *_get_file_data(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(spifiles); i++)
		if (!sectors_list_count(&spifiles[i].sectors)) {
			spifiles[i].written = 0;
			return &spifiles[i];
		}
	dfu_err("%s: too many files\n", __func__);
	return NULL;
}

static struct spi_flash_file_data *_new_file(const char *name,
					     unsigned long max_size)
{
	int nsectors;
	struct spi_flash_file_data *out = NULL;

	out = _get_file_data();
	if (!out)
		return out;
	nsectors = _get_sectors(max_size, name, &out->sectors);
	if (nsectors < 0)
		return NULL;
	out->max_size = max_size;

	/* Physically erase sector(s) and write name on flash header(s) */
	sectors_allocate_on_flash(out, name);
	return out;
}

static struct spi_flash_file_data *_find_file(const char *name)
{
	int i;

	dfu_dbg("%s(%s)\n", __func__, name);
	for (i = 0; i < ARRAY_SIZE(spifiles); i++) {
		if (sectors_list_empty(&spifiles[i].sectors)) {
			dfu_dbg("file %d empty, skipping\n", i);
			continue;
		}
		if (!_file_name_cmp(&spifiles[i], name))
			return &spifiles[i];
	}
	dfu_dbg("%s: %s not found\n", __func__, name);
	return NULL;
}

static int spi_flash_fc_open(struct dfu_file_container *fc,
				     struct dfu_simple_file *f,
				     const char *name,
				     int create_if_not_found,
				     unsigned long max_size)
{
	struct spi_flash_file_data *_f;

	_f = _find_file(name);
	if (!_f) {
		if (!create_if_not_found) {
			dfu_err("%s: file %s not found and cannot create it\n",
				__func__, name);
			return -1;
		}
		_f = _new_file(name, max_size);
		if (!_f)
			return -1;
	}
	f->priv = _f;
	f->ops = &spi_flash_file_ops;
	dfu_dbg("%s: file %s opened\n", __func__, name);
	return 0;
}

static int spi_flash_fc_remove(struct dfu_file_container *fc,
			       const char *name)
{
	struct spi_flash_file_data *_f = _find_file(name);
	struct spi_flash_sector *s, *tmp;

	if (!_f) {
		dfu_err("%s: cannot find %s\n", __func__, name);
		return -1;
	}
	/* Set whole header to 0 to indicate free sector */
	sector_deallocate_on_flash(_f);
	for_each_sector_safe(s, tmp, &_f->sectors) {
		sector_del(s);
		sector_add(&fcdata.free_head, s);
		dfu_dbg("%s: sector %d freed\n", __func__,
			sect_ptr_to_index(s));
	}
	dfu_dbg("%s: file %s removed\n", __func__, name);
	return 0;
}

struct dfu_file_container_ops spi_flash_fc_ops = {
	.init = spi_flash_fc_init,
	.open_file = spi_flash_fc_open,
	.remove_file = spi_flash_fc_remove,
};
