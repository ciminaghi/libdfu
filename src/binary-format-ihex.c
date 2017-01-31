#include "dfu.h"
#include "dfu-internal.h"

enum ihex_record_type {
	IHEX_DATA = 0,
	IHEX_EOF = 1,
	IHEX_EXT_SEG_ADDRESS = 2,
	IHEX_START_SEG_ADDRESS = 3,
	IHEX_EXT_LINEAR_ADDRESS = 4,
	IHEX_START_LINEAR_ADDRESS = 5,
};

struct ihex_line_data {
	int start_index;
	int data_start_index;
	int byte_count;
	int address;
	enum ihex_record_type record_type;
};

struct ihex_format_data {
	uint32_t curr_addr;
};

/* Just one instance for the moment */
static struct ihex_format_data ihdata;

static inline uint32_t _hi_addr(uint32_t a)
{
	return a & 0xffff0000;
}

static inline int __go_on(int index, int amount, int buf_size)
{
	return (index + amount) & (buf_size - 1);
}

static inline int _go_on(struct dfu_binary_file *f, int index, int amount)
{
	return __go_on(index, amount, f->max_size);
}

static inline int _dec_go_on(struct dfu_binary_file *f, int index, int amount)
{
	return __go_on(index, amount, f->decoded_size);
}

static inline int _next(struct dfu_binary_file *f, int index)
{
	return _go_on(f, index, 1);
}

static inline int _dec_next(struct dfu_binary_file *f, int index)
{
	return _dec_go_on(f, index, 1);
}

static inline int _hex_to_int(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/*
 * Decode given number of ascii chars (nbytes), part of a single multibyte
 * (big endian) quantity or of a buffer of bytes, taking data from an
 * input buffer starting at offset *index.
 * Update *index and stop at buffer's head or when all bytes have
 * been decoded. Return number of bytes decoded ( > 0), not enough input
 * bytes ( == 0) or error ( < 0)
 */
static int _decode_hex(struct dfu_binary_file *f, int *index, int nbytes,
		       int *out, int *out_index)
{
	int i, stat = 0, ret;
	char *ptr = f->buf, *dst;
	int _out = 0;

	if (out && (nbytes > sizeof(*out) * 2))
		return -1;
	for (i = 0; i < nbytes && *index != f->head;
	     i++, *index = _next(f, *index)) {
		stat = _hex_to_int(ptr[*index]);
		if (stat < 0)
			return stat;
		/* big endian */
		_out |= (stat << (4 * (nbytes - (i + 1))));
		/* FIXME: do this faster (fewer *out_index updates ?) */
		if (out_index) {
			dst = &((char *)f->decoded_buf)[*out_index];
			if (!(i & 0x1)) {
				*dst = (stat << 4);
				continue;
			}
			*dst |= stat;
			*out_index = _dec_next(f, *out_index);
		}
	}
	if (out)
		*out = _out;
	ret = *index == f->head ? 0 : i;
	dfu_dbg("%s returns %d\n", __func__, ret);
	return ret;
}

static inline int _decode_hex_byte(struct dfu_binary_file *f, int *index,
				   int *out)
{
	return _decode_hex(f, index, 2, out, NULL);
}

static inline int _decode_hex_word(struct dfu_binary_file *f, int *index,
				   int *out)
{
	return _decode_hex(f, index, 4, out, NULL);
}

static inline int _decode_hex_dword(struct dfu_binary_file *f, int *index,
				    int *out)
{
	return _decode_hex(f, index, 8, out, NULL);
}

static inline int _decode_hex_buf(struct dfu_binary_file *f, int *index,
				  int *out_index, int len)
{
	return _decode_hex(f, index, len, NULL, out_index);
}


/*
 * Check line's checksum
 * Assumes f->tail points to checksum start and that the checksum field
 * in *ld is up to date
 * Updates bf->tail
 */
static int _check_line(struct dfu_binary_file *bf, struct ihex_line_data *ld)
{
	return 2;
}

/*
 * Search for the beginning of a line, update index and return number of
 * bytes read before finding the ':'
 */
static inline int _search_line(struct dfu_binary_file *f, int *index)
{
	char *ptr = f->buf;
	int ret;

	*index = f->tail;
	for (ret = 0; ptr[*index] != ':' && *index != f->head;
	     *index = _next(f, *index), ret++);
	if (ptr[*index] != ':')
		return 0;
	*index = _next(f, *index);
	return ret + 1;
}

/*
 * Look the beginning of the next line (':') and decode its header
 * Return number of bytes read from the buffer (note that f->tail is NOT
 * updated)
 */
static int _peek_line_header(struct dfu_binary_file *f,
			     struct ihex_line_data *odata)
{
	int index = f->tail, stat, rt, ret = 0, line_length;

	stat = _search_line(f, &index);
	if (stat <= 0)
		return stat;
	ret += stat;
	odata->start_index = index;
	stat = _decode_hex_byte(f, &index, &odata->byte_count);
	dfu_dbg("%s: byte count = %u\n", __func__, odata->byte_count);
	if (stat <= 0)
		return stat;
	ret += stat;
	stat = _decode_hex_word(f, &index, &odata->address);
	dfu_dbg("%s: address = 0x%04x\n", __func__, odata->address);
	if (stat <= 0)
		return stat;
	ret += stat;
	stat = _decode_hex_byte(f, &index, &rt);
	dfu_dbg("%s: record type = 0x%02x\n", __func__, rt);
	if (stat <= 0)
		return stat;
	ret += stat;
	odata->record_type = rt;
	if (odata->record_type < IHEX_DATA ||
	    odata->record_type > IHEX_START_LINEAR_ADDRESS)
		return -1;
	odata->data_start_index = index;
	/* Data bytes + header length (without ':') + checksum */
	line_length = odata->byte_count * 2 + ret + 2;
	/* A whole line cannot be read, give up */
	dfu_dbg("%s, count = %d, line_length = %d\n", __func__,
		bf_count(f), line_length);
	if (bf_count(f) < line_length)
		return 0;
	return ret;
}

/* Intel HEX, check for correct first line header */
int ihex_probe(struct dfu_binary_file *f)
{
	int cnt = bf_count(f), stat;
	struct ihex_line_data ld;
	struct ihex_format_data *fd = &ihdata;

	if (cnt < 9)
		/* Buffer does not contain a line header */
		return -1;
	/* Check whether the file contains a valid line header */
	stat = _peek_line_header(f, &ld);
	if (stat <= 0)
		return stat;
	dfu_log("Intel HEX format probed\n");
	/* Format probed, initialize private data */
	f->format_data = fd;
	fd->curr_addr = 0;
	return 0;
}

#ifdef DEBUG
static void _print_bad_line(struct dfu_binary_file *bf,
			    struct ihex_line_data *ld, int index)
{
	int i, j;

	dfu_log("bad line:\n");
	for (i = 0, j = index; i < ld->byte_count; i++,
		     j = _next(bf, j))
		dfu_log_noprefix("0x%02x ", ((char *)bf->buf)[j]);
}
#else
static void _print_bad_line(struct dfu_binary_file *bf,
			    struct ihex_line_data *ld, int index)
{
}
#endif

/*
 * Decode a data line. bf->tail must point to line's data section
 */
static int _decode_data_line(struct dfu_binary_file *bf,
			     struct ihex_line_data *ld)
{
	int index, out_index, stat, ret = 0, space;

	space = bf_dec_space(bf);
	dfu_dbg("%s: space = %lu, ld->byte_count = %d\n", __func__, space,
		ld->byte_count);
	if (space < ld->byte_count)
		/* Not enough bytes in output buffer, do nothing */
		return 0;
	if (ld->data_start_index != bf->tail) {
		dfu_err("internal inconsistency\n");
		return -1;
	}
	index = bf->tail;
	out_index = bf->decoded_head;
	stat = _decode_hex_buf(bf, &index, &out_index, ld->byte_count * 2);
	dfu_dbg("%s: decoded %d bytes\n", __func__, stat);
	if (stat <= 0) {
		_print_bad_line(bf, ld, index);
		return stat;
	}
	ret += stat;
	bf->tail = _go_on(bf, bf->tail, stat);
	bf->decoded_head = out_index;
	dfu_dbg("%s: new tail is %d, decoded_head is %d\n", __func__, bf->tail,
		bf->decoded_head);
	/* Check line (checksum) */
	stat = _check_line(bf, ld);
	dfu_dbg("%s: _check_line() returns %d\n", __func__, stat);
	if (stat < 0)
		return stat;
	ret += stat;
	bf->tail = _go_on(bf, bf->tail, stat);
	dfu_dbg("%s: final tail = %d\n", __func__, bf->tail);
	return ret;
}

/*
 * Decode new file chunk (some lines in general)
 * Stop on line boundary
 */
int ihex_decode_chunk(struct dfu_binary_file *bf, uint32_t *addr)
{
	struct ihex_line_data ld;
	int stat, index, tot, decoded_tot, stopit;
	struct ihex_format_data *priv = bf->format_data;
	uint32_t curr_addr, next_addr = 0;

	for (stopit = 0, tot = 0, decoded_tot = 0; !bf->rx_done && !stopit; ) {
		stat = _peek_line_header(bf, &ld);
		if (stat <= 0) {
			dfu_dbg("%s %d, stat = %d, decoded_tot = %d\n",
				__func__, __LINE__, stat, decoded_tot);
			return stat < 0 ? stat : decoded_tot;
		}
		switch (ld.record_type) {
		case IHEX_DATA:
			curr_addr = _hi_addr(priv->curr_addr) | ld.address;
			if (tot && curr_addr != next_addr) {
				/*
				 * Address jump, discard line and stop
				 * decoding
				 */
				dfu_dbg("%s: address jump\n", __func__);
				return decoded_tot;
			}
			/* peek line header does not update tail, do it now */
			bf->tail = _go_on(bf, bf->tail, stat);
			dfu_dbg("%s: tail = %d\n", __func__, bf->tail);
			if (!tot)
				*addr = curr_addr;
			/*
			 * Calculate next line's start address, we'll have
			 * to bail out in case next_line's address does not
			 * match the calculated one (output buffer shall contain
			 * countiguous data)
			 */
			next_addr = curr_addr + ld.byte_count;
			/* decode line and write data to output buffer */
			/* note that _decode_data_line() verifies checksum */
			stat = _decode_data_line(bf, &ld);
			if (stat <= 0) {
				dfu_dbg("%s %d stat = %d\n", __func__, __LINE__,
					stat);
				return stat;
			}
			tot += stat;
			decoded_tot += ld.byte_count;
			if (!stat)
				return decoded_tot;
			break;
		case IHEX_EOF:
			/* peek line header does not update tail, do it now */
			bf->tail = _go_on(bf, bf->tail, stat);
			bf->rx_done = 1;
			/* Force written flag to 1 */
			dfu_binary_file_append_buffer(bf, NULL, 0);
			dfu_log("IHEX: file ended\n");
			break;
		case IHEX_EXT_SEG_ADDRESS:
			/* Unsupported at the moment */
			dfu_err("unsupported ihex file type\n");
			return -1;
		case IHEX_START_LINEAR_ADDRESS:
		case IHEX_START_SEG_ADDRESS:
		case IHEX_EXT_LINEAR_ADDRESS:
		{
			int a;

			index = ld.data_start_index;
			/* get new address */
			stat = ld.record_type == IHEX_EXT_LINEAR_ADDRESS ?
				_decode_hex_word(bf, &index, &a) :
				_decode_hex_dword(bf, &index, &a);
			if (stat <= 0)  {
				dfu_err("IHEX: not enough bytes for addr\n");
				return stat;
			}
			if (ld.record_type == IHEX_EXT_LINEAR_ADDRESS)
				a <<= 16;
			bf->tail = index;
			/* verify checksum */
			stat = _check_line(bf, &ld);
			if (stat < 0) {
				dfu_err("IHEX: invalid checksum\n");
				return stat;
			}
			if (ld.record_type == IHEX_START_SEG_ADDRESS ||
			    ld.record_type == IHEX_START_LINEAR_ADDRESS) {
				dfu_log("IHEX Entry: 0x%08x\n", a);
				dfu_target_set_entry(bf->dfu, a);
				break;
			}
			priv->curr_addr = a;
			next_addr = a;
			dfu_log("IHEX, new address: 0x%08x\n",
				/* esp8266: uint32_t is unsigned long ! */
				(unsigned int)priv->curr_addr);
			dfu_dbg("tail = %d\n", bf->tail);
		}
		}
	}
	return decoded_tot;
}

static int ihex_fini(struct dfu_binary_file *bf)
{
	return 0;
}

declare_dfu_format(ihex, ihex_probe, ihex_decode_chunk, ihex_fini);
