#ifndef _RETRO_DIRENT_H
#define _RETRO_DIRENT_H
/* Стаб вместо libretro-common: Floppy.c использует RDIR API
 * для списка дисков в папке. Мы диски не поддерживаем — всё пусто. */
struct RDIR;

struct RDIR* retro_opendir(const char* path);
int retro_readdir(struct RDIR* rdir);
const char* retro_dirent_get_name(struct RDIR* rdir);
void retro_closedir(struct RDIR* rdir);
#endif