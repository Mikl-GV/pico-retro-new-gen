// fuse_stubs.c — newlib-заглушки файловых syscalls, которые тянет Fuse/libretro-common.
// Файловая система не нужна (ROM/состояния подаём буфером), поэтому — no-op.

#include <stddef.h>

#if defined(__GNUC__)
#define UNUSED __attribute__((unused))
#endif

int _stat(const char* path, void* buf)   { (void)path; (void)buf; return -1; }
int mkdir(const char* path, int mode)    { (void)path; (void)mode; return -1; }
int ftruncate(int fd, long length)       { (void)fd; (void)length; return -1; }
int _link(const char* a, const char* b)  { (void)a; (void)b; return -1; }
