#ifndef SHIM_DIRENT_H
#define SHIM_DIRENT_H
/* bare-metal shim: Fuse includes <dirent.h> via compat.h, newlib's errors out */
struct dirent { char d_name[256]; };
typedef struct __dirstream DIR;
static inline DIR* opendir(const char* n){(void)n;return 0;}
static inline struct dirent* readdir(DIR* d){(void)d;return 0;}
static inline int closedir(DIR* d){(void)d;return -1;}
#endif
