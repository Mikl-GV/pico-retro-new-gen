#ifndef SHIM_DIRENT_H
#define SHIM_DIRENT_H
/* bare-metal shim: код ядра тянет <dirent.h>, newlib его отвергает.
   mkdir/rmdir не объявляем — они есть в newlib (sys/stat.h). */
struct dirent { char d_name[256]; };
typedef struct __dirstream DIR;
static inline DIR* opendir(const char* n){(void)n;return 0;}
static inline struct dirent* readdir(DIR* d){(void)d;return 0;}
static inline int closedir(DIR* d){(void)d;return -1;}
#endif
