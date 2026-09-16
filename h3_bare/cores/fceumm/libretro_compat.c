// libretro_compat.c — минимальные стабы libretro-совместимости для FCEUmm
// на bare-metal H3. Всё, что связано с файлами, работает поверх встроенного
// ROM-буфера (память), реальной файловой системы здесь нет.
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "libretro.h"

// ---- string-хелперы ----
int string_is_equal_noncase(const char* a, const char* b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return !*a && !*b;
}

uint32_t string_to_bits(const char* s) {
    uint32_t bits = 0;
    if (!s) return 0;
    for (; *s; s++) {
        if (*s >= '0' && *s <= '9') bits |= (1u << (*s - '0'));
        else if (*s >= 'a' && *s <= 'z') bits |= (1u << (*s - 'a' + 10));
        else if (*s >= 'A' && *s <= 'Z') bits |= (1u << (*s - 'A' + 10));
    }
    return bits;
}

char* string_trim_whitespace(char* s) {
    if (!s) return NULL;
    char* end;
    while (*s && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
    end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) end--;
    *end = 0;
    return s;
}

void fill_pathname_join(char* out, const char* a, const char* b, size_t size) {
    if (!out || size == 0) return;
    out[0] = 0;
    if (a && *a) {
        size_t n = strlen(a);
        if (n >= size) n = size - 1;
        memcpy(out, a, n); out[n] = 0;
        if (out[n-1] != '/') { if (n < size-1) { out[n] = '/'; out[n+1] = 0; } }
    }
    if (b && *b) strlcat(out, b, size);
}

// ---- filestream: не нужен (ROM подаётся буфером), но ядро ссылается ----
typedef struct {
    const uint8_t* data;
    uint64_t size;
    uint64_t pos;
} rfile_t;

RFILE* filestream_open(const char* path, unsigned mode, unsigned hints) {
    (void)path; (void)mode; (void)hints;
    return NULL;
}

void filestream_close(RFILE* stream) { (void)stream; }

int64_t filestream_read(RFILE* stream, void* data, uint64_t len) {
    (void)stream; (void)data; (void)len;
    return 0;
}

int64_t filestream_seek(RFILE* stream, int64_t offset, int whence) {
    (void)stream; (void)offset; (void)whence;
    return 0;
}

int64_t filestream_tell(RFILE* stream) { (void)stream; return 0; }
int64_t filestream_get_size(RFILE* stream) { (void)stream; return 0; }

// ---- path/string ----
int path_is_valid(const char* path) { (void)path; return 0; }
int string_is_empty(const char* s) { return !s || !*s; }

// ---- memstream: state.c использует для сейвов (RAM-буфер) ----
typedef struct {
    uint8_t* data;
    uint64_t size;
    uint64_t pos;
    int writing;
} memstream_t_impl;

memstream_t* memstream_open(uint8_t* data, uint64_t size, unsigned writing) {
    memstream_t_impl* m = (memstream_t_impl*)malloc(sizeof(memstream_t_impl));
    if (!m) return NULL;
    m->data = data; m->size = size; m->pos = 0; m->writing = writing;
    return (memstream_t*)m;
}

void memstream_close(memstream_t* stream) { free(stream); }

uint64_t memstream_read(memstream_t* stream, void* data, uint64_t bytes) {
    memstream_t_impl* m = (memstream_t_impl*)stream;
    if (m->writing) return 0;
    uint64_t n = bytes;
    if (m->pos + n > m->size) n = m->size - m->pos;
    memcpy(data, m->data + m->pos, n);
    m->pos += n;
    return n;
}

uint64_t memstream_write(memstream_t* stream, const void* data, uint64_t bytes) {
    memstream_t_impl* m = (memstream_t_impl*)stream;
    if (!m->writing) return 0;
    uint64_t n = bytes;
    if (m->pos + n > m->size) n = m->size - m->pos;
    memcpy(m->data + m->pos, data, n);
    m->pos += n;
    return n;
}

uint64_t memstream_pos(memstream_t* stream) {
    return ((memstream_t_impl*)stream)->pos;
}

int memstream_getc(memstream_t* stream) {
    memstream_t_impl* m = (memstream_t_impl*)stream;
    uint8_t c;
    if (m->writing || m->pos >= m->size) return EOF;
    c = m->data[m->pos++];
    return c;
}

void memstream_putc(memstream_t* stream, int c) {
    memstream_t_impl* m = (memstream_t_impl*)stream;
    if (!m->writing || m->pos >= m->size) return;
    m->data[m->pos++] = (uint8_t)c;
}

int64_t memstream_seek(memstream_t* stream, int64_t offset, int whence) {
    memstream_t_impl* m = (memstream_t_impl*)stream;
    int64_t base = (whence == 1) ? (int64_t)m->pos :
                   (whence == 2) ? (int64_t)m->size : 0;
    m->pos = (uint64_t)(base + offset);
    return (int64_t)m->pos;
}

// ---- strlcpy/strlcat (некоторые файлы ядра используют) ----
size_t strlcpy(char* dst, const char* src, size_t siz) {
    size_t slen = strlen(src);
    if (siz) {
        size_t n = (slen < siz - 1) ? slen : siz - 1;
        memcpy(dst, src, n);
        dst[n] = 0;
    }
    return slen;
}

size_t strlcat(char* dst, const char* src, size_t siz) {
    size_t dlen = strnlen(dst, siz);
    if (dlen == siz) return dlen + strlen(src);
    return dlen + strlcpy(dst + dlen, src, siz - dlen);
}
// ---- недостающий libc для -nostdlib ----
size_t strnlen(const char* s, size_t maxlen) {
    size_t n = 0;
    while (n < maxlen && s[n]) n++;
    return n;
}

int sscanf(const char* str, const char* fmt, ...) {
    // Минимальный sscanf для %x/%u/%d/%s (нужен cheat.c: "%02x%02x%02x%02x")
    va_list ap;
    va_start(ap, fmt);
    int conv = 0;
    const char* d = str;
    for (; *fmt; fmt++) {
        if (*fmt == ' ' || *fmt == '\t') { while (*d == ' ' || *d == '\t') d++; continue; }
        if (*fmt != '%') { if (*d == *fmt) { d++; continue; } break; }
        fmt++;
        while (*fmt >= '0' && *fmt <= '9') fmt++;   // width
        if (*fmt == 'h') fmt++;
        if (*fmt == 'l') fmt++;
        if (*fmt == 'x' || *fmt == 'X') {
            unsigned* p = va_arg(ap, unsigned*);
            unsigned v = 0; int any = 0;
            while ((*d >= '0' && *d <= '9') || (*d >= 'a' && *d <= 'f') || (*d >= 'A' && *d <= 'F')) {
                char c = *d++;
                v = (v << 4) | (c >= '0' && c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
                any = 1;
            }
            if (!any) break;
            *p = v; conv++;
        } else if (*fmt == 'd' || *fmt == 'i') {
            int* p = va_arg(ap, int*);
            int neg = 0; int v = 0; int any = 0;
            if (*d == '-') { neg = 1; d++; }
            while (*d >= '0' && *d <= '9') { v = v * 10 + (*d++ - '0'); any = 1; }
            if (!any) break;
            *p = neg ? -v : v; conv++;
        } else if (*fmt == 'u') {
            unsigned* p = va_arg(ap, unsigned*);
            unsigned v = 0; int any = 0;
            while (*d >= '0' && *d <= '9') { v = v * 10 + (*d++ - '0'); any = 1; }
            if (!any) break;
            *p = v; conv++;
        } else if (*fmt == 's') {
            char* p = va_arg(ap, char*);
            while (*d == ' ' || *d == '\t') d++;
            int any = 0;
            while (*d && *d != ' ' && *d != '\t' && *d != '\n' && *d != '\r') *p++ = *d++, any = 1;
            *p = 0;
            if (!any) break;
            conv++;
        } else if (*fmt == 'c') {
            char* p = va_arg(ap, char*);
            *p = *d++; conv++;
        } else {
            break;
        }
    }
    va_end(ap);
    return conv;
}

// newlib ctype-функции используют extern _ctype_[], но мы линкуем -nostdlib.
// Определяем нулевую таблицу — мапперы (coolgirl.c) ссылаются на неё.
const char _ctype_[256] = { 0 };
