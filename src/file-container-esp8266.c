/*
 * libdfu, implementation of file container for esp8266 spi flash
 * LGPL v2.1
 * Copyright What's Next GmbH 2018
 * Author Davide Ciminaghi 2018
 */
#include <dfu.h>
#include <dfu-internal.h>

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
	return sector->next != sector && sector->prev != sector;
}

#define for_each_sector(s, l)						\
	for (s = (l)->next; (s) != l; s = s->next)

static inline int sectors_list_count(struct spi_flash_sector *l)
{
	struct spi_flash_sector *s;
	int out = 0;

	for_each_sector(s, l)
		out++;
	return out;
}

static int _read_file_header(struct spi_flash_sector *s,
			     union spi_flash_sector_header *out)
{
	uint32 a = flash_sect_ptr_to_index(s);

	if (a == NO_SECTOR_ADDR)
		return -1;
	return spi_flash_read(a, out->v, sizeof(*out)) != SPI_FLASH_RESULT_OK ?
	    -1 : 0;
}

static int _write_file_header(struct spi_flash_sector *s,
			      union spi_flash_sector_header *in)
{
	uint32 a = flash_sect_ptr_to_index(s);

	if (a == NO_SECTOR_ADDR)
		return -1;
	return spi_flash_write(a, in->v, sizeof(*in)) != SPI_FLASH_RESULT_OK ?
	    -1 : 0;
}

static int _file_name_cmp(struct spi_flash_file_data *f, const char *name)
{
	union spi_flash_sector_header h;

	if (_read_file_header(f->sectors.next, &h) < 0)
		return -1;
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

		if (spi_flash_erase_sector(fsi) != SPI_FLASH_RESULT_OK)
			return -1;
		if (spi_flash_write(flash_sect_index_to_addr(fsi),
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
		if (spi_flash_write(flash_sect_index_to_addr(fsi),
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
			if (spi_flash_read(hdr_addr, h.v, sizeof(h)) !=
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
			if (spi_flash_read(data_addr, (uint32 *)(&rdbuf[done]),
					   _rdwrsz) != SPI_FLASH_RESULT_OK)
				goto err;
		} else {
			if (spi_flash_write(data_addr, (uint32 *)(&wrbuf[done]),
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
	/* Build list of free sectors */
	sector_list_init(&fcdata.free_head);
	for (i = 0; i < fcdata.nsectors; i++)
		sector_add(&fcdata.free_head, &sectors_start[i]);
	return 0;
}

static int _get_sectors(unsigned long max_size, const char *name,
			struct spi_flash_sector *out)
{
	int i;
	int num = max_size / sectsize + (max_size % sectsize) ? 1 : 0;
	struct spi_flash_sector *s;

	dfu_dbg("%s: num = %d\n", __func__, num);
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

	for (i = 0; i < ARRAY_SIZE(spifiles); i++) {
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
	struct spi_flash_sector *s;

	if (!_f) {
		dfu_err("%s: cannot find %s\n", __func__, name);
		return -1;
	}
	/* Set whole header to 0 to indicate free sector */
	sector_deallocate_on_flash(_f);
	for_each_sector(s, &_f->sectors) {
		sector_del(s);
		sector_add(s, &fcdata.free_head);
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
