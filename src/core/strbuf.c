#include "core/strbuf.h"
#include "core/utf8.h"
#include "core/xs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

typedef struct XSBuf {
    char *data;
    int   len;
    int   cap;
} XSBuf;

XSBuf *strbuf_new(void) {
    XSBuf *b = xs_calloc(1, sizeof(XSBuf));
    b->cap = 64;
    b->data = xs_malloc((size_t)b->cap);
    b->data[0] = '\0';
    b->len = 0;
    return b;
}

XSBuf *strbuf_with_cap(int capacity) {
    if (capacity < 16) capacity = 16;
    XSBuf *b = xs_calloc(1, sizeof(XSBuf));
    b->cap = capacity;
    b->data = xs_malloc((size_t)b->cap);
    b->data[0] = '\0';
    b->len = 0;
    return b;
}

void strbuf_free(XSBuf *b) {
    if (!b) return;
    free(b->data);
    free(b);
}

static void strbuf_grow(XSBuf *b, int need) {
    int required = b->len + need + 1;
    if (required <= b->cap) return;
    int newcap = b->cap;
    while (newcap < required) newcap *= 2;
    b->data = xs_realloc(b->data, (size_t)newcap);
    b->cap = newcap;
}

void strbuf_append(XSBuf *b, const XSBuf *other) {
    if (!other || other->len == 0) return;
    strbuf_grow(b, other->len);
    memcpy(b->data + b->len, other->data, (size_t)other->len);
    b->len += other->len;
    b->data[b->len] = '\0';
}

void strbuf_append_str(XSBuf *b, const char *s) {
    if (!s) return;
    int n = (int)strlen(s);
    strbuf_grow(b, n);
    memcpy(b->data + b->len, s, (size_t)n);
    b->len += n;
    b->data[b->len] = '\0';
}

void strbuf_append_char(XSBuf *b, char c) {
    strbuf_grow(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

void strbuf_append_int(XSBuf *b, int64_t val) {
    char tmp[32];
    int n = snprintf(tmp, sizeof tmp, "%lld", (long long)val);
    strbuf_grow(b, n);
    memcpy(b->data + b->len, tmp, (size_t)n);
    b->len += n;
    b->data[b->len] = '\0';
}

void strbuf_append_float(XSBuf *b, double val) {
    char tmp[64];
    int n = snprintf(tmp, sizeof tmp, "%g", val);
    strbuf_grow(b, n);
    memcpy(b->data + b->len, tmp, (size_t)n);
    b->len += n;
    b->data[b->len] = '\0';
}

void strbuf_printf(XSBuf *b, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need <= 0) { va_end(ap2); return; }
    strbuf_grow(b, need);
    vsnprintf(b->data + b->len, (size_t)(need + 1), fmt, ap2);
    va_end(ap2);
    b->len += need;
}

void strbuf_insert(XSBuf *b, int pos, const char *s) {
    if (!s) return;
    if (pos < 0) pos = 0;
    if (pos > b->len) pos = b->len;
    int n = (int)strlen(s);
    strbuf_grow(b, n);
    memmove(b->data + pos + n, b->data + pos, (size_t)(b->len - pos));
    memcpy(b->data + pos, s, (size_t)n);
    b->len += n;
    b->data[b->len] = '\0';
}

void strbuf_delete(XSBuf *b, int start, int count) {
    if (start < 0) start = 0;
    if (start >= b->len) return;
    if (start + count > b->len) count = b->len - start;
    memmove(b->data + start, b->data + start + count,
            (size_t)(b->len - start - count));
    b->len -= count;
    b->data[b->len] = '\0';
}

void strbuf_replace_range(XSBuf *b, int start, int count, const char *rep) {
    strbuf_delete(b, start, count);
    strbuf_insert(b, start, rep);
}

int strbuf_index_of(const XSBuf *b, const char *needle) {
    if (!needle || !b->data) return -1;
    int nlen = (int)strlen(needle);
    if (nlen == 0) return 0;
    if (nlen > b->len) return -1;
    for (int i = 0; i <= b->len - nlen; i++) {
        if (memcmp(b->data + i, needle, (size_t)nlen) == 0)
            return i;
    }
    return -1;
}

int strbuf_contains(const XSBuf *b, const char *needle) {
    return strbuf_index_of(b, needle) >= 0;
}

int strbuf_starts_with(const XSBuf *b, const char *prefix) {
    if (!prefix) return 1;
    int plen = (int)strlen(prefix);
    if (plen > b->len) return 0;
    return memcmp(b->data, prefix, (size_t)plen) == 0;
}

int strbuf_ends_with(const XSBuf *b, const char *suffix) {
    if (!suffix) return 1;
    int slen = (int)strlen(suffix);
    if (slen > b->len) return 0;
    return memcmp(b->data + b->len - slen, suffix, (size_t)slen) == 0;
}

static void strbuf_trim_left_impl(XSBuf *b);
static void strbuf_trim_right_impl(XSBuf *b);

void strbuf_trim(XSBuf *b) {
    strbuf_trim_left_impl(b);
    strbuf_trim_right_impl(b);
}

void strbuf_trim_left(XSBuf *b) {
    strbuf_trim_left_impl(b);
}

void strbuf_trim_right(XSBuf *b) {
    strbuf_trim_right_impl(b);
}

static void strbuf_trim_left_impl(XSBuf *b) {
    int i = 0;
    while (i < b->len && (unsigned char)b->data[i] <= ' ') i++;
    if (i > 0) {
        memmove(b->data, b->data + i, (size_t)(b->len - i));
        b->len -= i;
        b->data[b->len] = '\0';
    }
}

static void strbuf_trim_right_impl(XSBuf *b) {
    while (b->len > 0 && (unsigned char)b->data[b->len - 1] <= ' ')
        b->len--;
    b->data[b->len] = '\0';
}

void strbuf_to_upper(XSBuf *b) {
    if (utf8_is_ascii(b->data, b->len)) {
        for (int i = 0; i < b->len; i++)
            b->data[i] = (char)toupper((unsigned char)b->data[i]);
    } else {
        int out_len = 0;
        char *upper = utf8_str_upper(b->data, b->len, &out_len);
        if (upper) {
            strbuf_grow(b, out_len - b->len);
            memcpy(b->data, upper, (size_t)out_len);
            b->len = out_len;
            b->data[b->len] = '\0';
            free(upper);
        }
    }
}

void strbuf_to_lower(XSBuf *b) {
    if (utf8_is_ascii(b->data, b->len)) {
        for (int i = 0; i < b->len; i++)
            b->data[i] = (char)tolower((unsigned char)b->data[i]);
    } else {
        int out_len = 0;
        char *lower = utf8_str_lower(b->data, b->len, &out_len);
        if (lower) {
            strbuf_grow(b, out_len - b->len);
            memcpy(b->data, lower, (size_t)out_len);
            b->len = out_len;
            b->data[b->len] = '\0';
            free(lower);
        }
    }
}

char **strbuf_split(const XSBuf *b, const char *delim, int *count) {
    *count = 0;
    int dlen = (int)strlen(delim);
    if (dlen == 0 || b->len == 0) {
        char **result = xs_malloc(sizeof(char *));
        result[0] = xs_strndup(b->data, (size_t)b->len);
        *count = 1;
        return result;
    }
    int cap = 8;
    char **parts = xs_malloc((size_t)cap * sizeof(char *));
    int start = 0;
    for (int i = 0; i <= b->len - dlen; i++) {
        if (memcmp(b->data + i, delim, (size_t)dlen) == 0) {
            if (*count >= cap) {
                cap *= 2;
                parts = xs_realloc(parts, (size_t)cap * sizeof(char *));
            }
            parts[(*count)++] = xs_strndup(b->data + start, (size_t)(i - start));
            i += dlen - 1;
            start = i + 1;
        }
    }
    if (*count >= cap) {
        cap *= 2;
        parts = xs_realloc(parts, (size_t)cap * sizeof(char *));
    }
    parts[(*count)++] = xs_strndup(b->data + start, (size_t)(b->len - start));
    return parts;
}

XSBuf *strbuf_join(const char **parts, int count, const char *sep) {
    XSBuf *b = strbuf_new();
    int slen = sep ? (int)strlen(sep) : 0;
    for (int i = 0; i < count; i++) {
        if (i > 0 && slen > 0) strbuf_append_str(b, sep);
        strbuf_append_str(b, parts[i]);
    }
    return b;
}

void strbuf_repeat(XSBuf *b, const char *s, int times) {
    if (!s || times <= 0) return;
    int slen = (int)strlen(s);
    strbuf_grow(b, slen * times);
    for (int i = 0; i < times; i++) {
        memcpy(b->data + b->len, s, (size_t)slen);
        b->len += slen;
    }
    b->data[b->len] = '\0';
}

char *strbuf_finish(XSBuf *b) {
    char *result = b->data;
    b->data = NULL;
    b->len = b->cap = 0;
    free(b);
    return result;
}

void strbuf_clear(XSBuf *b) {
    b->len = 0;
    b->data[0] = '\0';
}

void strbuf_shrink(XSBuf *b) {
    if (b->len + 1 < b->cap) {
        b->cap = b->len + 1;
        b->data = xs_realloc(b->data, (size_t)b->cap);
    }
}

const char *strbuf_data(const XSBuf *b) {
    return b ? b->data : "";
}

int strbuf_len(const XSBuf *b) {
    return b ? b->len : 0;
}

int strbuf_cap(const XSBuf *b) {
    return b ? b->cap : 0;
}

int strbuf_last_index_of(const XSBuf *b, const char *needle) {
    if (!needle || !b->data) return -1;
    int nlen = (int)strlen(needle);
    if (nlen == 0) return b->len;
    if (nlen > b->len) return -1;
    for (int i = b->len - nlen; i >= 0; i--) {
        if (memcmp(b->data + i, needle, (size_t)nlen) == 0)
            return i;
    }
    return -1;
}

int strbuf_count(const XSBuf *b, const char *needle) {
    if (!needle || !b->data) return 0;
    int nlen = (int)strlen(needle);
    if (nlen == 0) return 0;
    int cnt = 0;
    for (int i = 0; i <= b->len - nlen; i++) {
        if (memcmp(b->data + i, needle, (size_t)nlen) == 0) {
            cnt++;
            i += nlen - 1;
        }
    }
    return cnt;
}

void strbuf_replace_all(XSBuf *b, const char *old_str, const char *new_str) {
    if (!old_str || !new_str) return;
    int olen = (int)strlen(old_str);
    if (olen == 0) return;

    XSBuf *result = strbuf_new();
    int i = 0;
    while (i <= b->len - olen) {
        if (memcmp(b->data + i, old_str, (size_t)olen) == 0) {
            strbuf_append_str(result, new_str);
            i += olen;
        } else {
            strbuf_append_char(result, b->data[i]);
            i++;
        }
    }
    while (i < b->len) {
        strbuf_append_char(result, b->data[i]);
        i++;
    }
    free(b->data);
    b->data = result->data;
    b->len = result->len;
    b->cap = result->cap;
    result->data = NULL;
    strbuf_free(result);
}

void strbuf_reverse(XSBuf *b) {
    if (utf8_is_ascii(b->data, b->len)) {
        for (int i = 0, j = b->len - 1; i < j; i++, j--) {
            char tmp = b->data[i];
            b->data[i] = b->data[j];
            b->data[j] = tmp;
        }
    } else {
        XSBuf *tmp = strbuf_new();
        const char *p = b->data;
        const char *end = b->data + b->len;
        int *offsets = NULL;
        int *lengths = NULL;
        int nchars = 0;
        int cap = 0;
        while (p < end) {
            const char *start_p = p;
            int cp;
            int bytes = utf8_decode(p, (int)(end - p), &cp);
            if (bytes <= 0) { p++; continue; }
            if (nchars >= cap) {
                cap = cap ? cap * 2 : 32;
                offsets = xs_realloc(offsets, (size_t)cap * sizeof(int));
                lengths = xs_realloc(lengths, (size_t)cap * sizeof(int));
            }
            offsets[nchars] = (int)(start_p - b->data);
            lengths[nchars] = bytes;
            nchars++;
            p += bytes;
        }
        for (int i = nchars - 1; i >= 0; i--) {
            strbuf_grow(tmp, lengths[i]);
            memcpy(tmp->data + tmp->len, b->data + offsets[i], (size_t)lengths[i]);
            tmp->len += lengths[i];
        }
        tmp->data[tmp->len] = '\0';
        free(offsets);
        free(lengths);
        free(b->data);
        b->data = tmp->data;
        b->len = tmp->len;
        b->cap = tmp->cap;
        tmp->data = NULL;
        strbuf_free(tmp);
    }
}

void strbuf_pad_left(XSBuf *b, int width, char pad) {
    if (b->len >= width) return;
    int need = width - b->len;
    strbuf_grow(b, need);
    memmove(b->data + need, b->data, (size_t)b->len);
    memset(b->data, pad, (size_t)need);
    b->len += need;
    b->data[b->len] = '\0';
}

void strbuf_pad_right(XSBuf *b, int width, char pad) {
    if (b->len >= width) return;
    int need = width - b->len;
    strbuf_grow(b, need);
    memset(b->data + b->len, pad, (size_t)need);
    b->len += need;
    b->data[b->len] = '\0';
}

XSBuf *strbuf_substr(const XSBuf *b, int start, int len) {
    if (start < 0) start = 0;
    if (start > b->len) start = b->len;
    if (start + len > b->len) len = b->len - start;
    if (len < 0) len = 0;
    XSBuf *result = strbuf_with_cap(len + 1);
    memcpy(result->data, b->data + start, (size_t)len);
    result->len = len;
    result->data[result->len] = '\0';
    return result;
}

int strbuf_cmp(const XSBuf *a, const XSBuf *b) {
    int minlen = a->len < b->len ? a->len : b->len;
    int r = memcmp(a->data, b->data, (size_t)minlen);
    if (r != 0) return r;
    if (a->len < b->len) return -1;
    if (a->len > b->len) return 1;
    return 0;
}

int strbuf_eq(const XSBuf *a, const XSBuf *b) {
    if (a->len != b->len) return 0;
    return memcmp(a->data, b->data, (size_t)a->len) == 0;
}

int strbuf_eq_str(const XSBuf *b, const char *s) {
    if (!s) return b->len == 0;
    int slen = (int)strlen(s);
    if (b->len != slen) return 0;
    return memcmp(b->data, s, (size_t)slen) == 0;
}

XSBuf *strbuf_clone(const XSBuf *b) {
    XSBuf *c = strbuf_with_cap(b->cap);
    memcpy(c->data, b->data, (size_t)b->len);
    c->len = b->len;
    c->data[c->len] = '\0';
    return c;
}

typedef struct ByteBuf {
    uint8_t *data;
    int      len;
    int      cap;
    int      pos;
} ByteBuf;

ByteBuf *bytebuf_new(void) {
    ByteBuf *b = xs_calloc(1, sizeof(ByteBuf));
    b->cap = 256;
    b->data = xs_malloc((size_t)b->cap);
    b->len = 0;
    b->pos = 0;
    return b;
}

ByteBuf *bytebuf_with_cap(int capacity) {
    if (capacity < 16) capacity = 16;
    ByteBuf *b = xs_calloc(1, sizeof(ByteBuf));
    b->cap = capacity;
    b->data = xs_malloc((size_t)b->cap);
    b->len = 0;
    b->pos = 0;
    return b;
}

void bytebuf_free(ByteBuf *b) {
    if (!b) return;
    free(b->data);
    free(b);
}

static void bytebuf_grow(ByteBuf *b, int need) {
    int required = b->len + need;
    if (required <= b->cap) return;
    int newcap = b->cap;
    while (newcap < required) newcap *= 2;
    b->data = xs_realloc(b->data, (size_t)newcap);
    b->cap = newcap;
}

void bytebuf_write_u8(ByteBuf *b, uint8_t v) {
    bytebuf_grow(b, 1);
    b->data[b->len++] = v;
}

void bytebuf_write_u16(ByteBuf *b, uint16_t v) {
    bytebuf_grow(b, 2);
    b->data[b->len++] = (uint8_t)(v >> 8);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
}

void bytebuf_write_u32(ByteBuf *b, uint32_t v) {
    bytebuf_grow(b, 4);
    b->data[b->len++] = (uint8_t)(v >> 24);
    b->data[b->len++] = (uint8_t)((v >> 16) & 0xFF);
    b->data[b->len++] = (uint8_t)((v >> 8) & 0xFF);
    b->data[b->len++] = (uint8_t)(v & 0xFF);
}

void bytebuf_write_u64(ByteBuf *b, uint64_t v) {
    bytebuf_grow(b, 8);
    for (int i = 7; i >= 0; i--)
        b->data[b->len++] = (uint8_t)((v >> (i * 8)) & 0xFF);
}

void bytebuf_write_i32(ByteBuf *b, int32_t v) {
    bytebuf_write_u32(b, (uint32_t)v);
}

void bytebuf_write_i64(ByteBuf *b, int64_t v) {
    bytebuf_write_u64(b, (uint64_t)v);
}

void bytebuf_write_f32(ByteBuf *b, float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    bytebuf_write_u32(b, bits);
}

void bytebuf_write_f64(ByteBuf *b, double v) {
    uint64_t bits;
    memcpy(&bits, &v, 8);
    bytebuf_write_u64(b, bits);
}

void bytebuf_write_bytes(ByteBuf *b, const uint8_t *data, int len) {
    bytebuf_grow(b, len);
    memcpy(b->data + b->len, data, (size_t)len);
    b->len += len;
}

void bytebuf_write_str(ByteBuf *b, const char *s) {
    if (!s) { bytebuf_write_u16(b, 0); return; }
    int len = (int)strlen(s);
    bytebuf_write_u16(b, (uint16_t)len);
    bytebuf_write_bytes(b, (const uint8_t *)s, len);
}

uint8_t bytebuf_read_u8(ByteBuf *b) {
    if (b->pos >= b->len) return 0;
    return b->data[b->pos++];
}

uint16_t bytebuf_read_u16(ByteBuf *b) {
    if (b->pos + 2 > b->len) return 0;
    uint16_t v = (uint16_t)((uint16_t)b->data[b->pos] << 8 | b->data[b->pos + 1]);
    b->pos += 2;
    return v;
}

uint32_t bytebuf_read_u32(ByteBuf *b) {
    if (b->pos + 4 > b->len) return 0;
    uint32_t v = ((uint32_t)b->data[b->pos] << 24) |
                 ((uint32_t)b->data[b->pos + 1] << 16) |
                 ((uint32_t)b->data[b->pos + 2] << 8) |
                 ((uint32_t)b->data[b->pos + 3]);
    b->pos += 4;
    return v;
}

uint64_t bytebuf_read_u64(ByteBuf *b) {
    if (b->pos + 8 > b->len) return 0;
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--)
        v |= (uint64_t)b->data[b->pos++] << (i * 8);
    return v;
}

int32_t bytebuf_read_i32(ByteBuf *b) {
    return (int32_t)bytebuf_read_u32(b);
}

int64_t bytebuf_read_i64(ByteBuf *b) {
    return (int64_t)bytebuf_read_u64(b);
}

float bytebuf_read_f32(ByteBuf *b) {
    uint32_t bits = bytebuf_read_u32(b);
    float v;
    memcpy(&v, &bits, 4);
    return v;
}

double bytebuf_read_f64(ByteBuf *b) {
    uint64_t bits = bytebuf_read_u64(b);
    double v;
    memcpy(&v, &bits, 8);
    return v;
}

int bytebuf_read_bytes(ByteBuf *b, uint8_t *out, int len) {
    int avail = b->len - b->pos;
    if (len > avail) len = avail;
    if (len > 0) memcpy(out, b->data + b->pos, (size_t)len);
    b->pos += len;
    return len;
}

char *bytebuf_read_str(ByteBuf *b) {
    uint16_t len = bytebuf_read_u16(b);
    if (b->pos + len > b->len) return xs_strdup("");
    char *s = xs_strndup((char *)(b->data + b->pos), len);
    b->pos += len;
    return s;
}

void bytebuf_seek(ByteBuf *b, int pos) {
    if (pos < 0) pos = 0;
    if (pos > b->len) pos = b->len;
    b->pos = pos;
}

int bytebuf_tell(ByteBuf *b) {
    return b->pos;
}

int bytebuf_remaining(ByteBuf *b) {
    return b->len - b->pos;
}

const uint8_t *bytebuf_data(const ByteBuf *b) {
    return b ? b->data : NULL;
}

int bytebuf_len(const ByteBuf *b) {
    return b ? b->len : 0;
}

void bytebuf_clear(ByteBuf *b) {
    b->len = 0;
    b->pos = 0;
}

void bytebuf_write_varint(ByteBuf *b, uint64_t v) {
    while (v >= 0x80) {
        bytebuf_write_u8(b, (uint8_t)(v | 0x80));
        v >>= 7;
    }
    bytebuf_write_u8(b, (uint8_t)v);
}

uint64_t bytebuf_read_varint(ByteBuf *b) {
    uint64_t result = 0;
    int shift = 0;
    for (;;) {
        uint8_t byte = bytebuf_read_u8(b);
        result |= (uint64_t)(byte & 0x7F) << shift;
        if (!(byte & 0x80)) break;
        shift += 7;
        if (shift > 63) break;
    }
    return result;
}

void bytebuf_write_zigzag(ByteBuf *b, int64_t v) {
    uint64_t encoded = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
    bytebuf_write_varint(b, encoded);
}

int64_t bytebuf_read_zigzag(ByteBuf *b) {
    uint64_t encoded = bytebuf_read_varint(b);
    return (int64_t)((encoded >> 1) ^ -(encoded & 1));
}

ByteBuf *bytebuf_slice(const ByteBuf *b, int start, int len) {
    if (start < 0) start = 0;
    if (start > b->len) start = b->len;
    if (start + len > b->len) len = b->len - start;
    if (len < 0) len = 0;
    ByteBuf *result = bytebuf_with_cap(len + 1);
    memcpy(result->data, b->data + start, (size_t)len);
    result->len = len;
    return result;
}

int bytebuf_cmp(const ByteBuf *a, const ByteBuf *b) {
    int minlen = a->len < b->len ? a->len : b->len;
    int r = memcmp(a->data, b->data, (size_t)minlen);
    if (r != 0) return r;
    if (a->len < b->len) return -1;
    if (a->len > b->len) return 1;
    return 0;
}

ByteBuf *bytebuf_clone(const ByteBuf *b) {
    ByteBuf *c = bytebuf_with_cap(b->cap);
    memcpy(c->data, b->data, (size_t)b->len);
    c->len = b->len;
    c->pos = 0;
    return c;
}

void bytebuf_write_length_prefixed(ByteBuf *b, const uint8_t *data, int len) {
    bytebuf_write_varint(b, (uint64_t)len);
    bytebuf_write_bytes(b, data, len);
}

int bytebuf_read_length_prefixed(ByteBuf *b, uint8_t **out) {
    uint64_t len = bytebuf_read_varint(b);
    if ((int)len > bytebuf_remaining(b)) {
        *out = NULL;
        return 0;
    }
    *out = xs_malloc((size_t)len);
    bytebuf_read_bytes(b, *out, (int)len);
    return (int)len;
}

static uint32_t bytebuf_crc32_byte(uint32_t crc, uint8_t byte) {
    crc = crc ^ byte;
    for (int i = 0; i < 8; i++) {
        if (crc & 1)
            crc = (crc >> 1) ^ 0xEDB88320;
        else
            crc = crc >> 1;
    }
    return crc;
}

uint32_t bytebuf_crc32(const ByteBuf *b) {
    uint32_t crc = 0xFFFFFFFF;
    for (int i = 0; i < b->len; i++)
        crc = bytebuf_crc32_byte(crc, b->data[i]);
    return crc ^ 0xFFFFFFFF;
}

void bytebuf_xor(ByteBuf *b, const uint8_t *key, int keylen) {
    if (keylen <= 0) return;
    for (int i = 0; i < b->len; i++)
        b->data[i] ^= key[i % keylen];
}

void bytebuf_fill(ByteBuf *b, uint8_t val, int count) {
    bytebuf_grow(b, count);
    memset(b->data + b->len, val, (size_t)count);
    b->len += count;
}

void bytebuf_compact(ByteBuf *b) {
    if (b->pos > 0) {
        int remaining = b->len - b->pos;
        if (remaining > 0)
            memmove(b->data, b->data + b->pos, (size_t)remaining);
        b->len = remaining;
        b->pos = 0;
    }
}

static void hex_byte(uint8_t byte, char *out) {
    static const char hex[] = "0123456789abcdef";
    out[0] = hex[byte >> 4];
    out[1] = hex[byte & 0x0F];
}

char *bytebuf_to_hex(const ByteBuf *b) {
    char *result = xs_malloc((size_t)(b->len * 2 + 1));
    for (int i = 0; i < b->len; i++)
        hex_byte(b->data[i], result + i * 2);
    result[b->len * 2] = '\0';
    return result;
}

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

ByteBuf *bytebuf_from_hex(const char *hex_str) {
    int slen = (int)strlen(hex_str);
    ByteBuf *b = bytebuf_with_cap(slen / 2 + 1);
    for (int i = 0; i + 1 < slen; i += 2) {
        int hi = hex_digit(hex_str[i]);
        int lo = hex_digit(hex_str[i + 1]);
        if (hi < 0 || lo < 0) continue;
        bytebuf_write_u8(b, (uint8_t)((hi << 4) | lo));
    }
    return b;
}

static const char b64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *bytebuf_to_base64(const ByteBuf *b) {
    int outlen = ((b->len + 2) / 3) * 4;
    char *out = xs_malloc((size_t)(outlen + 1));
    int j = 0;
    for (int i = 0; i < b->len; i += 3) {
        uint32_t n = ((uint32_t)b->data[i]) << 16;
        if (i + 1 < b->len) n |= ((uint32_t)b->data[i + 1]) << 8;
        if (i + 2 < b->len) n |= ((uint32_t)b->data[i + 2]);
        out[j++] = b64_chars[(n >> 18) & 0x3F];
        out[j++] = b64_chars[(n >> 12) & 0x3F];
        out[j++] = (i + 1 < b->len) ? b64_chars[(n >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < b->len) ? b64_chars[n & 0x3F] : '=';
    }
    out[j] = '\0';
    return out;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

ByteBuf *bytebuf_from_base64(const char *b64) {
    int slen = (int)strlen(b64);
    ByteBuf *b = bytebuf_with_cap(slen * 3 / 4 + 4);
    for (int i = 0; i < slen; i += 4) {
        int v[4] = {0, 0, 0, 0};
        int pad = 0;
        for (int j = 0; j < 4 && i + j < slen; j++) {
            if (b64[i + j] == '=') { pad++; v[j] = 0; }
            else { v[j] = b64_val(b64[i + j]); if (v[j] < 0) v[j] = 0; }
        }
        uint32_t n = ((uint32_t)v[0] << 18) | ((uint32_t)v[1] << 12) |
                     ((uint32_t)v[2] << 6) | (uint32_t)v[3];
        bytebuf_write_u8(b, (uint8_t)((n >> 16) & 0xFF));
        if (pad < 2) bytebuf_write_u8(b, (uint8_t)((n >> 8) & 0xFF));
        if (pad < 1) bytebuf_write_u8(b, (uint8_t)(n & 0xFF));
    }
    return b;
}

typedef struct RopeNode RopeNode;
struct RopeNode {
    enum { ROPE_LEAF, ROPE_CONCAT } kind;
    int    weight;
    int    total_len;
    int    depth;
    union {
        struct { char *data; int len; } leaf;
        struct { RopeNode *left; RopeNode *right; } concat;
    };
};

static RopeNode *rope_leaf(const char *s, int len) {
    RopeNode *n = xs_calloc(1, sizeof(RopeNode));
    n->kind = ROPE_LEAF;
    n->leaf.data = xs_malloc((size_t)(len + 1));
    memcpy(n->leaf.data, s, (size_t)len);
    n->leaf.data[len] = '\0';
    n->leaf.len = len;
    n->weight = len;
    n->total_len = len;
    n->depth = 0;
    return n;
}

static RopeNode *rope_concat(RopeNode *left, RopeNode *right) {
    if (!left) return right;
    if (!right) return left;
    RopeNode *n = xs_calloc(1, sizeof(RopeNode));
    n->kind = ROPE_CONCAT;
    n->concat.left = left;
    n->concat.right = right;
    n->weight = left->total_len;
    n->total_len = left->total_len + right->total_len;
    int ld = left->depth, rd = right->depth;
    n->depth = 1 + (ld > rd ? ld : rd);
    return n;
}

static void rope_free(RopeNode *n) {
    if (!n) return;
    if (n->kind == ROPE_LEAF) {
        free(n->leaf.data);
    } else {
        rope_free(n->concat.left);
        rope_free(n->concat.right);
    }
    free(n);
}

static char rope_index(RopeNode *n, int i) {
    while (n) {
        if (n->kind == ROPE_LEAF) {
            if (i >= 0 && i < n->leaf.len)
                return n->leaf.data[i];
            return '\0';
        }
        if (i < n->weight) {
            n = n->concat.left;
        } else {
            i -= n->weight;
            n = n->concat.right;
        }
    }
    return '\0';
}

static void rope_collect(RopeNode *n, char *out, int *pos) {
    if (!n) return;
    if (n->kind == ROPE_LEAF) {
        memcpy(out + *pos, n->leaf.data, (size_t)n->leaf.len);
        *pos += n->leaf.len;
    } else {
        rope_collect(n->concat.left, out, pos);
        rope_collect(n->concat.right, out, pos);
    }
}

static char *rope_to_string(RopeNode *n) {
    if (!n) return xs_strdup("");
    int len = n->total_len;
    char *s = xs_malloc((size_t)(len + 1));
    int pos = 0;
    rope_collect(n, s, &pos);
    s[len] = '\0';
    return s;
}

static RopeNode *rope_split_at(RopeNode *n, int pos,
                                RopeNode **out_right) {
    if (!n) { *out_right = NULL; return NULL; }
    if (pos <= 0) { *out_right = n; return NULL; }
    if (pos >= n->total_len) { *out_right = NULL; return n; }
    if (n->kind == ROPE_LEAF) {
        RopeNode *left = rope_leaf(n->leaf.data, pos);
        RopeNode *right = rope_leaf(n->leaf.data + pos, n->leaf.len - pos);
        *out_right = right;
        return left;
    }
    if (pos < n->weight) {
        RopeNode *right_of_left;
        RopeNode *new_left = rope_split_at(n->concat.left, pos, &right_of_left);
        *out_right = rope_concat(right_of_left, n->concat.right);
        return new_left;
    } else if (pos > n->weight) {
        RopeNode *right_part;
        RopeNode *new_right = rope_split_at(n->concat.right,
                                             pos - n->weight, &right_part);
        *out_right = right_part;
        return rope_concat(n->concat.left, new_right);
    } else {
        *out_right = n->concat.right;
        return n->concat.left;
    }
}

static RopeNode *rope_insert(RopeNode *rope, int pos, const char *s, int slen) {
    RopeNode *right;
    RopeNode *left = rope_split_at(rope, pos, &right);
    RopeNode *mid = rope_leaf(s, slen);
    return rope_concat(rope_concat(left, mid), right);
}

static RopeNode *rope_delete(RopeNode *rope, int pos, int count) {
    RopeNode *mid_right;
    RopeNode *left = rope_split_at(rope, pos, &mid_right);
    RopeNode *right;
    RopeNode *deleted = rope_split_at(mid_right, count, &right);
    (void)deleted;
    return rope_concat(left, right);
}

static int rope_find(RopeNode *n, const char *needle, int nlen) {
    if (!n || nlen <= 0) return -1;
    char *flat = rope_to_string(n);
    int total = n->total_len;
    int result = -1;
    for (int i = 0; i <= total - nlen; i++) {
        if (memcmp(flat + i, needle, (size_t)nlen) == 0) {
            result = i;
            break;
        }
    }
    free(flat);
    return result;
}

static RopeNode *rope_substr(RopeNode *n, int start, int len) {
    if (!n || start < 0) return rope_leaf("", 0);
    if (start >= n->total_len) return rope_leaf("", 0);
    if (start + len > n->total_len) len = n->total_len - start;
    RopeNode *right;
    RopeNode *left = rope_split_at(n, start, &right);
    (void)left;
    RopeNode *result_right;
    RopeNode *result = rope_split_at(right, len, &result_right);
    (void)result_right;
    return result;
}

static int rope_needs_rebalance(RopeNode *n) {
    if (!n || n->kind == ROPE_LEAF) return 0;
    int ld = n->concat.left ? n->concat.left->depth : 0;
    int rd = n->concat.right ? n->concat.right->depth : 0;
    int diff = ld - rd;
    if (diff < 0) diff = -diff;
    return diff > 4;
}

static RopeNode *rope_rebalance(RopeNode *n) {
    if (!n || n->kind == ROPE_LEAF) return n;
    if (!rope_needs_rebalance(n)) return n;
    char *flat = rope_to_string(n);
    int len = n->total_len;
    if (len <= 256) {
        return rope_leaf(flat, len);
    }
    int mid = len / 2;
    RopeNode *left = rope_leaf(flat, mid);
    RopeNode *right = rope_leaf(flat + mid, len - mid);
    free(flat);
    return rope_concat(left, right);
}

typedef struct {
    RopeNode *root;
} Rope;

Rope *rope_new(const char *s) {
    Rope *r = xs_calloc(1, sizeof(Rope));
    if (s && *s)
        r->root = rope_leaf(s, (int)strlen(s));
    else
        r->root = rope_leaf("", 0);
    return r;
}

void rope_destroy(Rope *r) {
    if (!r) return;
    rope_free(r->root);
    free(r);
}

int rope_length(Rope *r) {
    return r && r->root ? r->root->total_len : 0;
}

char rope_char_at(Rope *r, int i) {
    return r && r->root ? rope_index(r->root, i) : '\0';
}

void rope_append(Rope *r, const char *s) {
    if (!s || !*s) return;
    RopeNode *new_leaf = rope_leaf(s, (int)strlen(s));
    r->root = rope_concat(r->root, new_leaf);
    if (rope_needs_rebalance(r->root))
        r->root = rope_rebalance(r->root);
}

void rope_prepend(Rope *r, const char *s) {
    if (!s || !*s) return;
    RopeNode *new_leaf = rope_leaf(s, (int)strlen(s));
    r->root = rope_concat(new_leaf, r->root);
    if (rope_needs_rebalance(r->root))
        r->root = rope_rebalance(r->root);
}

void rope_insert_at(Rope *r, int pos, const char *s) {
    if (!s || !*s) return;
    r->root = rope_insert(r->root, pos, s, (int)strlen(s));
    if (rope_needs_rebalance(r->root))
        r->root = rope_rebalance(r->root);
}

void rope_delete_at(Rope *r, int pos, int count) {
    r->root = rope_delete(r->root, pos, count);
}

char *rope_flatten(Rope *r) {
    return r && r->root ? rope_to_string(r->root) : xs_strdup("");
}

int rope_index_of(Rope *r, const char *needle) {
    if (!r || !r->root || !needle) return -1;
    return rope_find(r->root, needle, (int)strlen(needle));
}

static int rope_line_count(Rope *r) {
    if (!r || !r->root) return 0;
    char *s = rope_to_string(r->root);
    int lines = 1;
    for (int i = 0; i < r->root->total_len; i++)
        if (s[i] == '\n') lines++;
    free(s);
    return lines;
}

static int rope_line_start(Rope *r, int line) {
    if (!r || !r->root || line <= 0) return 0;
    char *s = rope_to_string(r->root);
    int cur_line = 0;
    for (int i = 0; i < r->root->total_len; i++) {
        if (cur_line == line) { free(s); return i; }
        if (s[i] == '\n') cur_line++;
    }
    free(s);
    return r->root->total_len;
}

static char *rope_get_line(Rope *r, int line) {
    if (!r || !r->root) return xs_strdup("");
    char *s = rope_to_string(r->root);
    int cur_line = 0;
    int start = 0;
    for (int i = 0; i <= r->root->total_len; i++) {
        if (i == r->root->total_len || s[i] == '\n') {
            if (cur_line == line) {
                char *result = xs_strndup(s + start, (size_t)(i - start));
                free(s);
                return result;
            }
            cur_line++;
            start = i + 1;
        }
    }
    free(s);
    return xs_strdup("");
}
