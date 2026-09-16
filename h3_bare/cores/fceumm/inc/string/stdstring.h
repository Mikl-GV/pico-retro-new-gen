#ifndef LIBRETRO_STDSTRING_H
#define LIBRETRO_STDSTRING_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
int string_is_empty(const char *s);
int string_is_equal_noncase(const char *a, const char *b);
uint32_t string_to_bits(const char *s);
char *string_trim_whitespace(char *s);
char *string_to_nonempty_whitespace(char *s);
#ifdef __cplusplus
}
#endif
#endif
