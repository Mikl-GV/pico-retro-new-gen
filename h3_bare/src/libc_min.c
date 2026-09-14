// libc_min.c — минимальный freestanding libc для bare-metal H3.
#include <stddef.h>
#include <stdint.h>

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

// Атомарные операции (используются spinlock, если понадобятся)
int __sync_val_compare_and_swap_4(volatile void* ptr, int oldval, int newval) {
    // fallback: без SMP на старте это некритично; заглушка
    volatile int* p = (volatile int*)ptr;
    int cur = *p;
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
                    while (*s) *d++ = *s++; break; }
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