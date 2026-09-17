// retro_miscellaneous.h — минимальная заглушка libretro-common:
// константы и declspec, которые тянут snes9x.h/port.h/libretro_core_options.h.
// Оригинал живёт в libretro-common/include/retro_miscellaneous.h, но он тянет
// vfs/retro_environment и пр. — здесь только то, что реально используется.

#ifndef RETRO_MISCELLANEOUS_H__
#define RETRO_MISCELLANEOUS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

#define RETRO_API

#ifdef RARCH_INTERNAL
#define RETRO_HW_RENDER_INTERFACE_CUSTOM
#define RARCHINTERNAL_H__
#endif

/* Deprecated enums kept for ABI-compatibility */
enum retro_language
{
   RETRO_LANGUAGE_ENGLISH             = 0,
   RETRO_LANGUAGE_JAPANESE            = 1,
   RETRO_LANGUAGE_FRENCH              = 2,
   RETRO_LANGUAGE_SPANISH             = 3,
   RETRO_LANGUAGE_GERMAN              = 4,
   RETRO_LANGUAGE_ITALIAN             = 5,
   RETRO_LANGUAGE_DUTCH               = 6,
   RETRO_LANGUAGE_PORTUGUESE_BRAZIL   = 7,
   RETRO_LANGUAGE_PORTUGUESE_PORTUGAL = 8,
   RETRO_LANGUAGE_RUSSIAN             = 9,
   RETRO_LANGUAGE_KOREAN              = 10,
   RETRO_LANGUAGE_CHINESE_TRADITIONAL = 11,
   RETRO_LANGUAGE_CHINESE_SIMPLIFIED  = 12,
   RETRO_LANGUAGE_ESPERANTO           = 13,
   RETRO_LANGUAGE_POLISH              = 14,
   RETRO_LANGUAGE_VIETNAMESE          = 15,
   RETRO_LANGUAGE_LAST
};

/* RARCH_INTERNAL guard; только константы, которые нужны компиляции
 * (retro_core_options.h / libretro.h). */

#ifdef __cplusplus
}
#endif

#endif