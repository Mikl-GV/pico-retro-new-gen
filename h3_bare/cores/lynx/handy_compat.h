// handy_compat.h — заглушки libretro-common для bare-metal H3.
// Ромы грузим напрямую в память (нет файловой системы в эмуляторе).
#ifndef HANDY_COMPAT_H
#define HANDY_COMPAT_H

#include <stddef.h>
#include <string.h>

// ---- строки ----
static inline int string_is_empty(const char* s) { return !s || !s[0]; }
static inline int path_is_valid(const char* s) { (void)s; return 0; }

static inline size_t compat_strlcpy(char* dst, const char* src, size_t n) {
    if (!n) return strlen(src);
    size_t i = 0;
    while (src[i] && i < n - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return strlen(src);
}
static inline size_t compat_strlcat(char* dst, const char* src, size_t n) {
    size_t d = 0;
    while (d < n && dst[d]) d++;
    if (d >= n) return d + strlen(src);
    size_t i = 0;
    while (src[i] && d + i < n - 1) { dst[d + i] = src[i]; i++; }
    dst[d + i] = 0;
    return d + strlen(src);
}
#define strlcpy compat_strlcpy
#define strlcat compat_strlcat

// ---- файлы: всё no-op, ROM грузится host-слоем напрямую ----
typedef struct RFILE { int dummy; } RFILE;
enum {
    RETRO_VFS_FILE_ACCESS_READ = 1,
    RETRO_VFS_FILE_ACCESS_WRITE = 2,
    RETRO_VFS_FILE_ACCESS_HINT_NONE = 0,
    RETRO_VFS_SEEK_POSITION_START = 0,
    RETRO_VFS_SEEK_POSITION_CURRENT = 1,
    RETRO_VFS_SEEK_POSITION_END = 2
};

static inline RFILE* filestream_open(const char* f, int a, int h) { (void)f; (void)a; (void)h; return 0; }
static inline void filestream_close(RFILE* f) { (void)f; }
static inline int filestream_read(RFILE* f, void* b, size_t s) { (void)f; (void)b; (void)s; return 0; }
static inline int filestream_write(RFILE* f, const void* b, size_t s) { (void)f; (void)b; (void)s; return 0; }
static inline int filestream_seek(RFILE* f, int off, int o) { (void)f; (void)off; (void)o; return 0; }
static inline int filestream_tell(RFILE* f) { (void)f; return 0; }

#define PATH_MAX_LENGTH 4096
#define fill_pathname_resolve_relative(dst, base, suffix, sz) \
    do { (void)(dst); (void)(base); (void)(suffix); (void)(sz); dst[0] = 0; } while(0)

// ---- CRC32 ----
static unsigned long crc32(unsigned long crc, const unsigned char* buf, unsigned len) {
    static unsigned long table[256];
    static int table_ready = 0;
    unsigned long c;
    int n, k;
    unsigned i;
    if (!table_ready) {
        for (n = 0; n < 256; n++) {
            c = (unsigned long)n;
            for (k = 0; k < 8; k++)
                c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
            table[n] = c;
        }
        table_ready = 1;
    }
    c = crc ^ 0xFFFFFFFFUL;
    for (i = 0; i < len; i++)
        c = table[(c ^ buf[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFUL;
}

#endif