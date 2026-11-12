// uae4all_stubs.c — стабы POSIX-функций для ядра UAE4All, которых нет в
// bare-metal newlib/fs_layer. r1243 (замена PUAE).
// - glob/globfree: файловые шаблоны (disk.cpp загрузка образов) — bare-metal
//   fs_layer не имеет glob; заглушка возвращает «нет совпадений» (игра
//   грузится по прямому пути через RPATH, glob не используется);
// - usleep: микросекундная пауза по HS-таймеру H3 (udelay из проекта).
#include <stddef.h>
#include <stdint.h>
#include "h3_hs_timer.h"

typedef struct {
    size_t gl_pathc;
    char **gl_pathv;
    size_t gl_offs;
    int __unused;
} glob_t;

int glob(const char *pattern, int flags, int (*errfunc)(const char *, int),
         glob_t *pglob)
{
    (void)pattern; (void)flags; (void)errfunc;
    if (pglob) { pglob->gl_pathc = 0; pglob->gl_pathv = NULL; pglob->gl_offs = 0; }
    return 0;   // нет совпадений — не ошибка
}

void globfree(glob_t *pglob)
{
    (void)pglob;
}

#include <unistd.h>
int usleep(useconds_t usec)
{
    extern void udelay(uint32_t us);
    udelay((uint32_t)usec);
    return 0;
}