#include <stdarg.h>
#include <stdint.h>
#include "uart.h"

static void pputc(char c) {
    if (c == '\n') uart_putc('\r');
    uart_putc(c);
}

static int parse_width(const char** fmt) {
    if (**fmt != '0') return 0;
    (*fmt)++;
    int w = 0;
    while (**fmt >= '0' && **fmt <= '9') { w = w * 10 + (**fmt - '0'); (*fmt)++; }
    return w;
}

static void print_uint(unsigned long v, int base, int upper, int width) {
    char buf[32];
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0 && width <= 0) { pputc('0'); return; }
    while (v && i < 32) { buf[i++] = digits[v % base]; v /= base; }
    while (i < width) buf[i++] = '0';
    if (i == 0) buf[i++] = '0';
    while (i) pputc(buf[--i]);
}

static int do_printf(const char* fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { pputc(*fmt); continue; }
        fmt++;
        int width = 0;
        if (*fmt == '0') { width = parse_width(&fmt); }
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        switch (*fmt) {
        case 's': { const char* s = va_arg(ap, const char*);
                    if (!s) s = "(null)";
                    while (*s) pputc(*s++); break; }
        case 'c': pputc((char)va_arg(ap, int)); break;
        case 'd': case 'i': {
            long v = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (v < 0) { pputc('-'); v = -v; }
            print_uint((unsigned long)v, 10, 0, width); break; }
        case 'u': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 10, 0, width); break; }
        case 'x': case 'X': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 16, *fmt == 'X', width); break; }
        case 'p': { const void* v = va_arg(ap, const void*);
                    pputc('0'); pputc('x'); print_uint((unsigned long)v, 16, 0, 0); break; }
        case '%': pputc('%'); break;
        default: pputc('%'); if (*fmt) pputc(*fmt); break;
        }
    }
    return 0;
}

int uart0_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = do_printf(fmt, ap);
    va_end(ap);
    return r;
}

int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = do_printf(fmt, ap);
    va_end(ap);
    return r;
}