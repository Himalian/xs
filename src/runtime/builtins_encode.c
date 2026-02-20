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
#include <ctype.h>

/* encoding: base64, hex, json */

static const char b64_table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static Value *native_encode_base64_encode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str("");
    const uint8_t *in = (const uint8_t*)a[0]->s;
    size_t len = strlen(a[0]->s);
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = xs_malloc(out_len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t val = (uint32_t)in[i] << 16;
        if (i+1 < len) val |= (uint32_t)in[i+1] << 8;
        if (i+2 < len) val |= (uint32_t)in[i+2];
        out[j++] = b64_table[(val >> 18) & 0x3f];
        out[j++] = b64_table[(val >> 12) & 0x3f];
        out[j++] = (i+1 < len) ? b64_table[(val >> 6) & 0x3f] : '=';
        out[j++] = (i+2 < len) ? b64_table[val & 0x3f] : '=';
    }
    out[j] = '\0';
    Value *r = xs_str(out);
    free(out);
    return r;
}

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static Value *native_encode_base64_decode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str("");
    const char *in = a[0]->s;
    size_t len = strlen(in);
    if (len % 4 != 0) return xs_str("");
    size_t out_len = len / 4 * 3;
    if (len > 0 && in[len-1] == '=') out_len--;
    if (len > 1 && in[len-2] == '=') out_len--;
    char *out = xs_malloc(out_len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i += 4) {
        int v0 = b64_decode_char(in[i]);
        int v1 = b64_decode_char(in[i+1]);
        int v2 = (in[i+2] == '=') ? 0 : b64_decode_char(in[i+2]);
        int v3 = (in[i+3] == '=') ? 0 : b64_decode_char(in[i+3]);
        if (v0<0||v1<0) break;
        uint32_t val = ((uint32_t)v0<<18)|((uint32_t)v1<<12)|((uint32_t)v2<<6)|(uint32_t)v3;
        out[j++] = (char)((val >> 16) & 0xff);
        if (in[i+2] != '=') out[j++] = (char)((val >> 8) & 0xff);
        if (in[i+3] != '=') out[j++] = (char)(val & 0xff);
    }
    out[j] = '\0';
    Value *r = xs_str_n(out, j);
    free(out);
    return r;
}

static Value *native_encode_hex_encode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str("");
    const uint8_t *in = (const uint8_t*)a[0]->s;
    size_t len = strlen(a[0]->s);
    char *out = xs_malloc(len * 2 + 1);
    for (size_t i = 0; i < len; i++) sprintf(out + i*2, "%02x", in[i]);
    out[len*2] = '\0';
    Value *r = xs_str(out);
    free(out);
    return r;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static Value *native_encode_hex_decode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str("");
    const char *in = a[0]->s;
    size_t len = strlen(in);
    if (len % 2 != 0) return xs_str("");
    size_t out_len = len / 2;
    char *out = xs_malloc(out_len + 1);
    for (size_t i = 0; i < out_len; i++) {
        int hi = hex_val(in[i*2]);
        int lo = hex_val(in[i*2+1]);
        if (hi < 0 || lo < 0) { free(out); return xs_str(""); }
        out[i] = (char)((hi << 4) | lo);
    }
    out[out_len] = '\0';
    Value *r = xs_str_n(out, out_len);
    free(out);
    return r;
}

static Value *native_encode_url_encode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str("");
    const char *in = a[0]->s;
    size_t len = strlen(in);
    /* worst case: every char becomes %XX (3x) */
    char *out = xs_malloc(len * 3 + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)in[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = (char)c;
        } else {
            sprintf(out + j, "%%%02X", c);
            j += 3;
        }
    }
    out[j] = '\0';
    Value *r = xs_str(out);
    free(out);
    return r;
}

static Value *native_encode_url_decode(Interp *ig, Value **a, int n) {
    (void)ig;
    if (n < 1 || VAL_TAG(a[0]) != XS_STR) return xs_str("");
    const char *in = a[0]->s;
    size_t len = strlen(in);
    char *out = xs_malloc(len + 1);
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (in[i] == '%' && i+2 < len) {
            int hi = hex_val(in[i+1]);
            int lo = hex_val(in[i+2]);
            if (hi >= 0 && lo >= 0) {
                out[j++] = (char)((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        if (in[i] == '+') out[j++] = ' ';
        else out[j++] = in[i];
    }
    out[j] = '\0';
    Value *r = xs_str_n(out, j);
    free(out);
    return r;
}

Value *make_encode_module(void) {
    XSMap *m = map_new();
    map_take(m, "base64_encode", xs_native(native_encode_base64_encode));
    map_take(m, "base64_decode", xs_native(native_encode_base64_decode));
    map_take(m, "hex_encode",    xs_native(native_encode_hex_encode));
    map_take(m, "hex_decode",    xs_native(native_encode_hex_decode));
    map_take(m, "url_encode",    xs_native(native_encode_url_encode));
    map_take(m, "url_decode",    xs_native(native_encode_url_decode));
    return xs_module(m);
}
