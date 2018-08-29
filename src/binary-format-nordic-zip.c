#include <ctype.h>
#include "dfu.h"
#include "dfu-internal.h"
#include "jsmn.h"

/*
 * Zip file format, see:
 * https://pkware.cachefly.net/webdocs/casestudies/APPNOTE.TXT
 * https://users.cs.jmu.edu/buchhofp/forensics/formats/pkzip.html
 *
 * Contents of zip archive and dfu mechanism, see:
 * https://github.com/NordicSemiconductor/pc-nrfutil/blob/master/nordicsemi/dfu/dfu.py
 *
 * Manifest file format, see:
 * https://github.com/NordicSemiconductor/pc-nrfutil/blob/master/nordicsemi/dfu/tests/test_manifest.py
 *
 */

/* This is arbitrary */
#define MAX_FNAME 32

#define MAX_NTOKENS 40

struct zip_local_file_header_base {
	uint32_t signature;
	uint16_t version;
#define ENCRYPTED_FILE		BIT(0)
#define DATA_DESCRIPTOR		BIT(3)
#define STRONG_ENCRYPTION	BIT(6)
	uint16_t flags;
	uint16_t compression;
	uint16_t mod_time;
	uint16_t mod_date;
	uint32_t crc32;
	uint32_t compressed_size;
	uint32_t uncompressed_size;
	uint16_t file_name_len;
	uint16_t extra_field_len;
} __attribute__((packed));

union zip_local_file_header {
	struct {
		struct zip_local_file_header_base base;
		/* File name and extra field */
		uint8_t  file_and_extra[0];
	} __attribute__((packed)) s;
	char c[sizeof(struct zip_local_file_header_base) + MAX_FNAME];
} __attribute__((packed));

struct zip_central_dir_file_header_base {
	struct zip_local_file_header_base lfh;
	uint16_t file_comment_len;
	uint16_t disk_n_start;
	uint16_t internal_attr;
	uint32_t external_attr;
	uint32_t local_header_offs;
};

union zip_central_dir_file_header {
	struct {
		struct zip_central_dir_file_header_base base;
		/* File name, extra and comment */
		uint8_t file_extra_and_comment[0];
	} __attribute__((packed)) s;
	char c[sizeof(struct zip_central_dir_file_header_base)];
};

union zip_end_of_central_dir_header {
	struct {
		uint32_t signature;
		uint16_t disk_n;
		uint16_t disk_n_wcd;
		uint16_t disk_entries;
		uint16_t total_entries;
		uint32_t central_dir_size;
		uint32_t central_dir_offs;
		uint16_t comment_len;
		uint8_t comment[0];
	} __attribute__((packed)) s;
	char c[0];
};

struct received_file {
	char name[MAX_FNAME];
	char *ext;
	unsigned int size;
	unsigned int done;
	int fd;
	/* Short files are received here */
	char *local_buf;
	unsigned int local_buf_offset;
	unsigned int local_buf_size;
};

struct firmware_image {
	char name[MAX_FNAME];
	struct received_file *dat_file;
	struct received_file *bin_file;
	/* used by soft device + bootloader images only */
	unsigned long bl_size;
	unsigned long sd_size;
};

enum zip_header_type {
	INVALID = -1,
	LOCAL_HEADER,
	CENTRAL_HEADER,
	END_CENTRAL_HEADER,
};

enum nzstate {
	IDLE = 0,
	IGNORING_FILE,
	STORING_FILE,
	SENDING_COMMAND,
	SENDING_DATA,
	ALL_DONE,
};

#define MAX_NFILES 8

#define MAX_NIMAGES 4

#define MAX_MANIFEST_SIZE 600

struct nordic_zip_format_data {
	enum nzstate state;
	int rx_file_count;
	int image_count;
	int send_image_index;
	unsigned int ignored;
	unsigned int ignored_size;
	struct received_file *curr_rf;
	struct received_file files[MAX_NFILES];
	struct firmware_image images[MAX_NIMAGES];
	struct firmware_image *parser_curr_image;
	char manifest_buffer[MAX_MANIFEST_SIZE];
};

/* Just one instance for the moment */
static struct nordic_zip_format_data nzdata;

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

/*
 * Look for local file header signature
 * Returned number of bytes consumed
 */
int _search_signature(struct dfu_binary_file *f, int *index,
		      enum zip_header_type *t)
{
	char *ptr = f->buf;
	int consumed, signature_start;
	/* Signature is actually 4 bytes, but the first 2 are always
	   0x50 0x4b */
	char signature[] = {
		0x50,
		0x4b,
	}, next;
	enum zip_header_type out = INVALID;
	
	*index = f->tail;
	consumed = 0;
	do {
		/* Look for first signature char */
		for ( ; ptr[*index] != signature[0] && *index != f->head;
		     *index = _next(f, *index), consumed++);
		if (*index == f->head)
			/* Not enough bytes */
			return 0;
		consumed++;
		/* ptr[*index] == signature[0], look for the remaining chars */
		signature_start = *index;
		*index = _next(f, *index);
		if (*index == f->head)
			/* Not enough bytes */
			return 0;
		consumed++;
		if (ptr[*index] != signature[1])
			continue;
		*index = _next(f, *index);
		if (*index == f->head)
			/* Not enough bytes */
			return 0;
		consumed++;
		switch (ptr[*index]) {
		case 0x01:
			next = 0x02;
			out = CENTRAL_HEADER;
			break;
		case 0x03:
			next = 0x04;
			out = LOCAL_HEADER;
			break;
		case 0x05:
			out = END_CENTRAL_HEADER;
			next = 0x06;
			break;
		default:
			next = 0xff;
			break;
		}
		if (next == 0xff)
			continue;
		*index = _next(f, *index);
		if (*index == f->head)
			/* Not enough bytes */
			return 0;
		consumed++;
		if (ptr[*index] == next) {
			*index = signature_start;
			*t = out;
			break;
		}
	} while(1);
	return consumed;
}

/*
 * Look for a local file header and copy it into @lfh
 * Return number of bytes consumed
 */
int _peek_local_file_header(struct dfu_binary_file *f,
			    union zip_local_file_header *lfh, int start)
{
	int index = f->tail, stat, i;
	char *ptr;
	enum zip_header_type t = LOCAL_HEADER;
	
	if (start < 0) {
		stat = _search_signature(f, &index, &t);
		if (stat <= 0)
			return stat;
		if (t != LOCAL_HEADER)
			return -1;
		/*
		 * Restart counting (the for cycle below copies the whole
		 * header again starting from the signature)
		 */
		stat = 0;
	} else {
		stat = 0;
		index = start;
	}
	for (i = 0, ptr = lfh->c; i < sizeof(lfh->s.base) && index != f->head;
	     i++, ptr++, index = _next(f, index), stat++)
		*ptr = ((char *)f->buf)[index];
	if (index == f->head)
		/* Not enough characters in buffer */
		return 0;
	if (lfh->s.base.file_name_len > MAX_FNAME) {
		dfu_err("FILE NAME IS TOO LONG\n");
		return -1;
	}
	/* Read file name, we neglect the extra data for the moment */
	for (i = 0; i < lfh->s.base.file_name_len && index != f->head;
	     i++, ptr++,
		     index = _next(f, index), stat++)
		*ptr = ((char *)f->buf)[index];
	if (index == f->head)
		/* Not enough characters in buffer */
		return -1;
	if (bf_count(f) < sizeof(lfh->s.base) + lfh->s.base.file_name_len +
	    lfh->s.base.extra_field_len)
		/* Extra field not yet in buffer */
		return -1;
	/* Pretend we took the extra field too out of the file's buffer */
	stat += lfh->s.base.extra_field_len;
	return stat;
}

/* Nordic zip header, check we're dealing with a zip file at least */
int nz_probe(struct dfu_binary_file *f)
{
	int stat;
	struct nordic_zip_format_data *fd = &nzdata;
	union zip_local_file_header zlh;

	/* Check whether the file contains a valid header */
	stat = _peek_local_file_header(f, &zlh, -1);
	if (stat <= 0)
		return -1;
	dfu_log("ZIP format probed\n");
	/* Format probed, initialize private data */
	f->format_data = fd;
	fd->rx_file_count = 0;
	fd->send_image_index = 0;
	/* Zero out file structures to mark them free */
	memset(fd->files, 0, sizeof(fd->files));
	return 0;
}

static void _reset(struct nordic_zip_format_data *priv)
{
	/* Close and unlink all temporary open files */

	/* Reset dara structure */
	memset(priv, 0, sizeof(*priv));
}

static struct received_file *_get_rx_file(struct nordic_zip_format_data *priv,
					  union zip_local_file_header *zlh)
{
	char name[MAX_FNAME];
	char *ext;
	struct received_file *out = NULL;
	int i;

	memcpy(name,  zlh->s.file_and_extra, zlh->s.base.file_name_len);
	name[zlh->s.base.file_name_len] = 0;

	ext = strchr(name, '.');
	if (!ext)
		/* No extension */
		return NULL;
	if (!strcmp(ext, ".json")) {
		/* json file, __must__ be the first, position 0 */
		if (priv->files[0].name[0]) {
			/* BUSY */
			dfu_err("WARNING: .json file already received\n");
			return NULL;
		}
		out = &priv->files[0];
		if (zlh->s.base.uncompressed_size >
		    sizeof(priv->manifest_buffer)) {
			dfu_err("ERROR: manifest is too big\n");
			return NULL;
		}
		out->local_buf = priv->manifest_buffer;
		out->local_buf_offset = 0;
		out->local_buf_size = sizeof(priv->manifest_buffer);
	} else if (!strcmp(ext, ".bin") || !strcmp(ext, ".dat")) {
		/* Look for a free file, starting from position 1 */
		for (i = 1; i < MAX_NFILES; i++)
			if (!priv->files[i].name[0]) {
				out = &priv->files[i];
				break;
			}
	}
	if (!out)
		return out;
	strncpy(out->name, name, MAX_FNAME);
	out->ext = out->name + (ext - name);
	out->size = zlh->s.base.uncompressed_size;
	out->done = 0;
	return out;
}

static int _decode_local_header(struct dfu_binary_file *bf, int start)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	union zip_local_file_header zlh;
	struct received_file *rf;
	struct dfu_data *dfu = bf->dfu;
	int stat;

	/* Look for local file header */
	stat = _peek_local_file_header(bf, &zlh, start);
	if (stat <= 0)
		return stat;
	if (zlh.s.base.compression) {
		dfu_err("zip with compressed data is unsupported\n");
		return -1;
	}
	if (zlh.s.base.flags & DATA_DESCRIPTOR) {
		dfu_err("zip with data descriptors is unsupported\n");
		return -1;
	}
	if (zlh.s.base.flags & (ENCRYPTED_FILE | STRONG_ENCRYPTION)) {
		dfu_err("encryption is not supported\n");
		return -1;
	}
	if (zlh.s.base.file_name_len > MAX_FNAME - 1) {
		dfu_err("%s: file name is too long\n", __func__);
		priv->state = IGNORING_FILE;
		priv->ignored = 0;
		priv->ignored_size = zlh.s.base.uncompressed_size;
		return stat;
	}
	rf = _get_rx_file(priv, &zlh);
	if (!rf) {
		/* Ignore received file */
		priv->state = IGNORING_FILE;
		return stat;
	}
	priv->curr_rf = rf;
	priv->state = STORING_FILE;
	rf->fd = dfu_file_open(dfu, rf->name, 1, rf->size);
	if (rf->fd < 0) {
		dfu_err("%s: could not open file %s\n", __func__,
			rf->name);
		_reset(priv);
	}
	dfu_dbg("%s returns %d\b", __func__, stat);
	return stat;
}

static struct received_file *
_search_rx_file(struct nordic_zip_format_data *priv, const char *name,
		int namelen)
{
	int i;
	struct received_file *out = NULL;

	for (i = 0; i < priv->rx_file_count; i++) {
		if (!memcmp(name, priv->files[i].name, namelen)) {
			out = &priv->files[i];
			return out;
		}
	}
	return out;
}

static int _decode_central_header(struct dfu_binary_file *bf, int start)
{
	int index = start, stat, i;
	char *ptr;
	union zip_central_dir_file_header zcdfh;

	if (bf_count(bf) < sizeof(zcdfh.s.base))
		/* Not enough chars in buffer */
		return 0;
	for (i = 0, ptr = zcdfh.c;
	     i < sizeof(zcdfh.s.base);
	     i++, ptr++, index = _next(bf, index), stat++)
		*ptr = ((char *)bf->buf)[index];
	if (index == bf->head)
		/* Not enough chars in buffer */
		return 0;
	/* We just throw everything away */
	if (bf_count(bf) < sizeof(zcdfh) + zcdfh.s.base.file_comment_len)
		/* Not enough chars in buffer */
		return 0;
	return sizeof(zcdfh) + zcdfh.s.base.file_comment_len;
}

static void _all_done(struct dfu_binary_file *bf)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	
	bf->rx_done = 1;
	/* Force written flag to 1 */
	dfu_binary_file_append_buffer(bf, NULL, 0);
	priv->state = ALL_DONE;
	dfu_log("NORDIC ZIP: file written\n");
}

static void _next_file(struct dfu_binary_file *bf, struct firmware_image *fi)
{
	struct nordic_zip_format_data *priv = bf->format_data;

	switch (priv->state) {
	case SENDING_COMMAND:
		priv->state = SENDING_DATA;
		return;
	case SENDING_DATA:
		priv->send_image_index++;
		if (!priv->images[priv->send_image_index].name[0]) {
			/* NO MORE IMAGES TO SEND */
			_all_done(bf);
			break;
		}
	default:
		dfu_err("%s: invoked with wrong state\n", __func__);
		break;
	}
	/* NEVER REACHED */
}

static uint32_t _state_to_address(enum nzstate state, int image_index)
{
	uint32_t out = state == SENDING_COMMAND ? 0 : NZ_FWFILE_DATA_FLAG;

	dfu_dbg("out = 0x%08x\n", (unsigned int)out);
	out += (image_index << NZ_FWFILE_IMAGE_SHIFT);
	return out;
}

static int _send_file_chunk(struct dfu_binary_file *bf, uint32_t *addr)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	struct firmware_image *fi = &priv->images[priv->send_image_index];
	struct received_file *rf;
	unsigned int sz;
	char *ptrd = bf->decoded_buf;	
	int stat;

	rf = priv->state == SENDING_COMMAND ? fi->dat_file : fi->bin_file;
	dfu_dbg("%s: sending chunk of file %s\n", __func__, rf->name);

	sz = min(bf_dec_space_to_end(bf), rf->size - rf->done);
	/* NO BIGGER THAN OUR MTU, FIXME ? */
	sz = min(128, sz);
	dfu_dbg("%s: chunk size = %u\n", __func__, sz);
	if (!sz)
		/* No space in decoded buffer */
		return 0;
	if (!rf->done) {
		/* First chunk, close and reopen to reset file offset */
		dfu_file_close(bf->dfu, rf->fd);
		rf->fd = dfu_file_open(bf->dfu, rf->name, 0, 0);
		if (rf->fd < 0) {
			dfu_err("%s: cannot reopen file %s\n",
				__func__, rf->name);
			return -1;
		}
	}
	stat = dfu_file_read(bf->dfu, rf->fd, &ptrd[bf->decoded_head], sz);
	if (stat < 0) {
		dfu_err("%s: error reading from file\n", __func__);
		return stat;
	}
	/* Update decoded head */
	bf->decoded_head = _go_on(bf, bf->decoded_head, sz);
	*addr = _state_to_address(priv->state, priv->send_image_index) +
		rf->done;
	dfu_dbg("%s: address = 0x%08x\n", __func__, (unsigned int)*addr);
	/* Update number of sent bytes */
	rf->done += sz;
	if (rf->done >= rf->size) {
		/* We're done with this file, close and remove it */
		dfu_file_close(bf->dfu, rf->fd);
		dfu_file_remove(bf->dfu, rf->name);
		/* Go on to next file */
		_next_file(bf, fi);
	}
	return sz;
}

static int _realign_decoded_head(struct dfu_binary_file *bf, uint32_t *addr)
{
	int sz;

	if (!(bf->decoded_head % bf->write_chunk_size))
		return 0;
	/*
	 * Command file has probably broken decoded_head alignment
	 * Let's put in a throw away chunk to fix things up
	 */
	*addr = NZ_FWFILE_THROW_AWAY;
	sz = bf->write_chunk_size - (bf->decoded_head % bf->write_chunk_size);
	bf->decoded_head = _go_on(bf, bf->decoded_head, sz);
	return sz;
}

static struct firmware_image *
new_firmware_image(struct nordic_zip_format_data *priv, const char *name,
		   int name_len)
{
	int i;

	for (i = 0; i < MAX_NIMAGES; i++) {
		struct firmware_image *img = &priv->images[i];

		if (!img->name[0]) {
			int sz = min(MAX_FNAME - 1, name_len);

			memcpy(img->name, name, sz);
			img->name[sz] = 0;
			return img;
		}
	}
	dfu_err("%s: no free firmware image struct\n", __func__);
	return NULL;
}

static void _free_firmware_image(struct firmware_image *img)
{
	img->name[0] = 0;
}

/*
 * Manifest parsing
 */

static int new_image(const struct json_node *n, const jsmntok_t *t,
		     const char *buf, void *_priv)
{
	struct firmware_image *img;
	struct nordic_zip_format_data *priv = _priv;

	dfu_log("%s\n", __func__);
	img = new_firmware_image(priv, buf + t->start, t->end - t->start);
	if (!img)
		dfu_err("COULDN'T GET FIRMWARE IMAGE\n");
	priv->parser_curr_image = img;
	return img ? 0 : -1;
}


static int _get_file(struct nordic_zip_format_data *priv, const char *buf,
		     const jsmntok_t *t, struct received_file **out)
{
	struct received_file *rf = _search_rx_file(priv, buf + t->start,
						   t->end - t->start);

	if (!rf) {
		dfu_err("Cannot find file %.*s\n", t->end - t->start,
			buf + t->start);
		_free_firmware_image(priv->parser_curr_image);
		priv->parser_curr_image = NULL;
		return -1;
	}
	*out = rf;
	/*
	 * Reset done counter to get ready for sending file
	 */
	rf->done = 0;
	return 0;
}

static int get_binfile(const struct json_node *n, const jsmntok_t *t,
		       const char *buf, void *_priv)
{
	struct nordic_zip_format_data *priv = _priv;

	dfu_dbg("binfile = %.*s\n", t->end - t->start, buf + t->start);
	return _get_file(priv, buf, t, &priv->parser_curr_image->bin_file);
}

static int get_datfile(const struct json_node *n, const jsmntok_t *t,
		       const char *buf, void *_priv)
{
	struct nordic_zip_format_data *priv = _priv;

	dfu_dbg("datfile = %.*s\n", t->end - t->start, buf + t->start);
	return 	_get_file(priv, buf, t, &priv->parser_curr_image->dat_file);
}

static const struct json_node manifest_l3_softdevice_bootloader[] = {
	{
		.type = JSMN_UNDEFINED,
	},
};

static const struct json_node manifest_l3_simple[] = {
	{
		.type = JSMN_OBJECT,
	},
	{
		.type = JSMN_STRING,
		.expected = "bin_file",
	},
	{
		.type = JSMN_STRING,
		.cb = get_binfile,
	},
	{
		.type = JSMN_STRING,
		.expected = "dat_file",
	},
	{
		.type = JSMN_STRING,
		.cb = get_datfile,
	},
	{
		.type = JSMN_UNDEFINED,
	},
};

static const struct json_node application_nodes[] = {
	{
		.type = JSMN_STRING,
		.expected = "application",
		.next_tree = manifest_l3_simple,
		.cb = new_image,
	},
	{
		.type = JSMN_UNDEFINED,
		.flags = JSON_STOP_PARSING,
	},
};

static const struct json_node bootloader_nodes[] = {
	{
		.type = JSMN_STRING,
		.expected = "bootloader",
		.next_tree = manifest_l3_simple,
		.cb = new_image,
	},
	{
		.type = JSMN_UNDEFINED,
		.flags = JSON_STOP_PARSING,
	},
};

static const struct json_node softdevice_nodes[] = {
	{
		.type = JSMN_STRING,
		.expected = "softdevice",
		.next_tree = manifest_l3_simple,
		.cb = new_image,
	},
	{
		.type = JSMN_UNDEFINED,
		.flags = JSON_STOP_PARSING,
	},
};

static const struct json_node softdevice_bootloader_nodes[] = {
	{
		/* THIS IS THE LAST CHANCE, MANDATORY NODE */
		.type = JSMN_STRING,
		.expected = "softdevice_bootloader",
		.next_tree = manifest_l3_softdevice_bootloader,
		.cb = new_image,
		/* Back to last level */
		.type = JSMN_UNDEFINED,
	},
	{
		.type = JSMN_UNDEFINED,
	},
};

struct json_section {
	const char *start_token_name;
	const struct json_node *nodes;
};

static int _parse_manifest(struct dfu_binary_file *bf)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	/* Manifest is __ALWAYS__ file 0 */
	struct received_file *manifest = &priv->files[0];
	static jsmntok_t tokens[MAX_NTOKENS];
	const jsmntok_t *t;
	const struct json_node *nodes;
	jsmn_parser p;
	int stat, nimages;
	const char *buf;
	const struct json_section sections[] = {
		{
			.start_token_name = "application",
			.nodes = application_nodes,
		},
		{
			.start_token_name = "bootloader",
			.nodes = bootloader_nodes,
		},
		{
			.start_token_name = "softdevice",
			.nodes = softdevice_nodes,
		},
		{
			.start_token_name = "softdevice_bootloader",
			.nodes = softdevice_bootloader_nodes,
		},
		{
			.start_token_name = NULL,
			.nodes = NULL,
		},
	};
	const struct json_section *s;

	if (!manifest->name[0]) {
		/* Manifest not received */
		dfu_err("manifest is not there !\n");
		return -1;
	}
	if (!manifest->local_buf) {
		/* Manifest is too long to be parsed */
		dfu_err("manifest is too big !\n");
		return -1;
	}
	buf = manifest->local_buf;
	/* Initialize and run the parser with a fixed max number of tokens */
	jsmn_init(&p);
	stat = jsmn_parse(&p, buf, manifest->size + 1, tokens,
			  ARRAY_SIZE(tokens));
	if (stat < 0) {
		/* Not enough memory for tokens */
		dfu_err("%s: not enough memory for tokens\n", __func__);
		return -1;
	}
	/* Look for application data */
	for (s = sections, nimages = 0; s->start_token_name; s++) {
		t = jsmn_search_token(buf, tokens, p.toknext,
				      s->start_token_name);
		if (t)
			nimages++;
		else
			continue;
		nodes = s->nodes;
		if (jsmn_scan_tokens(buf, t, t - tokens + 1, &nodes, priv) < 0)
			return -1;
	}
	dfu_dbg("%s: %d images found\n", __func__, nimages);
	return nimages > 0 ? 0 : -1;
}

static int _start_sending_images(struct dfu_binary_file *bf)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	
	if (!priv->rx_file_count) {
		/* Nothing to do, just end here */
		_all_done(bf);
		return 0;
	}
	/* Output address depends on file index */
	priv->send_image_index = 0;
	priv->state = SENDING_COMMAND;
	/* Don't actually send anything, wait for the next decode_chunk call */
	return 0;
}

static int _decode_end_central_header(struct dfu_binary_file *bf, int start)
{
	int index = start, i;
	char *ptr;
	union zip_end_of_central_dir_header zeocdh;

	if (bf_count(bf) < sizeof(zeocdh))
		/* Not enough chars */
		return 0;
	for (i = 0, ptr = zeocdh.c; i < sizeof(zeocdh);
	     i++, index = _next(bf, index))
		*ptr++ = ((char *)bf->buf)[index];
	if (bf_count(bf) < sizeof(zeocdh) + zeocdh.s.comment_len)
		return 0;
	/*
	 * End of central header received
	 * Parse manifest file (index 0) and setup firmware images to be sent
	 */
	if (_parse_manifest(bf) < 0)
		return -1;
	/* Now start sending files */
	if (_start_sending_images(bf) < 0)
		return -1;
	return sizeof(zeocdh) + zeocdh.s.comment_len;
}

static int _do_idle(struct dfu_binary_file *bf)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	int index = bf->tail, stat;
	enum zip_header_type t;

	/* Search next signature */
	stat = _search_signature(bf, &index, &t);
	if (stat <= 0)
		/* Signature not found */
		return stat;
	switch (t) {
	case LOCAL_HEADER:
		stat = _decode_local_header(bf, index);
		break;
	case CENTRAL_HEADER:
		stat = _decode_central_header(bf, index);
		break;
	case END_CENTRAL_HEADER:
		stat = _decode_end_central_header(bf, index);
		break;
	default:
		dfu_err("Unknown signature\n");
		_reset(priv);
		return -1;
	}
	if (stat <= 0)
		return stat;
	/* Update tail now */
	bf->tail = _go_on(bf, bf->tail, stat);
	/*
	 * We advertise 0 data to be sent: just get bytes from input
	 * stream and save them for now
	 */
	return 0;
}

static int _do_store(struct dfu_binary_file *bf)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	unsigned int sz;
	struct received_file *rf = priv->curr_rf;
	uint8_t *ptr = bf->buf;
	int stat;

	/* Get data from file buffer and send it to the current file */
	sz = min(bf_count_to_end(bf), rf->size - rf->done);
	if (rf->local_buf) {
		sz = min(sz, rf->local_buf_size - rf->local_buf_offset - 1);
		memcpy(&rf->local_buf[rf->local_buf_offset], &ptr[bf->tail],
		       sz);
		rf->local_buf_offset += sz;
		rf->local_buf[rf->local_buf_offset] = 0;
		stat = sz;
	} else
		stat = dfu_file_write(bf->dfu, rf->fd, &ptr[bf->tail], sz);
	if (stat < 0) {
		dfu_err("%s: error writing to temp file\n", __func__);
		_reset(priv);
		return stat;
	}
	rf->done += sz;
	if (rf->done >= rf->size) {
		priv->rx_file_count++;
		/* File completely received, go back to idle */
		priv->state = IDLE;
	}
	/* Update tail now */
	bf->tail = _go_on(bf, bf->tail, sz);
	/*
	 * We advertise 0 data to be sent: just get bytes from input
	 * stream and save them for now
	 */
	return 0;
}

static int _do_ignore(struct dfu_binary_file *bf)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	unsigned int sz = bf_count(bf);
	
	/* Just update received counter and throw everything away */
	priv->ignored += sz;
	if (priv->ignored >= priv->ignored_size)
		/* File completely received, go back to idle */
		priv->state = IDLE;
	/* Update tail now and throw data away */
	bf->tail = _go_on(bf, bf->tail, sz);
	/* Nothing to be written, just return 0 */
	return 0;
}

/*
 * Decode new file chunk
 */
int nz_decode_chunk(struct dfu_binary_file *bf, uint32_t *addr)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	enum nzstate old_state;
	int ret = 0, do_break = 0;

	do {
		old_state = priv->state;
		switch(priv->state) {
		case IDLE:
			ret = _do_idle(bf);
			break;
		case STORING_FILE:
			ret = _do_store(bf);
			break;
		case IGNORING_FILE:
			ret = _do_ignore(bf);
			break;
		case SENDING_COMMAND:
		case SENDING_DATA:
			ret = _realign_decoded_head(bf, addr);
			if (ret > 0) {
				do_break = 1;
				break;
			}
			ret = _send_file_chunk(bf, addr);
			if (ret > 0)
				do_break = 1;
			break;
		case ALL_DONE:
			ret = 0;
			break;
		default:
			dfu_err("%s: unknown state %d\n", __func__,
				priv->state);
			ret = -1;
			break;
		}
	} while (priv->state != old_state && ret >= 0 && !do_break);
	return ret;
}

int nz_fini(struct dfu_binary_file *bf)
{
	struct nordic_zip_format_data *priv = bf->format_data;

	/* Reset everything */
	_reset(priv);
	return 0;
}

declare_dfu_format(nz, nz_probe, nz_decode_chunk, nz_fini);


int nzbf_get_file_type_and_size(struct dfu_binary_file *bf,
				phys_addr_t addr,
				enum nzbf_type *t, unsigned int *size)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	struct firmware_image *fi;
	struct received_file *rf;
	int image_index;

	image_index = addr >> NZ_FWFILE_IMAGE_SHIFT;
	fi = &priv->images[image_index];
	*t = addr & BIT(NZ_FWFILE_DATA_IMAGE_SHIFT) ? NZ_TYPE_DATA :
		NZ_TYPE_COMMAND;
	rf = (*t == NZ_TYPE_COMMAND) ? fi->dat_file : fi->bin_file;
	*size = rf->size;
	dfu_dbg("addr = 0x%08x, size = %u\n", (unsigned)addr, *size);
	return 0;
}

int nzbf_calc_crc(struct dfu_binary_file *bf, phys_addr_t addr,
		  unsigned int length, uint32_t *out)
{
	struct nordic_zip_format_data *priv = bf->format_data;
	struct firmware_image *fi;
	struct received_file *rf;
	int image_index;
	unsigned char buf[32];
	int i, sz, fd, ret = 0;
	enum nzbf_type t = addr & BIT(NZ_FWFILE_DATA_IMAGE_SHIFT) ?
		NZ_TYPE_DATA :
		NZ_TYPE_COMMAND;

	image_index = addr >> NZ_FWFILE_IMAGE_SHIFT;
	fi = &priv->images[image_index];
	rf = (t == NZ_TYPE_COMMAND) ? fi->dat_file : fi->bin_file;
	crc32_init(out);
	fd = dfu_file_open(bf->dfu, rf->name, 0, 0);
	if (fd < 0) {
		dfu_err("%s: could not open file %s\n", __func__, rf->name);
		return fd;
	}
	for (i = 0; i < length; ) {
		sz = min(sizeof(buf), length - i);
		if (!sz)
			break;
		if (dfu_file_read(bf->dfu, fd, buf, sz) < 0) {
			dfu_err("%s: error reading file\n", __func__);
			ret = -1;
			break;
		}
		crc32_iteration(buf, sz, out);
	}
	crc32_done(out);
	dfu_file_close(bf->dfu, fd);
	return ret;
}
