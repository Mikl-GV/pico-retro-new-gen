// a8_fs.c — файловый слой for Atari 800 (read-only, поверх памяти).
//
// Ядро atari800/libretro завязано на stdio (fopen/fread/fseek) и НЕ принимает
// ROM буфером (info->data игнорируется). Здесь реализуем минимальный FILE-слой:
//   - fopen() отдаёт «файл» из зарегистрированного host-слоем буфера в памяти
//     (ROM_BUF), путь задаёт host при регистрации (a8_fs_register);
//   - каталог (opendir/readdir) — пустой: системные OS-ROM вшиты в ядро
//     (EMUOS_ALTIRRA, данные в SYSROM_roms[i].data), а сканирование каталога
//     в поисках ROM-файлов не должно подменять вшитые на файловые;
//   - запись/каталог-мутации (mkdir/rmdir/unlink/write) — заглушки (-1/0);
//   - строковые/прочие сиволы, которых нет в нашем freestanding-окружении, —
//     стабы.
//
// Имена fopen/fread/... перекрывают newlib (-lc): определение в объектном
#include <stdarg.h>
// файле выигрывает у библиотечной функции. Новый libc-путь (файлы на SD через
// _open/_read) НЕ используется — _open/_read остаются стаб-(-1) из
// coleco_compat.c. Диски/ленты (запись) пока не поддерживаются.

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>   // FILE* (newlib), нам нужен только тип для сигнатур

#define A8_MAX_OPEN 8

typedef struct {
    int     used;
    const uint8_t* data;
    long    size;
    long    pos;
    int     eof;
    int     err;
} a8_mfile_t;

static a8_mfile_t g_files[A8_MAX_OPEN];

// Один зарегистрированный «ROM-файл» в памяти. Host кладёт сюда буфер
// (например 0x50000000 после load_rom) и тот же путь задаёт в retro_game_info.
#define A8_ROM_PATH_MAX 128
static char     g_rom_path[A8_ROM_PATH_MAX];
static const uint8_t* g_rom_data = 0;
static uint32_t g_rom_size = 0;

// Регистрация ROM-буфера под путём (вызывает a8_host перед retro_load_game).
// register_path — путь, который ядро будет открывать через fopen.
void a8_fs_register(const char* path, const void* data, uint32_t size) {
    if (!path || !data) { g_rom_data = 0; g_rom_size = 0; g_rom_path[0] = 0; return; }
    size_t n = strlen(path);
    if (n >= A8_ROM_PATH_MAX) n = A8_ROM_PATH_MAX - 1;
    memcpy(g_rom_path, path, n);
    g_rom_path[n] = 0;
    g_rom_data = (const uint8_t*)data;
    g_rom_size = size;
}

static int path_matches(const char* path) {
    if (!path || !g_rom_path[0] || !g_rom_data) return 0;
    return strcmp(path, g_rom_path) == 0;
}

// ---- fopen/fread/fseek/ftell/... (поверх зарегистрированного буфера) ----

FILE* fopen(const char* path, const char* mode) {
    (void)mode;
    if (!path_matches(path)) return NULL;
    for (int i = 0; i < A8_MAX_OPEN; i++) {
        if (g_files[i].used) continue;
        g_files[i].used = 1;
        g_files[i].data = g_rom_data;
        g_files[i].size = (long)g_rom_size;
        g_files[i].pos  = 0;
        g_files[i].eof  = 0;
        g_files[i].err  = 0;
        return (FILE*)&g_files[i];
    }
    return NULL;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    a8_mfile_t* f = (a8_mfile_t*)(void*)stream;
    if (!f || !f->used) return 0;
    if (size == 0 || nmemb == 0) return 0;
    long want = (long)(size * nmemb);
    if (want <= 0) return 0;
    if (f->pos >= f->size) { f->eof = 1; return 0; }
    if (f->pos + want > f->size) { want = f->size - f->pos; }
    if (ptr) memcpy(ptr, f->data + f->pos, (size_t)want);
    f->pos += want;
    if (f->pos >= f->size) f->eof = 1;
    return (size_t)(want / size);   // число элементов
}

int fclose(FILE* stream) {
    a8_mfile_t* f = (a8_mfile_t*)(void*)stream;
    if (f && f->used) { f->used = 0; f->data = 0; f->size = 0; f->pos = 0; return 0; }
    return EOF;
}

int fseek(FILE* stream, long offset, int whence) {
    a8_mfile_t* f = (a8_mfile_t*)(void*)stream;
    if (!f || !f->used) return -1;
    long base = 0;
    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = f->pos;
    else if (whence == SEEK_END) base = f->size;
    else return -1;
    long np = base + offset;
    if (np < 0) np = 0;
    f->pos = np;
    f->eof = (np >= f->size);
    return 0;
}

long ftell(FILE* stream) {
    a8_mfile_t* f = (a8_mfile_t*)(void*)stream;
    return (f && f->used) ? f->pos : -1;
}

void rewind(FILE* stream) {
    a8_mfile_t* f = (a8_mfile_t*)(void*)stream;
    if (f && f->used) { f->pos = 0; f->eof = 0; f->err = 0; }
}

int (feof)(FILE* stream) {
    a8_mfile_t* f = (a8_mfile_t*)(void*)stream;
    return (f && f->used && f->eof) ? 1 : 0;
}

int (ferror)(FILE* stream) {
    a8_mfile_t* f = (a8_mfile_t*)(void*)stream;
    return (f && f->used && f->err) ? 1 : 0;
}

int fflush(FILE* stream) { (void)stream; return 0; }

int fgetc(FILE* stream) {
    a8_mfile_t* f = (a8_mfile_t*)(void*)stream;
    if (!f || !f->used) return EOF;
    if (f->pos >= f->size) { f->eof = 1; return EOF; }
    return (int)f->data[f->pos++];
}

char* fgets(char* s, int size, FILE* stream) {
    a8_mfile_t* f = (a8_mfile_t*)(void*)stream;
    if (!s || size <= 0 || !f || !f->used) return NULL;
    int i = 0;
    while (i < size - 1) {
        if (f->pos >= f->size) { f->eof = 1; break; }
        char c = (char)f->data[f->pos++];
        s[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
    s[i] = 0;
    return s;
}

// ---- запись (диски/ленты не поддерживаем): заглушки ----

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    (void)ptr; (void)size; (void)nmemb; (void)stream;
    return 0;
}

int fputc(int c, FILE* stream) { (void)c; (void)stream; return EOF; }
int fputs(const char* s, FILE* stream) { (void)s; (void)stream; return EOF; }
// fprintf/vfprintf: уже определены глобально (gameboy_stubs.c и newlib) —
// здесь не дублируем.

// ---- каталоги: пустые (вшитые OS-ROM не подменяем) ----

typedef void DIR;
typedef struct { int dummy; } dirent_placeholder;

DIR* opendir(const char* path) { (void)path; return (DIR*)0; }
int closedir(DIR* d) { (void)d; return -1; }
void* readdir(DIR* d) { (void)d; return NULL; }
// readdir возвращает struct dirent* — ядро atari800 читает d_name;
// так как opendir всегда NULL, readdir не вызовется. Заглушка-void* достаточна
// на уровне линковки (символ readdir с совместимой сигнатурой: возвращает
// указатель на dirent*, члены не читаются, т.к. NULL-каталог).

// ---- создание/удаление — не поддерживаем (mkdir уже в fuse_stubs.c) ----
int rmdir(const char* path) { (void)path; return -1; }
int unlink(const char* path) { (void)path; return -1; }
int stat(const char* path, void* st) { (void)path; (void)st; return -1; }
int fstat(int fd, void* st) { (void)fd; (void)st; return -1; }

// ---- прочие сиволы окружения ----

char* getcwd(char* buf, size_t size) {
    if (!buf || size < 2) return NULL;
    buf[0] = '/'; buf[1] = 0;
    return buf;
}

// getenv и mkstemp уже определены в прошивке (coleco_compat.c, fuse/src/libretro.c) —
// здесь их НЕ определяем, чтобы не плодить дубли.

int remove(const char* path) { (void)path; return -1; }
FILE* tmpfile(void) { return NULL; }
FILE* fdopen(int fd, const char* mode) { (void)fd; (void)mode; return NULL; }
int system(const char* cmd) { (void)cmd; return -1; }
void perror(const char* s) { (void)s; }
int fseeko(FILE* stream, off_t off, int whence) { return fseek(stream, (long)off, whence); }
off_t ftello(FILE* stream) { return (off_t)ftell(stream); }

// time/локаль — newlib даёт через _gettimeofday (наш стаб); стабы для
// недоступного:
void* setlocale(int category, const char* locale) { (void)category; (void)locale; return 0; }
void signal(int sig, void* hnd) { (void)sig; (void)hnd; }