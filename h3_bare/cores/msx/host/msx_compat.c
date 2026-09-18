// msx_compat.c — стабы libretro-common/POSIX для порта fMSX на bare-metal H3.
// Даёт ядру fMSX видимость RFILE (файловый ввод-вывод) поверх FAT SD,
// плюс недостающие libc-функции (sscanf, strcasestr, time/rand и т.п.).
//
// Политика файлов: BIOS-файлы (MSX2.ROM, MSX2EXT.ROM и т.д.) вшиты в образ
// (см. msx_roms.c); ROM-картриджи и диски читаются с SD через fat_*.
// rfopen() ищет сначала во встроенных BIOS, потом на SD /roms/msx/bios/.

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "fat.h"
#include "msx_compat.h"

extern int printf(const char* fmt, ...);

// sram_disk_ptr — глобал, объявленный extern в FDIDisk.c (SRAM-диск).
// У нас диски не поддерживаются — NULL.
uint8_t* sram_disk_ptr = NULL;

// ---- встроенные BIOS (генерятся xxd из fMSX/ROMs) ----
extern unsigned char fMSX_ROMs_MSX2_ROM[];     // MSX2.ROM  (32768)
extern unsigned int  fMSX_ROMs_MSX2_ROM_len;
extern unsigned char fMSX_ROMs_MSX2EXT_ROM[];  // MSX2EXT.ROM (16384)
extern unsigned int  fMSX_ROMs_MSX2EXT_ROM_len;

#define MAX_OPEN_FILES 16
typedef struct {
    bool used;
    const uint8_t* data;      // указатель на данные (ROM/SD-буфер)
    int32_t  size;
    int32_t  pos;
    int32_t  cap;             // для записи: ёмкость буфера
    uint8_t* wbuf;            // для записи: динамический буфер
    bool     writeable;
} msx_file_t;

static msx_file_t g_files[MAX_OPEN_FILES];

// ---- встроенные BIOS ----
typedef struct { const char* name; const uint8_t* data; int32_t size; } msx_bios_t;
static const msx_bios_t g_bios[] = {
    { "MSX2.ROM",    fMSX_ROMs_MSX2_ROM,    32768 },
    { "MSX2EXT.ROM", fMSX_ROMs_MSX2EXT_ROM, 16384 },
    { 0, 0, 0 }
};

static const msx_bios_t* find_bios(const char* name) {
    for (const msx_bios_t* b = g_bios; b->name; b++) {
        if (strcmp(b->name, name) == 0) return b;
    }
    return 0;
}

// ---- буфер картриджа, подаваемый хосту (CARTA.ROM / CARTB.ROM) ----
static const uint8_t* g_cart[2] = { 0, 0 };
static uint32_t g_cart_size[2] = { 0, 0 };

void msx_compat_set_cart(const uint8_t* rom, uint32_t size) {
    g_cart[0] = rom;
    g_cart_size[0] = size;
}

static const uint8_t* find_cart(const char* name, uint32_t* size) {
    if (strcmp(name, "CARTA.ROM") == 0) { *size = g_cart_size[0]; return g_cart[0]; }
    if (strcmp(name, "CARTB.ROM") == 0) { *size = g_cart_size[1]; return g_cart[1]; }
    return 0;
}

static int alloc_slot(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++)
        if (!g_files[i].used) { g_files[i].used = true; return i; }
    return -1;
}

// Полное имя на SD: /roms/msx/bios/<name>
static void make_bios_path(const char* name, char* out, int cap) {
    int p = 0;
    const char* pfx = "/roms/msx/bios/";
    while (*pfx && p < cap - 1) out[p++] = *pfx++;
    while (*name && p < cap - 1) out[p++] = *name++;
    out[p] = 0;
}

// ---- публичные rf* ----
RFILE* rfopen(const char* path, const char* mode) {
    if (!path) return 0;

    // 1) Встроенный BIOS
    const msx_bios_t* b = find_bios(path);
    if (b) {
        int s = alloc_slot();
        if (s < 0) return 0;
        g_files[s].data = b->data;
        g_files[s].size = b->size;
        g_files[s].pos  = 0;
        g_files[s].cap  = 0;
        g_files[s].wbuf = 0;
        g_files[s].writeable = 0;
        return (RFILE*)(intptr_t)(s + 1);
    }

    // 2) Картридж, поданный хостом (буфер в памяти)
    {
        uint32_t csz = 0;
        const uint8_t* cdata = find_cart(path, &csz);
        if (cdata && csz > 0) {
            int s = alloc_slot();
            if (s < 0) return 0;
            g_files[s].data = cdata;
            g_files[s].size = (int32_t)csz;
            g_files[s].pos  = 0;
            g_files[s].cap  = 0;
            g_files[s].wbuf = 0;
            g_files[s].writeable = 0;
            return (RFILE*)(intptr_t)(s + 1);
        }
    }

    // 2) Файл на SD: /roms/msx/bios/<name>
    char buf[FAT_NAME_LEN + 32];
    const char* name = path;
    // если передан "CMOS.ROM" и т.п. без пути — ищем в биос-папке
    if (name[0] != '/') {
        make_bios_path(name, buf, sizeof(buf));
        name = buf;
    }

    // разделяем путь на директорию и имя
    char dir[FAT_NAME_LEN];
    char fname[FAT_NAME_LEN];
    const char* slash = strrchr(name, '/');
    if (!slash) { return 0; }
    int dlen = (int)(slash - name);
    if (dlen >= FAT_NAME_LEN) dlen = FAT_NAME_LEN - 1;
    memcpy(dir, name, dlen);
    dir[dlen] = 0;
    strncpy(fname, slash + 1, FAT_NAME_LEN - 1);
    fname[FAT_NAME_LEN - 1] = 0;

    fat_entry_t f;
    if (fat_find(dir, fname, &f) != 0) {
        // BIOS не найден — возвращаем NULL (ядро обработает)
        return 0;
    }

    int s = alloc_slot();
    if (s < 0) return 0;

    if (mode[0] == 'r' || strchr(mode, '+')) {
        // Читаем весь файл в выделенный буфер
        if (f.size <= 0 || f.size > (16 << 20)) { g_files[s].used = false; return 0; }
        uint8_t* data = (uint8_t*)malloc(f.size);
        if (!data) { g_files[s].used = false; return 0; }
        int r = fat_read_file(&f, 0, data, f.size);
        if (r != (int)f.size) { free(data); g_files[s].used = false; return 0; }
        g_files[s].data = data;
        g_files[s].size = f.size;
        g_files[s].pos  = 0;
        g_files[s].cap  = f.size;
        g_files[s].wbuf = data;
        g_files[s].writeable = (mode[0] == 'w' || strchr(mode, '+')) ? 1 : 0;
    } else {
        g_files[s].used = false;
        return 0;
    }
    return (RFILE*)(intptr_t)(s + 1);
}

int rfclose(RFILE* stream) {
    int s = (int)(intptr_t)stream - 1;
    if (s < 0 || s >= MAX_OPEN_FILES || !g_files[s].used) return 0;
    if (g_files[s].wbuf) free(g_files[s].wbuf);
    g_files[s].used = false;
    return 0;
}

int64_t rfread(void* data, size_t elem_size, size_t elem_count, RFILE* stream) {
    int s = (int)(intptr_t)stream - 1;
    if (s < 0 || s >= MAX_OPEN_FILES || !g_files[s].used) return 0;
    msx_file_t* f = &g_files[s];
    int64_t bytes = (int64_t)(elem_size * elem_count);
    if (f->pos + bytes > f->size) bytes = f->size - f->pos;
    if (bytes <= 0) return 0;
    memcpy(data, f->data + f->pos, (size_t)bytes);
    f->pos += (int32_t)bytes;
    return bytes / (int64_t)elem_size;
}

int64_t rfwrite(const void* data, size_t elem_size, size_t elem_count, RFILE* stream) {
    int s = (int)(intptr_t)stream - 1;
    if (s < 0 || s >= MAX_OPEN_FILES || !g_files[s].used) return 0;
    msx_file_t* f = &g_files[s];
    if (!f->writeable || !f->wbuf) return 0;
    int64_t bytes = (int64_t)(elem_size * elem_count);
    if (f->pos + bytes > f->cap) bytes = f->cap - f->pos;
    if (bytes <= 0) return 0;
    memcpy(f->wbuf + f->pos, data, (size_t)bytes);
    f->pos += (int32_t)bytes;
    return bytes / (int64_t)elem_size;
}

int64_t rfseek(RFILE* stream, int64_t offset, int whence) {
    int s = (int)(intptr_t)stream - 1;
    if (s < 0 || s >= MAX_OPEN_FILES || !g_files[s].used) return -1;
    msx_file_t* f = &g_files[s];
    int64_t np;
    switch (whence) {
    case SEEK_SET: np = offset; break;
    case SEEK_CUR: np = f->pos + offset; break;
    case SEEK_END: np = f->size + offset; break;
    default: return -1;
    }
    if (np < 0) return -1;
    f->pos = (int32_t)np;
    return 0;
}

int64_t rftell(RFILE* stream) {
    int s = (int)(intptr_t)stream - 1;
    if (s < 0 || s >= MAX_OPEN_FILES || !g_files[s].used) return -1;
    return g_files[s].pos;
}

int rfeof(RFILE* stream) {
    int s = (int)(intptr_t)stream - 1;
    if (s < 0 || s >= MAX_OPEN_FILES || !g_files[s].used) return 1;
    return g_files[s].pos >= g_files[s].size;
}

int rfgetc(RFILE* stream) {
    int s = (int)(intptr_t)stream - 1;
    if (s < 0 || s >= MAX_OPEN_FILES || !g_files[s].used) return -1;
    msx_file_t* f = &g_files[s];
    if (f->pos >= f->size) return -1;
    return f->data[f->pos++];
}

int rfputc(int c, RFILE* stream) {
    int s = (int)(intptr_t)stream - 1;
    if (s < 0 || s >= MAX_OPEN_FILES || !g_files[s].used) return -1;
    msx_file_t* f = &g_files[s];
    if (!f->writeable || !f->wbuf || f->pos >= f->cap) return -1;
    f->wbuf[f->pos++] = (uint8_t)c;
    return c;
}

char* rfgets(char* s, int size, RFILE* stream) {
    int s_ = (int)(intptr_t)stream - 1;
    if (s_ < 0 || s_ >= MAX_OPEN_FILES || !g_files[s_].used) return 0;
    msx_file_t* f = &g_files[s_];
    int i = 0;
    while (i < size - 1 && f->pos < f->size) {
        char c = (char)f->data[f->pos++];
        s[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = 0;
    return s;
}

void filestream_rewind(RFILE* stream) {
    int s = (int)(intptr_t)stream - 1;
    if (s >= 0 && s < MAX_OPEN_FILES && g_files[s].used)
        g_files[s].pos = 0;
}

// ---- retro_dirent стабы (Floppy.c): диски не поддерживаем ----
struct RDIR { int dummy; };
struct RDIR* retro_opendir(const char* path) { (void)path; return 0; }
int retro_readdir(struct RDIR* rdir) { (void)rdir; return 0; }
const char* retro_dirent_get_name(struct RDIR* rdir) { (void)rdir; return 0; }
void retro_closedir(struct RDIR* rdir) { (void)rdir; }

int sscanf(const char* str, const char* fmt, ...) {
    // Минимальная реализация: %d %u %x %s %c %f* — для MCF/CHT-парсинга.
    // %f пропускаем (не используется ядром).
    va_list ap;
    va_start(ap, fmt);
    int count = 0;
    const char* p = fmt;
    const char* s = str;
    while (*p && *s) {
        if (*p == ' ') { p++; while (*s == ' ') s++; continue; }
        if (*p != '%') {
            if (*s != *p) break;
            s++; p++; continue;
        }
        p++;
        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        if (*p == 'l' || *p == 'h') { p++; }
        switch (*p) {
        case 'd': case 'i': {
            int v = 0, sign = 1, n = 0;
            if (*s == '-') { sign = -1; s++; }
            while (*s >= '0' && *s <= '9' && (width == 0 || n < width)) {
                v = v * 10 + (*s - '0'); s++; n++;
            }
            if (n == 0) { va_end(ap); return count; }
            *(int*)va_arg(ap, int*) = v * sign;
            count++; break;
        }
        case 'u': {
            unsigned v = 0; int n = 0;
            while (*s >= '0' && *s <= '9' && (width == 0 || n < width)) {
                v = v * 10 + (*s - '0'); s++; n++;
            }
            if (n == 0) { va_end(ap); return count; }
            *(unsigned*)va_arg(ap, unsigned*) = v;
            count++; break;
        }
        case 'x': case 'X': {
            unsigned v = 0; int n = 0;
            while ((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f') ||
                   (*s >= 'A' && *s <= 'F')) {
                if (width && n >= width) break;
                int d = (*s <= '9') ? *s - '0' :
                        (*s <= 'F') ? *s - 'A' + 10 : *s - 'a' + 10;
                v = (v << 4) | d; s++; n++;
            }
            if (n == 0) { va_end(ap); return count; }
            *(unsigned*)va_arg(ap, unsigned*) = v;
            count++; break;
        }
        case 's': {
            char* out = va_arg(ap, char*);
            while (*s == ' ') s++;
            int n = 0;
            while (*s && *s != ' ' && *s != '\n' && *s != '\r' &&
                   *s != '\t' && (width == 0 || n < width)) {
                *out++ = *s++; n++;
            }
            if (n == 0) { va_end(ap); return count; }
            *out = 0;
            count++; break;
        }
        case 'c': {
            char* out = va_arg(ap, char*);
            if (!*s) { va_end(ap); return count; }
            *out = *s++; count++; break;
        }
        default:
            va_end(ap); return count;
        }
        while (*p && *p != ' ' && *p != '%') p++;
    }
    va_end(ap);
    return count;
}

char* strcasestr(const char* h, const char* n) {
    if (!*n) return (char*)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*a && *b && ((*a | 0x20) == (*b | 0x20))) { a++; b++; }
        if (!*b) return (char*)h;
    }
    return 0;
}

// chdir/getcwd — заглушки: fMSX использует ProgDir=NULL, значит работаем в корне
int chdir(const char* path) { (void)path; return 0; }
char* getcwd(char* buf, size_t size) {
    if (buf && size > 0) { buf[0] = '/'; buf[1] = 0; }
    return buf;
}

// time/rand — детерминированные заглушки
time_t time(time_t* t) { if (t) *t = 0; return 0; }
struct tm* localtime(const time_t* t) {
    static struct tm tm0;
    (void)t;
    tm0.tm_year = 2026 - 1900; tm0.tm_mon = 0; tm0.tm_mday = 1;
    tm0.tm_hour = 0; tm0.tm_min = 0; tm0.tm_sec = 0;
    return &tm0;
}

// strlcpy (нужен где-то в ядре)
size_t strlcpy(char* dst, const char* src, size_t siz) {
    if (!siz) return strlen(src);
    size_t i = 0;
    while (i < siz - 1 && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return strlen(src);
}

// fill_pathname_join — не нужен (ProgDir=NULL), но оставим пустым
void fill_pathname_join(char* dst, const char* a, const char* b, size_t size) {
    (void)a; (void)b; (void)size;
    if (dst && size) dst[0] = 0;
}