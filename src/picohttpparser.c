/*
 * Copyright (c) 2009-2014 Kazuho Oku, Tokuhiro Matsuno, Daisuke Murase,
 *                         Shigeo Mitsunari
 *
 * The software is licensed under either the MIT License (below) or the Perl
 * license.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>
#include <stddef.h>
#include <string.h>
#ifdef __SSE4_2__
#ifdef _MSC_VER
#include <nmmintrin.h>
#else
#include <x86intrin.h>
#endif
#endif
#include "picohttpparser.h"

/* $Id: 8e21379070e9a13462ee692b40548e5fd59a547c $ */

#if __GNUC__ >= 3
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#else
#define likely(x) (x)
#define unlikely(x) (x)
#endif

#ifdef _MSC_VER
#define ALIGNED(n) _declspec(align(n))
#else
#define ALIGNED(n) __attribute__((aligned(n)))
#endif

static inline const void *check_eof(const void *buf, const void *end, int *ret)
{
	if (likely((buf != end)))
		return buf;
	*ret = -2;
	return NULL;
}

static inline const void *expect_char(char ch, const void *buf, const void *end,
				      int *ret)
{
	const char *ptr = buf;

	if (!check_eof(buf, end, ret))
		return NULL;
	if (*ptr++ != ch) {
		*ret = -1;
		return NULL;
	}
	return ptr;
}

#define IS_PRINTABLE_ASCII(c) ((unsigned char)(c)-040u < 0137u)

static const char *token_char_map =
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\1\1\1\1\1\1\1\0\0\1\1\0\1\1\0\1\1\1\1\1\1\1\1\1\1\0\0\0\0\0\0"
	"\0\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\0\0\1\1"
	"\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\0\1\0\1\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
	"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0";

static const char *findchar_fast(const char *buf, const char *buf_end, const char *ranges, size_t ranges_size, int *found)
{
    *found = 0;
#ifdef __SSE4_2__
    if (likely(buf_end - buf >= 16)) {
        __m128i ranges16 = _mm_loadu_si128((const __m128i *)ranges);

        size_t left = (buf_end - buf) & ~15;
        do {
            __m128i b16 = _mm_loadu_si128((void *)buf);
            int r = _mm_cmpestri(ranges16, ranges_size, b16, 16, _SIDD_LEAST_SIGNIFICANT | _SIDD_CMP_RANGES | _SIDD_UBYTE_OPS);
            if (unlikely(r != 16)) {
                buf += r;
                *found = 1;
                break;
            }
            buf += 16;
            left -= 16;
        } while (likely(left != 0));
    }
#else
    /* suppress unused parameter warning */
    (void)buf_end;
    (void)ranges;
    (void)ranges_size;
#endif
    return buf;
}

static inline const void *advance_token(const char **tok, size_t *toklen,
					const char *buf, const void *end,
					int *ret)
{
	const char *tok_start = buf;
	static const char ALIGNED(16) ranges2[] = "\000\040\177\177";
	int found;

	buf = findchar_fast(buf, end, ranges2, sizeof(ranges2) - 1, &found);
	if (!found) {
		if (!check_eof(buf, end, ret))
			return NULL;
	}
	while (1) {
		if (*buf == ' ') {
			break;
		} else if (unlikely(!IS_PRINTABLE_ASCII(*buf))) {
			if ((unsigned char)*buf < '\040' || *buf == '\177') {
				*ret = -1;
				return NULL;
			}
		}
		++buf;
		if (!check_eof(buf, end, ret))
			return NULL;
	}
	*tok = tok_start;
	*toklen = buf - tok_start;
	return buf;
}


static const char *get_token_to_eol(const char *buf, const char *buf_end,
				    const char **token, size_t *token_len,
				    int *ret)
{
	const char *token_start = buf;

#ifdef __SSE4_2__
	static const char ranges1[] = "\0\010"
		/* allow HT */
		"\012\037"
		/* allow SP and up to but not including DEL */
		"\177\177"
		/* allow chars w. MSB set */
		;
	int found;
	buf = findchar_fast(buf, buf_end, ranges1, sizeof(ranges1) - 1, &found);
	if (found)
		goto FOUND_CTL;
#else
	/*
	 * find non-printable char within the next 8 bytes, this is the
	 * hottest code; manually inlined
	 */
	while (likely(buf_end - buf >= 8)) {
#define DOIT()								\
		do {							\
			if (unlikely(!IS_PRINTABLE_ASCII(*buf)))	\
				goto NonPrintable;			\
			++buf;						\
		} while (0)
		DOIT();
		DOIT();
		DOIT();
		DOIT();
		DOIT();
		DOIT();
		DOIT();
		DOIT();
#undef DOIT
		continue;
	NonPrintable:
		if ((likely((unsigned char)*buf < '\040') &&
		     likely(*buf != '\011')) || unlikely(*buf == '\177')) {
			goto FOUND_CTL;
		}
		++buf;
	}
#endif
	for (;; ++buf) {
		if (!check_eof(buf, buf_end, ret))
			return NULL;
		if (unlikely(!IS_PRINTABLE_ASCII(*buf))) {
			if ((likely((unsigned char)*buf < '\040') &&
			     likely(*buf != '\011')) ||
			    unlikely(*buf == '\177')) {
				goto FOUND_CTL;
			}
		}
	}
FOUND_CTL:
	if (likely(*buf == '\015')) {
		++buf;
		if (!expect_char('\012', buf, buf_end, ret))
			return NULL;
		*token_len = buf - 2 - token_start;
	} else if (*buf == '\012') {
		*token_len = buf - token_start;
		++buf;
	} else {
		*ret = -1;
		return NULL;
	}
	*token = token_start;
	return buf;
}

static const char *is_complete(const char *buf, const char *buf_end,
			       size_t last_len, int *ret)
{
	int ret_cnt = 0;
	buf = last_len < 3 ? buf : buf + last_len - 3;

	while (1) {
		if (!check_eof(buf, buf_end, ret))
			return NULL;
		if (*buf == '\015') {
			++buf;
			if (!check_eof(buf, buf_end, ret))
				return NULL;
			if (!expect_char('\012', buf, buf_end, ret))
				return NULL;
			++ret_cnt;
		} else if (*buf == '\012') {
			++buf;
			++ret_cnt;
		} else {
			++buf;
			ret_cnt = 0;
		}
		if (ret_cnt == 2) {
			return buf;
		}
	}
	*ret = -2;
	return NULL;
}

/* *_buf is always within [buf, buf_end) upon success */
static const char *parse_int(const char *buf, const char *buf_end, int *value, int *ret)
{
    int v;
    if (!check_eof(buf, buf_end, ret))
	    return NULL;
    if (!('0' <= *buf && *buf <= '9')) {
        *ret = -1;
        return NULL;
    }
    v = 0;
    for (;; ++buf) {
	    if (!check_eof(buf, buf_end, ret))
		    return NULL;
        if ('0' <= *buf && *buf <= '9') {
            v = v * 10 + *buf - '0';
        } else {
            break;
        }
    }

    *value = v;
    return buf;
}

/* returned pointer is always within [buf, buf_end), or null */
static const char *parse_http_version(const char *buf, const char *buf_end, int *minor_version, int *ret)
{
	char str[] = { 'H', 'T', 'T', 'P', '/', '1', '.', };
	int i;

	for (i = 0; i < sizeof(str); i++)
		if (!expect_char(str[i], buf, buf_end, ret))
			return NULL;
    return parse_int(buf, buf_end, minor_version, ret);
}

static const char *parse_headers(const char *buf, const char *buf_end, struct phr_header *headers, size_t *num_headers,
                                 size_t max_headers, int *ret)
{
    for (;; ++*num_headers) {
 if (!check_eof(buf, buf_end, ret))
	 return NULL;
        if (*buf == '\015') {
            ++buf;
            if (!expect_char('\012', buf, buf_end, ret))
		    return NULL;
            break;
        } else if (*buf == '\012') {
            ++buf;
            break;
        }
        if (*num_headers == max_headers) {
            *ret = -1;
            return NULL;
        }
        if (!(*num_headers != 0 && (*buf == ' ' || *buf == '\t'))) {
            static const char ALIGNED(16) ranges1[] = "::\x00\037";
            int found;
            if (!token_char_map[(unsigned char)*buf]) {
                *ret = -1;
                return NULL;
            }
            /* parsing name, but do not discard SP before colon, see
             * http://www.mozilla.org/security/announce/2006/mfsa2006-33.html */
            headers[*num_headers].name = buf;
            buf = findchar_fast(buf, buf_end, ranges1, sizeof(ranges1) - 1, &found);
            if (!found) {
		    if (!check_eof(buf, buf_end, ret))
			    return NULL;
            }
            while (1) {
                if (*buf == ':') {
                    break;
                } else if (*buf < ' ') {
                    *ret = -1;
                    return NULL;
                }
                ++buf;
                if (!check_eof(buf, buf_end, ret))
			return NULL;
            }
            headers[*num_headers].name_len = buf - headers[*num_headers].name;
            ++buf;
            for (;; ++buf) {
		    if (!check_eof(buf, buf_end, ret))
			    return NULL;
                if (!(*buf == ' ' || *buf == '\t')) {
                    break;
                }
            }
        } else {
            headers[*num_headers].name = NULL;
            headers[*num_headers].name_len = 0;
        }
        if ((buf = get_token_to_eol(buf, buf_end, &headers[*num_headers].value, &headers[*num_headers].value_len, ret)) == NULL) {
            return NULL;
        }
    }
    return buf;
}

static const char *parse_request(const char *buf, const char *buf_end, const char **method, size_t *method_len, const char **path,
                                 size_t *path_len, int *minor_version, struct phr_header *headers, size_t *num_headers,
                                 size_t max_headers, int *ret)
{
    /* skip first empty line (some clients add CRLF after POST content) */
    if (!check_eof(buf, buf_end, ret))
	    return NULL;
    if (*buf == '\015') {
        ++buf;
	if (!expect_char('\012', buf, buf_end, ret))
		return NULL;
    } else if (*buf == '\012') {
        ++buf;
    }

    /* parse request line */
    buf = advance_token(method, method_len, buf, buf_end, ret);
    if (!buf)
	    return buf;
    ++buf;
    buf = advance_token(path, path_len, buf, buf_end, ret);
    if (!buf)
	    return buf;
    ++buf;
    if ((buf = parse_http_version(buf, buf_end, minor_version, ret)) == NULL) {
        return NULL;
    }
    if (*buf == '\015') {
        ++buf;
	if (!expect_char('\012', buf, buf_end, ret))
		return NULL;
    } else if (*buf == '\012') {
        ++buf;
    } else {
        *ret = -1;
        return NULL;
    }

    return parse_headers(buf, buf_end, headers, num_headers, max_headers, ret);
}

int phr_parse_request(const char *buf_start, size_t len, const char **method, size_t *method_len, const char **path,
                      size_t *path_len, int *minor_version, struct phr_header *headers, size_t *num_headers, size_t last_len)
{
    const char *buf = buf_start, *buf_end = buf_start + len;
    size_t max_headers = *num_headers;
    int r;

    *method = NULL;
    *method_len = 0;
    *path = NULL;
    *path_len = 0;
    *minor_version = -1;
    *num_headers = 0;

    /* if last_len != 0, check if the request is complete (a fast countermeasure
       againt slowloris */
    if (last_len != 0 && is_complete(buf, buf_end, last_len, &r) == NULL) {
        return r;
    }

    if ((buf = parse_request(buf, buf_end, method, method_len, path, path_len, minor_version, headers, num_headers, max_headers,
                             &r)) == NULL) {
        return r;
    }

    return (int)(buf - buf_start);
}

static const char *parse_response(const char *buf, const char *buf_end, int *minor_version, int *status, const char **msg,
                                  size_t *msg_len, struct phr_header *headers, size_t *num_headers, size_t max_headers, int *ret)
{
    /* parse "HTTP/1.x" */
    if ((buf = parse_http_version(buf, buf_end, minor_version, ret)) == NULL) {
        return NULL;
    }
    /* skip space */
    if (*buf++ != ' ') {
        *ret = -1;
        return NULL;
    }
    /* parse status code */
    if ((buf = parse_int(buf, buf_end, status, ret)) == NULL) {
        return NULL;
    }
    /* skip space */
    if (*buf++ != ' ') {
        *ret = -1;
        return NULL;
    }
    /* get message */
    if ((buf = get_token_to_eol(buf, buf_end, msg, msg_len, ret)) == NULL) {
        return NULL;
    }

    return parse_headers(buf, buf_end, headers, num_headers, max_headers, ret);
}

int phr_parse_response(const char *buf_start, size_t len, int *minor_version, int *status, const char **msg, size_t *msg_len,
                       struct phr_header *headers, size_t *num_headers, size_t last_len)
{
    const char *buf = buf_start, *buf_end = buf + len;
    size_t max_headers = *num_headers;
    int r;

    *minor_version = -1;
    *status = 0;
    *msg = NULL;
    *msg_len = 0;
    *num_headers = 0;

    /* if last_len != 0, check if the response is complete (a fast countermeasure
       against slowloris */
    if (last_len != 0 && is_complete(buf, buf_end, last_len, &r) == NULL) {
        return r;
    }

    if ((buf = parse_response(buf, buf_end, minor_version, status, msg, msg_len, headers, num_headers, max_headers, &r)) == NULL) {
        return r;
    }

    return (int)(buf - buf_start);
}

int phr_parse_headers(const char *buf_start, size_t len, struct phr_header *headers, size_t *num_headers, size_t last_len)
{
    const char *buf = buf_start, *buf_end = buf + len;
    size_t max_headers = *num_headers;
    int r;

    *num_headers = 0;

    /* if last_len != 0, check if the response is complete (a fast countermeasure
       against slowloris */
    if (last_len != 0 && is_complete(buf, buf_end, last_len, &r) == NULL) {
        return r;
    }

    if ((buf = parse_headers(buf, buf_end, headers, num_headers, max_headers, &r)) == NULL) {
        return r;
    }

    return (int)(buf - buf_start);
}

enum {
    CHUNKED_IN_CHUNK_SIZE,
    CHUNKED_IN_CHUNK_EXT,
    CHUNKED_IN_CHUNK_DATA,
    CHUNKED_IN_CHUNK_CRLF,
    CHUNKED_IN_TRAILERS_LINE_HEAD,
    CHUNKED_IN_TRAILERS_LINE_MIDDLE
};

static int decode_hex(int ch)
{
    if ('0' <= ch && ch <= '9') {
        return ch - '0';
    } else if ('A' <= ch && ch <= 'F') {
        return ch - 'A' + 0xa;
    } else if ('a' <= ch && ch <= 'f') {
        return ch - 'a' + 0xa;
    } else {
        return -1;
    }
}

ssize_t phr_decode_chunked(struct phr_chunked_decoder *decoder, char *buf, size_t *_bufsz)
{
    size_t dst = 0, src = 0, bufsz = *_bufsz;
    ssize_t ret = -2; /* incomplete */

    while (1) {
        switch (decoder->_state) {
        case CHUNKED_IN_CHUNK_SIZE:
            for (;; ++src) {
                int v;
                if (src == bufsz)
                    goto Exit;
                if ((v = decode_hex(buf[src])) == -1) {
                    if (decoder->_hex_count == 0) {
                        ret = -1;
                        goto Exit;
                    }
                    break;
                }
                if (decoder->_hex_count == sizeof(size_t) * 2) {
                    ret = -1;
                    goto Exit;
                }
                decoder->bytes_left_in_chunk = decoder->bytes_left_in_chunk * 16 + v;
                ++decoder->_hex_count;
            }
            decoder->_hex_count = 0;
            decoder->_state = CHUNKED_IN_CHUNK_EXT;
        /* fallthru */
        case CHUNKED_IN_CHUNK_EXT:
            /* RFC 7230 A.2 "Line folding in chunk extensions is disallowed" */
            for (;; ++src) {
                if (src == bufsz)
                    goto Exit;
                if (buf[src] == '\012')
                    break;
            }
            ++src;
            if (decoder->bytes_left_in_chunk == 0) {
                if (decoder->consume_trailer) {
                    decoder->_state = CHUNKED_IN_TRAILERS_LINE_HEAD;
                    break;
                } else {
                    goto Complete;
                }
            }
            decoder->_state = CHUNKED_IN_CHUNK_DATA;
        /* fallthru */
        case CHUNKED_IN_CHUNK_DATA: {
            size_t avail = bufsz - src;
            if (avail < decoder->bytes_left_in_chunk) {
                if (dst != src)
                    memmove(buf + dst, buf + src, avail);
                src += avail;
                dst += avail;
                decoder->bytes_left_in_chunk -= avail;
                goto Exit;
            }
            if (dst != src)
                memmove(buf + dst, buf + src, decoder->bytes_left_in_chunk);
            src += decoder->bytes_left_in_chunk;
            dst += decoder->bytes_left_in_chunk;
            decoder->bytes_left_in_chunk = 0;
            decoder->_state = CHUNKED_IN_CHUNK_CRLF;
        }
        /* fallthru */
        case CHUNKED_IN_CHUNK_CRLF:
            for (;; ++src) {
                if (src == bufsz)
                    goto Exit;
                if (buf[src] != '\015')
                    break;
            }
            if (buf[src] != '\012') {
                ret = -1;
                goto Exit;
            }
            ++src;
            decoder->_state = CHUNKED_IN_CHUNK_SIZE;
            break;
        case CHUNKED_IN_TRAILERS_LINE_HEAD:
            for (;; ++src) {
                if (src == bufsz)
                    goto Exit;
                if (buf[src] != '\015')
                    break;
            }
            if (buf[src++] == '\012')
                goto Complete;
            decoder->_state = CHUNKED_IN_TRAILERS_LINE_MIDDLE;
        /* fallthru */
        case CHUNKED_IN_TRAILERS_LINE_MIDDLE:
            for (;; ++src) {
                if (src == bufsz)
                    goto Exit;
                if (buf[src] == '\012')
                    break;
            }
            ++src;
            decoder->_state = CHUNKED_IN_TRAILERS_LINE_HEAD;
            break;
        default:
            assert(!"decoder is corrupt");
        }
    }

Complete:
    ret = bufsz - src;
Exit:
    if (dst != src)
        memmove(buf + dst, buf + src, bufsz - src);
    *_bufsz = dst;
    return ret;
}
