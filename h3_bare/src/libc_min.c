// libc_min.c — минимальный freestanding libc для bare-metal H3.
#include <stddef.h>
#include <stdint.h>

extern void uart_putc(char c);

void* memset(void* dst, int c, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    while (n--) {
        if (*x != *y) return *x - *y;
        x++; y++;
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t n = 0;
    while (*s++) n++;
    return n;
}

int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

char* strcpy(char* dst, const char* src) {
    char* d = dst;
    while ((*d++ = *src++)) {}
    return dst;
}

char* strcat(char* dst, const char* src) {
    char* d = dst;
    while (*d) d++;
    while ((*d++ = *src++)) {}
    return dst;
}

char* strncpy(char* dst, const char* src, size_t n) {
    char* d = dst;
    while (n > 0 && *src) { *d++ = *src++; n--; }
    while (n > 0) { *d++ = 0; n--; }
    return dst;
}

int strncmp(const char* a, const char* b, size_t n) {
    while (n > 0 && *a && *a == *b) { a++; b++; n--; }
    if (n == 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && *h == *n) { h++; n++; }
        if (!*n) return (char*)haystack;
    }
    return 0;
}

// Атомарные операции (используются spinlock, если понадобятся).
// Тип возврата должен совпадать с встроенной функцией GCC
// (unsigned int), иначе -Wbuiltin-declaration-mismatch.
unsigned int __sync_val_compare_and_swap_4(volatile void* ptr, unsigned int oldval, unsigned int newval) {
    // fallback: без SMP на старте это некритично; заглушка
    volatile unsigned int* p = (volatile unsigned int*)ptr;
    unsigned int cur = *p;
    if (cur == oldval) *p = newval;
    return cur;
}

void __sync_synchronize(void) {
    __asm volatile("dmb ish" ::: "memory");
}

// ---- LCG rand/srand для MCUME (ядра используют rand()) ----
static uint32_t g_rand_seed = 1;

int rand(void) {
    g_rand_seed = g_rand_seed * 1664525u + 1013904223u;
    return (int)((g_rand_seed >> 16) & 0x7FFF);
}

void srand(unsigned int seed) {
    g_rand_seed = seed ? seed : 1;
}

// ---- sprintf (очень минимальный, только для %s/%d/%x) ----
#include <stdarg.h>
int sprintf(char* buf, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char* d = buf;
    for (; *fmt; fmt++) {
        if (*fmt != '%') { *d++ = *fmt; continue; }
        fmt++;
        switch (*fmt) {
        case 's': { const char* s = va_arg(ap, const char*);
                    while (*s) *d++ = *s++;
                    break; }
        case 'd': {
            int v = va_arg(ap, int);
            if (v < 0) { *d++ = '-'; v = -v; }
            char tmp[16]; int i = 0;
            do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
            while (i) *d++ = tmp[--i];
            break; }
        case 'x': case 'X': {
            unsigned v = va_arg(ap, unsigned);
            char tmp[16]; int i = 0;
            do { tmp[i++] = "0123456789abcdef"[v & 0xF]; v >>= 4; } while (v);
            while (i) *d++ = tmp[--i];
            break; }
        default: *d++ = *fmt; break;
        }
    }
    *d = 0;
    va_end(ap);
    return (int)(d - buf);
}

void exit(int code) {
    (void)code;
    while (1) __asm volatile("wfi");
}

int abs(int x) { return x < 0 ? -x : x; }

// newlib-заглушки (некоторые модули тянут _sbrk / _gettimeofday)
// Простой bump-аллокатор от _hend (конец образа) вверх. Без free —
// для эмуляторов это нормально: память освобождается при перезапуске
// эмулятора (ядро инициализируется заново, куча растёт только вверх).
extern char _hend[];

/* Верхняя граница bump-кучи: _menu_arena (0x4F000000) — за ней ROM_BUF
 * (0x50000000) и EMU_FB (0x5F800000). Не даём _sbrk наехать на них:
 * при переполнении возвращаем (void*)-1, как стандартный sbrk. */
#define SBRK_LIMIT 0x4F000000u

static char* g_brk = 0;

void* _sbrk(int incr) {
    if (!g_brk) g_brk = _hend;
    char* cur = g_brk;
    if (incr > 0) {
        uintptr_t a = ((uintptr_t)cur + 7u) & ~(uintptr_t)7u;
        uintptr_t next = a + (uintptr_t)incr;
        if (next >= SBRK_LIMIT) return (void*)-1;   // переполнение кучи
        g_brk = (char*)next;
        return (void*)a;
    }
    g_brk = cur + incr;
    return cur;
}

int _gettimeofday(void* tv, void* tz) {
    (void)tv; (void)tz;
    return -1;
}

int _unlink(const char* path) {
    (void)path;
    return -1;
}

int putchar(int c) {
    uart_putc((char)c);   // из uart.h — но libc_min не включает его; объявим ниже
    return c;
}

int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
    char* d = buf;
    size_t left = n;
    for (; *fmt && left > 1; fmt++) {
        if (*fmt != '%') { *d++ = *fmt; left--; continue; }
        fmt++;

        // Ширина: %Nd, %NX, %04X и т.п. (паддинг — нулями)
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        switch (*fmt) {
        case 's': { const char* s = va_arg(ap, const char*);
                    if (!s) s = "(null)";
                    while (*s && left > 1) { *d++ = *s++; left--; } break; }
        case 'd': { int v = va_arg(ap, int);
                    if (v < 0) { if (left > 1) { *d++ = '-'; left--; } v = -v; }
                    char tmp[16]; int i = 0;
                    do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
                    while (i < width && left > 1) { *d++ = '0'; left--; width--; }
                    while (i && left > 1) { *d++ = tmp[--i]; left--; }
                    break; }
        case 'u': { unsigned v = va_arg(ap, unsigned);
                    char tmp[16]; int i = 0;
                    do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
                    while (i < width && left > 1) { *d++ = '0'; left--; width--; }
                    while (i && left > 1) { *d++ = tmp[--i]; left--; }
                    break; }
        case 'x': case 'X': { unsigned v = va_arg(ap, unsigned);
                    char tmp[16]; int i = 0;
                    do { tmp[i++] = "0123456789abcdef"[v & 0xF]; v >>= 4; } while (v);
                    while (i < width && left > 1) { *d++ = '0'; left--; width--; }
                    while (i && left > 1) {
                        char c = tmp[--i];
                        if (*fmt == 'X' && c >= 'a' && c <= 'f') c -= 32;
                        *d++ = c; left--;
                    }
                    break; }
        case 'l': {
            fmt++;
            if (*fmt == 'u') {
                unsigned long v = va_arg(ap, unsigned long);
                char tmp[24]; int i = 0;
                do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
                while (i && left > 1) { *d++ = tmp[--i]; left--; }
            } else if (*fmt == 'd' || *fmt == 'i') {
                long v = va_arg(ap, long);
                if (v < 0) { if (left > 1) { *d++ = '-'; left--; } v = -v; }
                char tmp[24]; int i = 0;
                do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
                while (i && left > 1) { *d++ = tmp[--i]; left--; }
            }
            break; }
        case 'c': { if (left > 1) { *d++ = (char)va_arg(ap, int); left--; } break; }
        default: if (left > 1) { *d++ = *fmt; left--; } break;
        }
    }
    if (left > 0) *d = 0;
    return (int)(d - buf);
}

int snprintf(char* buf, size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf, n, fmt, ap);
    va_end(ap);
    return r;
}

int strcasecmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    while (*a && (*a | 0x20) == (*b | 0x20)) { a++; b++; }
    return (int)((unsigned char)*a | 0x20) - (int)((unsigned char)*b | 0x20);
}

int strncasecmp(const char* a, const char* b, size_t n) {
    while (n > 0 && *a && (*a | 0x20) == (*b | 0x20)) { a++; b++; n--; }
    if (n == 0) return 0;
    return (int)((unsigned char)*a | 0x20) - (int)((unsigned char)*b | 0x20);
}

unsigned long strtoul(const char* s, char** endptr, int base) {
    const char* p = s;
    unsigned long v = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (base == 0) {
        if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) { base = 16; p += 2; }
        else if (p[0] == '0') { base = 8; p++; }
        else base = 10;
    } else if (base == 16 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    while (*p) {
        int d;
        if (*p >= '0' && *p <= '9') d = *p - '0';
        else if (*p >= 'a' && *p <= 'f') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'F') d = *p - 'A' + 10;
        else break;
        if (d >= base) break;
        v = v * (unsigned long)base + (unsigned long)d;
        p++;
    }
    if (endptr) *endptr = (char*)p;
    return v;
}