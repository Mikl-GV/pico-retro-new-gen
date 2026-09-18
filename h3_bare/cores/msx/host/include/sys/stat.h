#ifndef _SYS_STAT_H
#define _SYS_STAT_H
/* Минимальный стаб sys/stat.h — Floppy.c использует stat() для
 * проверки регулярности файла. Без дисков всегда -1/0. */
#include <stdint.h>

struct stat {
    uint32_t st_mode;
    int64_t  st_size;
};

#define S_IFMT  0170000
#define S_IFREG 0100000
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)

static inline int stat(const char* path, struct stat* buf) {
    (void)path; (void)buf;
    return -1;
}
#endif