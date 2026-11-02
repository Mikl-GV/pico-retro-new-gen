// c64_stubs.c — стабы для VICE (Commodore 64/128) на freestanding H3.
// Всё, что newlib не даёт в bare-metal, а ядро/retrodep упоминают на уровне
// символов. НЕ дублируем то, что уже глобально дают другие слои прошивки
// (linkcheck «дубль»): FILE-слой fopen/fread/* — a8_fs.o; path_vfs_init —
// fuse libretro-common; time — gba_compat.o.
#include <stdint.h>
#include <stddef.h>

// vfs/dirent: ядро зовёт dirent_vfs_init при снятии GET_VFS_INTERFACE
// (libretro-core.c:5581). У нас каталоги не используются — стаб.
void dirent_vfs_init(const void* iface) { (void)iface; }

// ---- мелкие POSIX-функции, которых нет в freestanding newlib ----
int getpid(void) { return 0; }

// r974: retrodep/archdep.c зовёт chdir (архитектура) и nanosleep (ticks/delay) —
// на bare-metal no-op-стабы (каталоги и засыпание не нужны).
int chdir(const char* path) { (void)path; return 0; }
struct timespec;
int nanosleep(const struct timespec* req, struct timespec* rem) {
    (void)req; (void)rem;
    return 0;
}