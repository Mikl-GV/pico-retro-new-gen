#ifndef LIBRETRO_STRL_H
#define LIBRETRO_STRL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t strlcpy(char *dst, const char *src, size_t siz);
size_t strlcat(char *dst, const char *src, size_t siz);
#ifdef __cplusplus
}
#endif
#endif
