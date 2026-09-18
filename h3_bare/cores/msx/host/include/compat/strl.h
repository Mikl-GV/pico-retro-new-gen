#ifndef _COMPAT_STRL_H
#define _COMPAT_STRL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
size_t strlcpy(char* dst, const char* src, size_t siz);
#ifdef __cplusplus
}
#endif
#endif
