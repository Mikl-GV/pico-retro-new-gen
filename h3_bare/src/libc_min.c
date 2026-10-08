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

char* strchr(const char* s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return (c == 0) ? (char*)s : 0;
}

// Атомарные операции (используются spinlock и guard-переменными libstdc++).
// Тип возврата должен совпадать с встроенной функцией GCC
// (unsigned int), иначе -Wbuiltin-declaration-mismatch.
unsigned int __sync_val_compare_and_swap_4(volatile void* ptr, unsigned int oldval, unsigned int newval) {
    // r0.365 (H5 аудита): раньше была простая проверка+запись без атомарности —
    // при SMP (h3_smp) gcc guard-переменные и spinlock могли рассинхрониться.
    // Теперь честный CAS через LDREX/STREX + dmb (Cortex-A7, ARMv7).
    volatile unsigned int* p = (volatile unsigned int*)ptr;
    unsigned int cur, tmp;
    __asm__ __volatile__(
        "1: ldrex %0, [%2]\n"
        "    cmp   %0, %3\n"
        "    bne   2f\n"
        "    strex %1, %4, [%2]\n"
        "    teq   %1, #0\n"
        "    bne   1b\n"
        "2:  dmb   ish\n"
        : "=&r" (cur), "=&r" (tmp)
        : "r" (p), "r" (oldval), "r" (newval)
        : "memory", "cc");
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
            // r0.417 (L4): INT_MIN через -v = signed overflow (UB); считаем |v|
            // в unsigned (0u - u ≡ |v| по модулю 2^32) — корректно для INT_MIN.
            int v = va_arg(ap, int);
            unsigned u = (unsigned)v;
            if (v < 0) { *d++ = '-'; u = 0u - u; }
            char tmp[16]; int i = 0;
            do { tmp[i++] = '0' + (u % 10); u /= 10; } while (u);
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
// Простой bump-аллокатор. Без free — для эмуляторов это нормально: память
// освобождается при перезапуске эмулятора (g_brk сбрасывается в emu_prepare).

// r0.417 (H2 аудита): куча newlib РАЗМЕЩЕНА ПОСЛЕ uncached-области .coherent
// (USB/OHCI: ED/TD/HCCA, 1 МБ), а не сразу за _hend — прежний вариант рос от
// _hend вверх и через ~451 КБ наезжал на .coherent → тихая порча структур
// OHCI (отвал клавиатуры/тача) при накоплении выделений между запусками игр.
// r725 (уточнение): старт = libh3_coherent_region + 1 МБ (после uncached-окна
// и L1-таблицы 0x4A40C000..0x4A410000), лимит = _menu_arena (0x4F000000):
// за ним ROM_BUF (0x50000000) и EMU_FB (0x5F800000). При переполнении
// возвращаем (void*)-1, как стандартный sbrk.
extern unsigned char libh3_coherent_region[];
// r779: куча эмуляторов вынесена ЗА uncached-зону (linker.ld: .gb_heap 0x4A700000).
// sbrk стартует сразу над кучей (0x4AC00000), лимит — _menu_arena (0x4F000000).
extern unsigned char _gb_heap_end[];
#define SBRK_START ((char*)_gb_heap_end)
// r780: лимит кучи — НЕ 0x4F000000 (это НАЧАЛО _menu_arena, linker.ld).
// Резервируем 1 МБ под арену меню, чтобы переполнение newlib-кучи не
// затирало g_items/g_dir_names (арена меню лежит там же в DRAM).
#define SBRK_LIMIT 0x4EF00000u

static char* g_brk = 0;

void newlib_heap_reset(void) {
    g_brk = 0;
}

void* _sbrk(int incr) {
    if (!g_brk) g_brk = SBRK_START;
    char* cur = g_brk;
    if (incr > 0) {
        uintptr_t a = ((uintptr_t)cur + 7u) & ~(uintptr_t)7u;
        uintptr_t next = a + (uintptr_t)incr;
        if (next >= SBRK_LIMIT) return (void*)-1;   // переполнение кучи
        g_brk = (char*)next;
        return (void*)a;
    }
    g_brk = cur + incr;
    // r0.417 (L4): incr<0 (trim) не должен увести кучу ниже старта
    if ((uintptr_t)g_brk < (uintptr_t)SBRK_START) g_brk = SBRK_START;
    return cur;
}

int _gettimeofday(void* tv, void* tz) {
    // r180: newlib time() читает tv.tv_sec при успешном возврате 0; оставлять
    // стековый мусор нельзя — иначе rand-seed'ы эмуляторов получают мусор.
    (void)tz;
    if (tv) { volatile char* p = (volatile char*)tv; for (size_t i = 0; i < 16; i++) p[i] = 0; }
    return -1;   // время недоступно — time() вернёт (time_t)-1
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
                    // r0.417 (L4): width применяется и к %s (паддинг пробелами)
                    int sl = 0; while (s[sl]) sl++;
                    while (sl < width && left > 1) { *d++ = ' '; left--; width--; }
                    while (*s && left > 1) { *d++ = *s++; left--; } break; }
        case 'd': { int v = va_arg(ap, int);
                    // r0.417 (L4): INT_MIN без signed overflow (0u-u ≡ |v|)
                    unsigned u = (unsigned)v;
                    if (v < 0) { if (left > 1) { *d++ = '-'; left--; } u = 0u - u; }
                    char tmp[16]; int i = 0;
                    do { tmp[i++] = '0' + (u % 10); u /= 10; } while (u);
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
                // r0.417 (L4): LONG_MIN без signed overflow
                unsigned long u = (unsigned long)v;
                if (v < 0) { if (left > 1) { *d++ = '-'; left--; } u = 0ul - u; }
                char tmp[24]; int i = 0;
                do { tmp[i++] = '0' + (u % 10); u /= 10; } while (u);
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

// F4/F5 (r541): регистронезависимое сравнение — понижаем ТОЛЬКО 'A'..'Z'.
// Прежний `| 0x20` по всем байтам давал ложное равенство пар @≡`, [≡{,
// \≡|, ]≡}, ^≡~. Здесь сравниваем нормализованные значения (в unsigned char,
// чтобы знак не влиял на порядок).
static inline int ascii_lc(int c) {
    return (c >= 'A' && c <= 'Z') ? (c + 0x20) : c;
}

int strcasecmp(const char* a, const char* b) {
    while (*a && ascii_lc((unsigned char)*a) == ascii_lc((unsigned char)*b)) { a++; b++; }
    return ascii_lc((unsigned char)*a) - ascii_lc((unsigned char)*b);
}

int strncasecmp(const char* a, const char* b, size_t n) {
    while (n > 0 && ascii_lc((unsigned char)*a) == ascii_lc((unsigned char)*b)) {
        if (*a == 0) return 0;   // оба конца строки — равны (иначе уйдём за '\0')
        a++; b++; n--;
    }
    if (n == 0) return 0;
    return ascii_lc((unsigned char)*a) - ascii_lc((unsigned char)*b);
}