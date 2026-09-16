#ifndef LIBRETRO_FILE_PATH_H
#define LIBRETRO_FILE_PATH_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int path_is_valid(const char *path);
void fill_pathname_join(char *out, const char *a, const char *b, size_t size);
char *fill_pathname_base(char *out, const char *in_path, size_t size);
#ifdef __cplusplus
}
#endif
#endif
