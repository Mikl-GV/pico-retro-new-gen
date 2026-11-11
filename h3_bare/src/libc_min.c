// libc_min.c — минимальный freestanding libc для bare-metal H3.
#include <stddef.h>
#include <stdint.h>

extern void uart_putc(char c);

// r1153 (E7): word-циклы вместо побайтовых. Эти функции на hot-path
// (загрузка ROM, emu_scale, fb_*, кадры ядер). Быстрый 32-битный путь
// включается только при совпадении выравнивания источника и приёмника —
// unaligned-доступы к MMIO исключены. Порядок байтов не меняется.

void* memset(void* dst, int c, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    while (n && ((uintptr_t)d & 3u)) { *d++ = (uint8_t)c; n--; }
    if (n >= 4) {
        uint32_t pat = (uint32_t)(uint8_t)c;
        pat |= pat << 8;
        pat |= pat << 16;
        uint32_t* dw = (uint32_t*)d;
        size_t words = n >> 2;
        while (words--) *dw++ = pat;
        d = (uint8_t*)dw;
        n &= 3u;
    }
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0) {
        uint32_t* dw = (uint32_t*)d;
        const uint32_t* sw = (const uint32_t*)s;
        size_t words = n >> 2;
        while (words--) *dw++ = *sw++;
        d = (uint8_t*)dw;
        s = (const uint8_t*)sw;
        n &= 3u;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    if (d == s || n == 0) return dst;
    int word_ok = ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0);
    if (d < s) {
        if (word_ok) {
            uint32_t* dw = (uint32_t*)d;
            const uint32_t* sw = (const uint32_t*)s;
            size_t words = n >> 2;
            while (words--) *dw++ = *sw++;
            d = (uint8_t*)dw;
            s = (const uint8_t*)sw;
            n &= 3u;
        }
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        if (word_ok) {
            uint32_t* dw = (uint32_t*)d;
            const uint32_t* sw = (const uint32_t*)s;
            size_t words = n >> 2;
            while (words--) *--dw = *--sw;
            d = (uint8_t*)dw;
            s = (const uint8_t*)sw;
            n &= 3u;
        }
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void* a, const void* b, size_t n) {
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    if ((((uintptr_t)x | (uintptr_t)y) & 3u) == 0) {
        const uint32_t* xw = (const uint32_t*)x;
        const uint32_t* yw = (const uint32_t*)y;
        size_t words = n >> 2;
        while (words--) {
            if (*xw != *yw) {
                const uint8_t* xb = (const uint8_t*)xw;
                const uint8_t* yb = (const uint8_t*)yw;
                for (int i = 0; i < 4; i++)
                    if (xb[i] != yb[i]) return xb[i] - yb[i];
                break;   /* недостижимо: слова были разными */
            }
            xw++; yw++;
        }
        x = (const uint8_t*)xw;
        y = (const uint8_t*)yw;
        n &= 3u;
    }
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
        case 'd': case 'i': {
            // r0.417 (L4): INT_MIN через -v = signed overflow (UB); считаем |v|
            // в unsigned (0u - u ≡ |v| по модулю 2^32) — корректно для INT_MIN.
            // r986: %i — синоним %d (VICE использует lib_msprintf("Drive%i…")).
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
        // r1164 (E17/P2-2): %p — формат как у printf.c/vsnprintf ("0x"+hex).
        case 'p': { uintptr_t v = (uintptr_t)va_arg(ap, void*);
                    *d++ = '0'; *d++ = 'x';
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

// ---- ctype: единственная таблица на всю прошивку -------------------------
// На неё опираются макросы newlib <ctype.h> (isspace/isdigit/isupper/...)
// во ВСЕХ ядрах (PUAE, VICE, FBNeo, FCEUmm...), а libc не линкуется
// (-nostdlib). Раскладка как в newlib: индекс 0 — EOF, индекс c+1 — флаги
// символа c. Флаги из newlib ctype.h:
//   _U 01, _L 02, _N 04, _S 010, _P 020, _C 040, _X 0100, _B 0200.
// Раньше в cores/fceumm/libretro_compat.c лежала нулевая заглушка [256] —
// она удовлетворяла линкер, и настоящая таблица не подключалась: isspace()
// всегда возвращал 0. Из-за этого PUAE не мог разобрать строку инструкции
// ("ORSR.B  #1"), буфер мнемоники переполнялся, и ядро уходило в
// readcpu.c:abort() (стенд r1212, «Internal error; line 733»).
const char _ctype_[257] = {
    0x00,                                   /* EOF: индекс 0 — EOF */
    /* 0x00 */ 0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x28,0x28,0x28,0x28,0x28,0x20,0x20,
    /* 0x10 */ 0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
    /* 0x20 */ 0x88,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
    /* 0x30 */ 0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x04,0x10,0x10,0x10,0x10,0x10,0x10,
    /* 0x40 */ 0x10,0x41,0x41,0x41,0x41,0x41,0x41,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
    /* 0x50 */ 0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x10,0x10,0x10,0x10,0x10,
    /* 0x60 */ 0x10,0x42,0x42,0x42,0x42,0x42,0x42,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,
    /* 0x70 */ 0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x02,0x10,0x10,0x10,0x10,0x20,
    /* 0x80 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* 0x90 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* 0xA0 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* 0xB0 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* 0xC0 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* 0xD0 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* 0xE0 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    /* 0xF0 */ 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};

// ---- Аварийная остановка (единая точка) ----------------------------------
// Все «аварийные» выходы libc/libstdc++/ядер (abort/exit/_exit/_Exit/_kill/
// assert) идут через panic(): печатаем причину и адрес ВОЗВРАТА, затем
// останавливаем ядро. Раньше abort()/_exit() были глухими for(;;) — падения
// ядер (PUAE/VICE/FBNeo) выглядели как зависание без следа в UART.
// Адрес возврата: `arm-none-eabi-addr2line -f -e build/h3_bare.elf <ret>`.
extern int uart0_printf(const char* fmt, ...);

__attribute__((noreturn)) static void panic(const char* what, unsigned code,
                                           unsigned ret) {
    uart0_printf("\nPANIC: %s code=%u ret=%08X\n", what, code, ret);
    for (;;) __asm volatile("wfi");
}

void exit(int code) {
    panic("exit", (unsigned)code, (unsigned)(uintptr_t)__builtin_return_address(0));
}

void _exit(int code) {
    panic("_exit", (unsigned)code, (unsigned)(uintptr_t)__builtin_return_address(0));
}

void _Exit(int code) {
    panic("_Exit", (unsigned)code, (unsigned)(uintptr_t)__builtin_return_address(0));
}

void abort(void) {
    panic("abort", 0, (unsigned)(uintptr_t)__builtin_return_address(0));
}

int _kill(int pid, int sig) {
    (void)pid;
    panic("kill", (unsigned)sig, (unsigned)(uintptr_t)__builtin_return_address(0));
}

// newlib assert(): печатаем файл/строку/выражение — падения ядер на assert
// (частая практика в VICE/FBNeo) больше не молчат.
void __assert_func(const char* file, int line, const char* func, const char* expr) {
    uart0_printf("\nASSERT: %s:%d %s: %s\n",
                 file ? file : "?", line, func ? func : "?", expr ? expr : "?");
    panic("assert", (unsigned)line, (unsigned)(uintptr_t)__builtin_return_address(0));
}

int abs(int x) { return x < 0 ? -x : x; }

// newlib-заглушки (некоторые модули тянут _sbrk / _gettimeofday)
// Простой bump-аллокатор. Без free — для эмуляторов это нормально: память
// освобождается при перезапуске эмулятора (g_brk сбрасывается в emu_prepare).

// r0.417 (H2 аудита): куча newlib РАЗМЕЩЕНА ПОСЛЕ uncached-области .coherent
// (USB/OHCI: ED/TD/HCCA, 1 МБ), а не сразу за _hend — прежний вариант рос от
// _hend вверх и через ~451 КБ наезжал на .coherent → тихая порча структур
// OHCI (отвал клавиатуры/тача) при накоплении выделений между запусками игр.
// r779: куча эмуляторов вынесена в .gb_heap (linker.ld: NOLOAD 0x4A700000,
// 24 МБ). r1140: sbrk стартует сразу над концом .gb_heap (_gb_heap_end),
// лимит — _menu_arena минус резерв (SBRK_LIMIT ниже).
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

// ---- %f без libm ----------------------------------------------------------
// Нужен логам ядер: PUAE печатает частоты как "%.4f". Пока конверсии не было,
// vsnprintf не забирал аргумент-double из va_list, и ВСЕ следующие %d/%s читали
// чужие значения — лог превращался в мусор (стенд r1213: "IDX=… (▒▒▒)",
// «V=.4fHz»). Печатаем знак, целую часть и ровно prec цифр после точки.
// |v| >= 1e19 (в логах не встречается) — "inf", чтобы не переполнять счётчик.
static int fmt_double(char* out, int cap, double v, int prec) {
    int n = 0;
    const char* s;

    if (prec < 0) prec = 6;
    if (prec > 9) prec = 9;

    if (v != v) {                                   // NaN
        s = "nan";
        while (*s && n < cap) out[n++] = *s++;
        return n;
    }
    if (__builtin_isinf(v) || v >= 1.0e19 || v <= -1.0e19) {
        if (v < 0 && n < cap) out[n++] = '-';
        s = "inf";
        while (*s && n < cap) out[n++] = *s++;
        return n;
    }
    if (v < 0) { if (n < cap) out[n++] = '-'; v = -v; }

    unsigned long long ipart = (unsigned long long)v;
    double fpart = v - (double)ipart;

    // округление дробной части к prec знакам (перенос в целую — редкость)
    double scale = 1.0;
    for (int i = 0; i < prec; i++) scale *= 10.0;
    fpart += 0.5 / scale;
    if (fpart >= 1.0) { fpart -= 1.0; ipart++; }

    char ibuf[24];
    int il = 0;
    if (ipart == 0) {
        ibuf[il++] = '0';
    } else {
        while (ipart && il < (int)sizeof(ibuf)) {
            ibuf[il++] = (char)('0' + (int)(ipart % 10u));
            ipart /= 10u;
        }
    }
    while (il && n < cap) out[n++] = ibuf[--il];

    if (prec > 0) {
        if (n < cap) out[n++] = '.';
        for (int i = 0; i < prec && n < cap; i++) {
            fpart *= 10.0;
            int dg = (int)fpart;
            if (dg < 0) dg = 0;
            if (dg > 9) dg = 9;
            out[n++] = (char)('0' + dg);
            fpart -= (double)dg;
        }
    }
    return n;
}

int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) {
    // Стандартный контракт snprintf: если buf==NULL или n==0 — не писать,
    // вернуть длину строки, которая была бы записана (C99). VICE-шный
    // lib_mvsprintf (vice/src/lib.c) так запрашивает размер перед malloc;
    // без этого maxlen=0 -> lib_msprintf возвращал пустую строку, и ресурсы
    // регистрировались с пустым именем -> "Duplicated resource declaration ''".
    char* d = buf;
    size_t left = n;
    size_t out = 0;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            if (left > 1) { *d++ = *fmt; left--; }
            out++;
            continue;
        }
        fmt++;

        // Флаги '-' (влево) и '+' (всегда знак) — нужны для %f в логах ядер.
        int left_align = 0;
        while (*fmt == '-' || *fmt == '+') {
            if (*fmt == '-') left_align = 1;
            fmt++;
        }

        // Ширина: %Nd, %NX, %04X и т.п. (паддинг — нулями)
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }

        // Точность: %.4f, %2.4f (для %s — предел длины, для целых не применяется)
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') { prec = prec * 10 + (*fmt - '0'); fmt++; }
        }

        switch (*fmt) {
        // %f и родственные (e/E/g/G печатаем как f — для логов достаточно):
        // без этой ветки аргумент-double не забирался из va_list, и все
        // последующие конверсии читали чужие значения (мусор в логах ядер).
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
                    double v = va_arg(ap, double);
                    char tmp[64];
                    int len = fmt_double(tmp, (int)sizeof(tmp), v, prec);
                    int pad = width > len ? width - len : 0;
                    if (!left_align) {
                        while (pad-- > 0) { if (left > 1) { *d++ = ' '; left--; } out++; }
                    }
                    for (int i = 0; i < len; i++) { if (left > 1) { *d++ = tmp[i]; left--; } out++; }
                    if (left_align) {
                        int pad2 = width > len ? width - len : 0;
                        while (pad2-- > 0) { if (left > 1) { *d++ = ' '; left--; } out++; }
                    }
                    break; }
        case 's': { const char* s = va_arg(ap, const char*);
                    if (!s) s = "(null)";
                    // r0.417 (L4): width применяется и к %s (паддинг пробелами)
                    int sl = 0; while (s[sl]) sl++;
                    if (prec >= 0 && sl > prec) sl = prec;   // %.Ns — предел длины
                    while (sl < width) { if (left > 1) { *d++ = ' '; left--; } out++; sl++; width--; }
                    for (int i = 0; s[i] && (prec < 0 || i < prec); i++) {
                        if (left > 1) { *d++ = s[i]; left--; } out++;
                    }
                    break; }
        case 'd': case 'i': { int v = va_arg(ap, int);
                    // r0.417 (L4): INT_MIN без signed overflow (0u-u ≡ |v|)
                    unsigned u = (unsigned)v;
                    if (v < 0) { if (left > 1) { *d++ = '-'; left--; } out++; u = 0u - u; }
                    char tmp[16]; int i = 0;
                    do { tmp[i++] = '0' + (u % 10); u /= 10; } while (u);
                    while (i < width) { if (left > 1) { *d++ = '0'; left--; } out++; i++; width--; }
                    while (i) { if (left > 1) { *d++ = tmp[i - 1]; left--; } i--; out++; }
                    break; }
        case 'u': { unsigned v = va_arg(ap, unsigned);
                    char tmp[16]; int i = 0;
                    do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
                    while (i < width) { if (left > 1) { *d++ = '0'; left--; } out++; i++; width--; }
                    while (i) { if (left > 1) { *d++ = tmp[i - 1]; left--; } i--; out++; }
                    break; }
        case 'x': case 'X': { unsigned v = va_arg(ap, unsigned);
                    char tmp[16]; int i = 0;
                    do { tmp[i++] = "0123456789abcdef"[v & 0xF]; v >>= 4; } while (v);
                    while (i < width) { if (left > 1) { *d++ = '0'; left--; } out++; i++; width--; }
                    while (i) {
                        char c = tmp[--i];
                        if (*fmt == 'X' && c >= 'a' && c <= 'f') c -= 32;
                        if (left > 1) { *d++ = c; left--; }
                        out++;
                    }
                    break; }
        // r1164 (E17/P2-2): %p — как в printf.c (UART-логгер) и newlib:
        // "0x" + строчный hex без ведущих нулей, NULL → "0x0". Ширина
        // (%016p) паддит нулями после "0x", паддинг не режет значение.
        case 'p': { uintptr_t v = (uintptr_t)va_arg(ap, void*);
                    char tmp[16]; int i = 0;
                    do { tmp[i++] = "0123456789abcdef"[v & 0xF]; v >>= 4; } while (v);
                    int padn = width - (i + 2);
                    if (left > 1) { *d++ = '0'; left--; } out++;
                    if (left > 1) { *d++ = 'x'; left--; } out++;
                    while (padn-- > 0) { if (left > 1) { *d++ = '0'; left--; } out++; }
                    while (i) { if (left > 1) { *d++ = tmp[--i]; left--; } out++; }
                    break; }
        case 'l': {
            fmt++;
            if (*fmt == 'u') {
                unsigned long v = va_arg(ap, unsigned long);
                char tmp[24]; int i = 0;
                do { tmp[i++] = '0' + (v % 10); v /= 10; } while (v);
                while (i) { if (left > 1) { *d++ = tmp[i - 1]; left--; } i--; out++; }
            } else if (*fmt == 'd' || *fmt == 'i') {
                long v = va_arg(ap, long);
                // r0.417 (L4): LONG_MIN без signed overflow
                unsigned long u = (unsigned long)v;
                if (v < 0) { if (left > 1) { *d++ = '-'; left--; } out++; u = 0ul - u; }
                char tmp[24]; int i = 0;
                do { tmp[i++] = '0' + (u % 10); u /= 10; } while (u);
                while (i) { if (left > 1) { *d++ = tmp[i - 1]; left--; } i--; out++; }
            }
            break; }
        case 'c': { if (left > 1) { *d++ = (char)va_arg(ap, int); left--; } out++; break; }
        default: if (left > 1) { *d++ = *fmt; left--; } out++; break;
        }
    }
    if (buf && n > 0) {
        if (left > 0) *d = 0;
        else buf[n - 1] = 0;   // r1140: ровное заполнение (left==0) — гарантированный NUL
    }
    return (int)out;
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
