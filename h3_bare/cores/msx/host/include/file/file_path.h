#ifndef _FILE_FILE_PATH_H
#define _FILE_FILE_PATH_H
#include <stddef.h>
/* Минимальный стаб: fill_pathname_join/path_is_directory для fMSX.
 * ProgDir = NULL — работаем в корне, пути не собираем. */
static inline void fill_pathname_join(char* dst, const char* base, const char* name, size_t size) {
    (void)base; (void)dst; (void)name; (void)size;
}

static inline int path_is_directory(const char* path) {
    (void)path;
    return 0;
}
#endif