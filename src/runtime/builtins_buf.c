#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "core/xs_compat.h"
#include "runtime/interp.h"
#include "runtime/builtins.h"
#include "core/value.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* byte buffers */

typedef struct {
    uint8_t *data;
    int      len;
    int      cap;
    int      pos; /* read position */
} XSBuf;

static XSBuf *buf_create(int cap) {
    XSBuf *b = xs_malloc(sizeof(XSBuf));
    b->cap = cap > 0 ? cap : 64;
    b->data = xs_malloc((size_t)b->cap);
    b->len = 0;
    b->pos = 0;
    return b;
}

static void buf_ensure(XSBuf *b, int need) {
    while (b->len + need > b->cap) {
        b->cap *= 2;
        b->data = xs_realloc(b->data, (size_t)b->cap);
    }
}

/* We store the XSBuf pointer as an int (cast). A bit hacky but simple. */
static XSBuf *buf_from_map(XSMap *m) {
    Value *pv = map_get(m, "_ptr");
    if (!pv || VAL_TAG(pv) != XS_INT) return NULL;
    return (XSBuf*)(uintptr_t)VAL_INT(pv);
}

static Value *native_buf_new(Interp *ig, Value **a, int n) {
    (void)ig;
    int cap = (n > 0 && VAL_TAG(a[0]) == XS_INT) ? (int)VAL_INT(a[0]) : 64;
    XSBuf *b = buf_create(cap);
    XSMap *m = map_new();
    map_take(m, "_ptr", xs_int((int64_t)(uintptr_t)b));
    return xs_module(m);
}

static Value *native_buf_write_u8(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_MAP) return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    uint8_t val = (VAL_TAG(a[1]) == XS_INT) ? (uint8_t)VAL_INT(a[1]) : 0;
    buf_ensure(b, 1);
    b->data[b->len++] = val;
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_write_u16(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_MAP) return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    uint16_t val = (VAL_TAG(a[1]) == XS_INT) ? (uint16_t)VAL_INT(a[1]) : 0;
    buf_ensure(b, 2);
    b->data[b->len++] = (uint8_t)(val & 0xff);
    b->data[b->len++] = (uint8_t)((val >> 8) & 0xff);
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_write_u32(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_MAP) return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    uint32_t val = (VAL_TAG(a[1]) == XS_INT) ? (uint32_t)VAL_INT(a[1]) : 0;
    buf_ensure(b, 4);
    for (int i=0;i<4;i++) b->data[b->len++] = (uint8_t)((val >> (i*8)) & 0xff);
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_write_u64(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_MAP) return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    uint64_t val = (VAL_TAG(a[1]) == XS_INT) ? (uint64_t)VAL_INT(a[1]) : 0;
    buf_ensure(b, 8);
    for (int i=0;i<8;i++) b->data[b->len++] = (uint8_t)((val >> (i*8)) & 0xff);
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_read_u8(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->pos >= b->len) return xs_int(0);
    return xs_int(b->data[b->pos++]);
}

static Value *native_buf_read_u16(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->pos + 2 > b->len) return xs_int(0);
    uint16_t v = (uint16_t)b->data[b->pos] | ((uint16_t)b->data[b->pos+1] << 8);
    b->pos += 2;
    return xs_int(v);
}

static Value *native_buf_read_u32(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->pos + 4 > b->len) return xs_int(0);
    uint32_t v = 0;
    for (int i=0;i<4;i++) v |= ((uint32_t)b->data[b->pos++] << (i*8));
    return xs_int((int64_t)v);
}

static Value *native_buf_read_u64(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->pos + 8 > b->len) return xs_int(0);
    uint64_t v = 0;
    for (int i=0;i<8;i++) v |= ((uint64_t)b->data[b->pos++] << (i*8));
    return xs_int((int64_t)v);
}

static Value *native_buf_write_str(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 2 || VAL_TAG(a[0]) != XS_MAP || VAL_TAG(a[1]) != XS_STR)
        return value_incref(XS_NULL_VAL);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return value_incref(XS_NULL_VAL);
    size_t slen = strlen(a[1]->s);
    buf_ensure(b, (int)slen);
    memcpy(b->data + b->len, a[1]->s, slen);
    b->len += (int)slen;
    return value_incref(XS_NULL_VAL);
}

static Value *native_buf_to_str(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_MAP) return xs_str("");
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return xs_str("");
    return xs_str_n((const char*)b->data, (size_t)b->len);
}

static Value *native_buf_to_hex(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_MAP) return xs_str("");
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b || b->len == 0) return xs_str("");
    char *hex = xs_malloc((size_t)b->len * 2 + 1);
    for (int i = 0; i < b->len; i++) sprintf(hex + i*2, "%02x", b->data[i]);
    hex[b->len * 2] = '\0';
    Value *r = xs_str(hex);
    free(hex);
    return r;
}

static Value *native_buf_len(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_MAP) return xs_int(0);
    XSBuf *b = buf_from_map(a[0]->map);
    if (!b) return xs_int(0);
    return xs_int(b->len);
}

Value *make_buf_module(void) {
    XSMap *m = map_new();
    map_take(m, "new",       xs_native(native_buf_new));
    map_take(m, "write_u8",  xs_native(native_buf_write_u8));
    map_take(m, "write_u16", xs_native(native_buf_write_u16));
    map_take(m, "write_u32", xs_native(native_buf_write_u32));
    map_take(m, "write_u64", xs_native(native_buf_write_u64));
    map_take(m, "read_u8",   xs_native(native_buf_read_u8));
    map_take(m, "read_u16",  xs_native(native_buf_read_u16));
    map_take(m, "read_u32",  xs_native(native_buf_read_u32));
    map_take(m, "read_u64",  xs_native(native_buf_read_u64));
    map_take(m, "write_str", xs_native(native_buf_write_str));
    map_take(m, "to_str",    xs_native(native_buf_to_str));
    map_take(m, "to_hex",    xs_native(native_buf_to_hex));
    map_take(m, "len",       xs_native(native_buf_len));
    return xs_module(m);
}
