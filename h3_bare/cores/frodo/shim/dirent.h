#ifndef SHIM_DIRENT_H
#define SHIM_DIRENT_H
/* bare-metal shim: код Frodo (1541fs.cpp, sysdeps.h) тянет <dirent.h>,
   newlib его отвергает («<dirent.h> not supported»). Даём минимальный
   контракт opendir/readdir/closedir; реального каталога на голом H3 нет —
   opendir всегда NULL (как atari800/fuse shim, fs_layer-стабы). */
struct dirent { char d_name[256]; };
typedef struct __dirstream DIR;
static inline DIR* opendir(const char* n){(void)n;return 0;}
static inline struct dirent* readdir(DIR* d){(void)d;return 0;}
static inline int closedir(DIR* d){(void)d;return -1;}
#endif