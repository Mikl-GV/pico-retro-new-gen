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