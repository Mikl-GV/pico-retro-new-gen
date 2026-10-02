#include <stdarg.h>
#include <stdint.h>
#include "uart.h"

static void pputc(char c) {
    if (c == '\n') uart_putc('\r');
    uart_putc(c);
}

// r540 (F3): паддинг-символ вынесен параметром — '%05X' → нули, '%5d' → пробелы.
static void print_uint(unsigned long v, int base, int upper, int width, char pad) {
    char buf[32];
    const char* digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int i = 0;
    // do/while: минимум одна цифра (для v==0 → '0'), иначе '%5u' от 0 дал бы
    // поле из одних пробелов без нуля.
    do { buf[i++] = digits[v % base]; v /= base; } while (v && i < 32);
    // r0.417 (L5 аудита): клампим width — раньше "%09999999d" писал за buf[32]
    if (width > 31) width = 31;
    while (i < width) buf[i++] = pad;
    while (i) pputc(buf[--i]);
}

// r540 (F3): знаковое число — '-' входит в поле ширины и стоит по стандарту:
// '%5d' от -42 → "  -42" (пробелы ПЕРЕД знаком), '%05d' → "-0042" (знак до нулей).
static void print_int(long v, int width, char pad) {
    char buf[32];
    int i = 0;
    unsigned long u = (unsigned long)v;   // F2: модуль без signed overflow
    int neg = (v < 0);
    if (neg) u = 0ul - u;
    do { buf[i++] = (char)('0' + (u % 10)); u /= 10; } while (u && i < 32);
    if (width > 31) width = 31;
    int need = width - i - (neg ? 1 : 0);
    if (neg && pad == '0') pputc('-');
    while (need-- > 0) pputc(pad);
    if (neg && pad != '0') pputc('-');
    while (i) pputc(buf[--i]);
}

static int do_printf(const char* fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { pputc(*fmt); continue; }
        fmt++;
        // F3 (r540): ширина — '%5d' И '%05d'. Ведущий '0' = паддинг нулями,
        // иначе — пробелами (как в стандартном printf). Прежний код принимал
        // ширину только после '0', поэтому '%5d' печатался как литерал.
        int width = 0;
        char pad = ' ';
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        int lng = 0;
        while (*fmt == 'l') { lng++; fmt++; }
        switch (*fmt) {
        case 's': { const char* s = va_arg(ap, const char*);
                    if (!s) s = "(null)";
                    // r582 (F8): '%5s' — паддинг пробелами до/после строки.
                    int len = 0; { const char* p = s; while (*p++) len++; }
                    if (width > 31) width = 31;
                    for (int sp = width - len; sp > 0; sp--) pputc(' ');   // right-align
                    while (*s) pputc(*s++);
                    break; }
        case 'c': { char c = (char)va_arg(ap, int);
                    if (width > 31) width = 31;
                    for (int sp = width - 1; sp > 0; sp--) pputc(' ');
                    pputc(c); break; }
        case 'd': case 'i':
            print_int(lng ? va_arg(ap, long) : (long)va_arg(ap, int), width, pad);
            break;
        case 'u': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 10, 0, width, pad); break; }
        case 'x': case 'X': {
            unsigned long v = lng ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned);
            print_uint(v, 16, *fmt == 'X', width, pad); break; }
        case 'p': { const void* v = va_arg(ap, const void*);
                    pputc('0'); pputc('x'); print_uint((unsigned long)v, 16, 0, 0, ' '); break; }
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