// printf.c — минимальный printf для bare-metal (поддержка %s %c %d %u %x %p %ld %lu).
#include <stdarg.h>
#include <stdint.h>
#include "uart.h"

static void print_uint(unsigned long long v, int base, int upper) {
    char buf[32];
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    if (v == 0) { uart_putc('0'); return; }
    while (v && i < 32) { buf[i++] = digits[v % base]; v /= base; }
    while (i) uart_putc(buf[--i]);
}

int uart0_printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { uart_putc(*fmt); continue; }
        fmt++;
        // длина (l)
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        switch (*fmt) {
        case 's': { const char* s = va_arg(ap, const char*);
                    if (!s) s = "(null)";
                    uart_puts(s); break; }
        case 'c': uart_putc((char)va_arg(ap, int)); break;
        case 'd': case 'i': {
            long v = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (v < 0) { uart_putc('-'); v = -v; }
            print_uint((unsigned long)v, 10, 0); break; }
        case 'u': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 10, 0); break; }
        case 'x': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 16, 0); break; }
        case 'X': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 16, 1); break; }
        case 'p': uart_puts("0x"); print_uint((unsigned long)va_arg(ap, void*), 16, 0); break;
        case '%': uart_putc('%'); break;
        default: uart_putc('%'); if (*fmt) uart_putc(*fmt); break;
        }
    }
    va_end(ap);
    return 0;
}

// совместимость: некоторые файлы вызывают printf()
int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { uart_putc(*fmt); continue; }
        fmt++;
        // длина (l)
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        switch (*fmt) {
        case 's': { const char* s = va_arg(ap, const char*);
                    if (!s) s = "(null)";
                    uart_puts(s); break; }
        case 'c': uart_putc((char)va_arg(ap, int)); break;
        case 'd': case 'i': {
            long v = lng ? va_arg(ap, long) : (long)va_arg(ap, int);
            if (v < 0) { uart_putc('-'); v = -v; }
            print_uint((unsigned long)v, 10, 0); break; }
        case 'u': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 10, 0); break; }
        case 'x': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 16, 0); break; }
        case 'X': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 16, 1); break; }
        case 'p': uart_puts("0x"); print_uint((unsigned long)va_arg(ap, void*), 16, 0); break;
        case '%': uart_putc('%'); break;
        default: uart_putc('%'); if (*fmt) uart_putc(*fmt); break;
        }
    }
    va_end(ap);
    return 0;
}