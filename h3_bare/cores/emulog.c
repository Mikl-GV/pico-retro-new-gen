// emulog.c — единый каркас диагностического лога сессий эмуляторов.
// См. emulog.h. Реализация хелперов блока start/exit и полей.
// Автономный объект (линкуется последним, см. Makefile) — правка emulog.c
// не сдвигает адреса .text остальных ядер (правило владельца об USB).

#include <stdarg.h>
#include <stdint.h>
#include "emulog.h"

extern int printf(const char* fmt, ...);
extern int vsnprintf(char* buf, size_t size, const char* fmt, va_list ap);

// Единственный гейт всего сессионного лога. 1 = выводить (релиз), 0 = молчать.
// Для отладки: выставить 0 в одном месте (или поставить брейк и поменять).
int g_emulog = 1;

// r1413: ширина ТЕГА тоже фиксирована (MS1504=5, PXT=3, Portfolio=9 — раньше
// тело уезжало). Паддинг — вручную: printf проекта не обязан поддерживать '*'.
#define EMULOG_TAG_W   8
#define EMULOG_FIELD_W 8
static void pad_copy(char* dst, const char* src, int width) {
    int n = 0;
    while (src[n] && n < width) { dst[n] = src[n]; n++; }
    while (n < width) dst[n++] = ' ';
    dst[n] = 0;
}
static void print_field(const char* tag, const char* field, const char* body) {
    char t[EMULOG_TAG_W + 1], f[EMULOG_FIELD_W + 1];
    pad_copy(t, tag, EMULOG_TAG_W);
    pad_copy(f, field, EMULOG_FIELD_W);
    printf("%s: %s %s\n", t, f, body);
}

void emulog_begin(const char* tag) {
    if (!g_emulog) return;
    printf("==== %s: start ====\n", tag);
}

void emulog_end(const char* tag) {
    if (!g_emulog) return;
    printf("==== %s: exit ====\n", tag);
    char t[EMULOG_TAG_W + 1];
    pad_copy(t, tag, EMULOG_TAG_W);
    printf("%s: done\n", t);
}

void emulog_field(const char* tag, const char* field, const char* fmt, ...) {
    char body[160];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    print_field(tag, field, body);
}

void emulog_frames(const char* tag, uint32_t frames, uint32_t duration_us) {
    uint32_t fps10 = duration_us
        ? (uint32_t)(((uint64_t)frames * 10000000u) / duration_us) : 0;
    char b[64];
    int n = 0;
    if (frames) {
        n = 0;
        b[n++] = (char)('0' + (fps10 / 10u) % 10u);
        b[n++] = '.';
        b[n++] = (char)('0' + fps10 % 10u);
    }
    b[n] = 0;
    print_field(tag, "frames", frames ? b : "?");
    // fps — отдельным полем, чтобы не ломать парсинг frames
    if (frames) {
        char fb[32];
        int m = 0;
        if (fps10 >= 1000u) fb[m++] = (char)('0' + (fps10 / 1000u) % 10u);
        fb[m++] = (char)('0' + (fps10 / 100u) % 10u);
        fb[m++] = (char)('0' + (fps10 / 10u) % 10u);
        fb[m++] = '.';
        fb[m++] = (char)('0' + fps10 % 10u);
        fb[m] = 0;
        print_field(tag, "fps", fb);
    }
}