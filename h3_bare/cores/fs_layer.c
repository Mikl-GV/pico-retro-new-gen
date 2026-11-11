// fs_layer.c — ЕДИНЫЙ файловый слой прошивки (read-only, поверх памяти).
//
// r1013: вынесен из atari800/a8_fs.c (рефакторинг по требованию владельца:
// встройка C64 не должна «лезть» в файлы Atari 8-bit). Этот модуль предоставляет
// глобальные fopen/fread/stat/open/read/... — их через --allow-multiple-definition
// видят ВСЕ ядра (Atari 800, C64/VICE, и др.), которым нужен stdio поверх
// зарегистрированного буфера (ROM_BUF). a8_fs.c остаётся только тонким мостом
// (a8_fs_register → fs_register), чтобы не менять все вызовы в a8_host/c64_fs.
//
//   - fopen() отдаёт «файл» из зарегистрированного host-слоем буфера в памяти
//     (ROM_BUF), путь задаёт host при регистрации (fs_register/a8_fs_register);
//   - каталог (opendir/readdir) — пустой: вшитые OS-ROM не подменяем;
//   - запись/каталог-мутации (mkdir/rmdir/unlink/write) — заглушки (-1/0);
//   - r997: позиционный (fd) доступ (open/read/lseek) — VICE читает диски
//     через unbuffered VFS (RETRO_VFS_FILE_ACCESS_HINT_UNBUFFERED);
//   - r1011: setvbuf/setbuf — no-op (newlib писал в наш фейковый FILE*, затирая
//     слоты; «Cannot read image header»), слоты валидируются magic+диапазон .fs_bss.
//
// Данные слоя живут в NOLOAD-секции .fs_bss (linker.ld, 0x53E00000) — ядра
// (VICE) в неё не пишут; обнуляется startup.S.

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>   // r1227: bool для path_is_valid()
#include <string.h>
#include <stdio.h>   // FILE* (newlib), нам нужен только тип для сигнатур
#include <sys/stat.h>   // r994: struct stat/S_IFREG — stat() «существования»

#define FS_MAX_OPEN 16

#define FS_SLOT_MAGIC 0xA85A5A5Au   // r1011: метка валидного слота (used заменён

typedef struct {
    uint32_t magic;     // == FS_SLOT_MAGIC, иначе слот не наш/затёрт
    const uint8_t* data;
    long    size;
    long    pos;
    int     eof;
    int     err;
    int     dbg;    // r996: одноразовая диагностика первого чтения
} fs_mfile_t;

// Внешние метки .fs_bss (linker.ld) — для проверки диапазона слотов.
extern char _fs_bss_start[], _fs_bss_end[];

// r1224: диагностика «disk insert» — сколько раз и сколько байт реально читали.
unsigned g_fs_read_ops = 0;
unsigned g_fs_read_bytes = 0;
// r1229: сколько раз ядро открывало сам образ (ADF/ROM). Косвенно показывает,
// не переоткрывает ли оно диск на каждом кадре (смена диска ломает boot).
unsigned g_fs_rom_opens = 0;

static int slot_is_ours(const void* stream) {
    if (!stream) return 0;
    // Наши слоты в секции .fs_bss (0x53E00000..0x54A00000) — первая защита
    // от чужого указателя (RFILE* из heap-области).
    uintptr_t p = (uintptr_t)stream;
    if (p < (uintptr_t)&_fs_bss_start || p >= (uintptr_t)&_fs_bss_end) return 0;
    const fs_mfile_t* f = (const fs_mfile_t*)stream;
    return f->magic == FS_SLOT_MAGIC;
}

// r1010: файловый слой — в отдельной NOLOAD-секции .fs_bss (linker.ld,
// 0x53E00000), а НЕ в общем .bss. Секция обнуляется в startup.S.
#define FS_BSS __attribute__((section(".fs_bss")))
static fs_mfile_t g_files[FS_MAX_OPEN] FS_BSS;

// Один зарегистрированный «ROM-файл» в памяти. Host кладёт сюда буфер
// (например 0x50000000 после load_rom) и тот же путь задаёт в retro_game_info.
#define FS_ROM_PATH_MAX 128
static char     g_rom_path[FS_ROM_PATH_MAX] FS_BSS;
static const uint8_t* g_rom_data FS_BSS = 0;
static uint32_t g_rom_size FS_BSS = 0;

// Регистрация ROM-буфера под путём (вызывают a8_host, c64_host, ...).
// register_path — путь, который ядро будет открывать через fopen/open.
void fs_register(const char* path, const void* data, uint32_t size) {
    if (!path || !data) { g_rom_data = 0; g_rom_size = 0; g_rom_path[0] = 0; return; }
    size_t n = strlen(path);
    if (n >= FS_ROM_PATH_MAX) n = FS_ROM_PATH_MAX - 1;
    memcpy(g_rom_path, path, n);
    g_rom_path[n] = 0;
    g_rom_data = (const uint8_t*)data;
    g_rom_size = size;
    // r995: одна строка на запуск — видно, под каким путём зарегистрирован
    // образ и его размер (сверка с тем, что позже просит fopen ядра).
    printf("fs: reg '%s' size=%u ptr=0x%08X\n",
           g_rom_path, (unsigned)g_rom_size, (unsigned)(uintptr_t)data);
    // r1230: счётчики — на сессию (иначе в логе второго запуска сумма обеих).
    g_fs_read_ops = 0;
    g_fs_read_bytes = 0;
    g_fs_rom_opens = 0;
}

// r994: VICE собран с UNIX_COMPILE, поэтому archdep_expand_path() через
// archdep_getcwd() (наш getcwd = "/") превращает "game.d64" в "/game.d64"
// (а при пустом cwd — в "//game.d64"). Сравниваем пути без ведущих "/" и "./",
// иначе зарегистрированный образ не находится и автозапуск диска не идёт.
static const char* skip_path_prefix(const char* p) {
    for (;;) {
        if (*p == '/') { p++; continue; }
        if (p[0] == '.' && p[1] == '/') { p += 2; continue; }
        break;
    }
    return p;
}

static int path_matches(const char* path) {
    if (!path || !g_rom_path[0] || !g_rom_data) return 0;
    const char* a = skip_path_prefix(path);
    const char* b = skip_path_prefix(g_rom_path);
    // r1003: без учёта регистра — VICE может запросить путь, отличный по
    // регистру от зарегистрированного (расширение нормализуется в host'е).
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

// ---- fopen/fread/fseek/ftell/... (поверх зарегистрированного буфера) ----

// r994: диагностика промахов (только ошибки, до 10 строк) — по ней видно,
// какой путь просит ядро и какой зарегистрирован host'ом.
static int g_fs_miss_log FS_BSS = 0;
static int g_fs_ok_log FS_BSS = 0;

// r1225: второй файл слоя — вшитый Kickstart (kick34005.A500, 256 КБ).
// Ядро читает его через stat/fopen при старте, ADF монтируется позже, поэтому
// оба буфера должны быть доступны одновременно (один слот уже занят образом).
static char     g_bios_path[FS_ROM_PATH_MAX] FS_BSS;
static const uint8_t* g_bios_data FS_BSS = 0;
static uint32_t g_bios_size FS_BSS = 0;

void fs_register_bios(const char* path, const void* data, uint32_t size) {
    if (!path || !data) { g_bios_data = 0; g_bios_size = 0; g_bios_path[0] = 0; return; }
    size_t n = strlen(path);
    if (n >= FS_ROM_PATH_MAX) n = FS_ROM_PATH_MAX - 1;
    memcpy(g_bios_path, path, n);
    g_bios_path[n] = 0;
    g_bios_data = (const uint8_t*)data;
    g_bios_size = size;
    printf("fs: bios '%s' size=%u ptr=0x%08X\n",
           g_bios_path, (unsigned)g_bios_size, (unsigned)(uintptr_t)data);
}

// Те же правила сопоставления, что у path_matches (без регистра, без ведущих
// '/' и '.'), но для второго слота.
static int path_matches_bios(const char* path) {
    if (!path || !g_bios_path[0] || !g_bios_data) return 0;
    const char* a = skip_path_prefix(path);
    const char* b = skip_path_prefix(g_bios_path);
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

// Какой буфер отдаём по запрошенному пути: 1 — образ (ROM/диск), 2 — Kickstart.
static int fs_source(const char* path, const uint8_t** data, uint32_t* size) {
    if (path_matches(path)) {
        if (data) *data = g_rom_data;
        if (size) *size = g_rom_size;
        return 1;
    }
    if (path_matches_bios(path)) {
        if (data) *data = g_bios_data;
        if (size) *size = g_bios_size;
        return 2;
    }
    return 0;
}

FILE* fopen(const char* path, const char* mode) {
    (void)mode;
    const uint8_t* src = g_rom_data;
    uint32_t src_size = g_rom_size;
    int which = fs_source(path, &src, &src_size);
    if (!which) {
        if (g_fs_miss_log < 10) {
            g_fs_miss_log++;
            printf("fs: fopen miss '%s' (reg '%s')\n",
                   path ? path : "(null)", g_rom_path);
        }
        return NULL;
    }
    if (which == 1) g_fs_rom_opens++;   // r1229: сколько раз открывали образ
    for (int i = 0; i < FS_MAX_OPEN; i++) {
        if (g_files[i].magic == FS_SLOT_MAGIC) continue;
        g_files[i].magic = FS_SLOT_MAGIC;
        g_files[i].data = src;
        g_files[i].size = (long)src_size;
        g_files[i].pos  = 0;
        g_files[i].eof  = 0;
        g_files[i].err  = 0;
        g_files[i].dbg  = 1;   // r996: показать первое чтение этого открытия
        if (g_fs_ok_log < 40) {   // r1230: до 40 — считаем переоткрытия образа
            g_fs_ok_log++;
            printf("fs: open#%u kind=%d ok '%s' gsz=%ld ptr=%08X\n",
                   g_fs_ok_log, which, path, (long)src_size,
                   (unsigned)(uintptr_t)&g_files[i]);
        }
        return (FILE*)&g_files[i];
    }
    printf("fs: fopen '%s' — все %d слотов заняты (fclose не вызывался?)\n",
           g_rom_path, FS_MAX_OPEN);
    return NULL;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    g_fs_read_ops++;
    // r1011: слот обязан быть НАШИМ (в .fs_bss + magic). VICE-VFS передаёт сюда
    // stream->fp — если слот затёрт/чужой указатель (RFILE), не читаем мусор.
    if (!slot_is_ours(stream)) return 0;
    fs_mfile_t* f = (fs_mfile_t*)(void*)stream;
    // r1004/r1005: диагностика провала — «Cannot read image header» значит, что 0.
    static int rlog FS_BSS = 0;
    if (!f->magic) {
        if (rlog < 5) { rlog++; printf("fs: fread bad stream 0x%08X magic=%08X\n",
                                       (unsigned)(uintptr_t)stream, (unsigned)f->magic); }
        return 0;
    }
    if (size == 0 || nmemb == 0) return 0;
    long want = (long)(size * nmemb);
    if (want <= 0) return 0;
    if (f->pos >= f->size) {
        if (rlog < 8) {
            rlog++;
            printf("fs: fread EOF pos=%ld size=%ld ptr=%08X magic=%08X lr=%08X\n",
                   (long)f->pos, (long)f->size,
                   (unsigned)(uintptr_t)stream, (unsigned)f->magic,
                   (unsigned)(uintptr_t)__builtin_return_address(0));
        }
        f->eof = 1;
        return 0;
    }
    // r1005: любой вызов с нашим живым слотом виден (первые 12).
    if (rlog < 12) { rlog++; printf("fs: fread call pos=%ld size=%ld want=%ld ptr=%08X\n",
                                    (long)f->pos, (long)f->size, want,
                                    (unsigned)(uintptr_t)stream); }
    if (f->pos + want > f->size) { want = f->size - f->pos; }
    g_fs_read_bytes += (unsigned)want;   // r1230: считаем отданное, не запрошенное
    if (ptr) memcpy(ptr, f->data + f->pos, (size_t)want);
    // r996: первое чтение после open — что реально отдаём ядру.
    if (f->dbg) {
        f->dbg = 0;
        const uint8_t* d = f->data + f->pos;
        printf("fs: fread req=%ld got=%ld pos=%ld size=%ld b0=%02X %02X %02X %02X\n",
               (long)(size * nmemb), (long)want, (long)f->pos, (long)f->size,
               d[0], d[1], d[2], d[3]);
    }
    f->pos += want;
    if (f->pos >= f->size) f->eof = 1;
    return (size_t)(want / size);   // число элементов
}

int fclose(FILE* stream) {
    // r1011: закрываем только наши валидные слоты; чужой/затёртый указатель
    // (RFILE*) не трогаем и не обнуляем его память.
    if (!slot_is_ours(stream)) return EOF;
    fs_mfile_t* f = (fs_mfile_t*)(void*)stream;
    // r1007: трассировка — кто и когда закрывает наши слоты (первые 10).
    static int cllog FS_BSS = 0;
    if (cllog < 10) {
        cllog++;
        printf("fs: fclose ptr=%08X magic=%08X\n",
               (unsigned)(uintptr_t)stream, (unsigned)f->magic);
    }
    f->magic = 0; f->data = 0; f->size = 0; f->pos = 0;
    return 0;
}

int fseek(FILE* stream, long offset, int whence) {
    if (!slot_is_ours(stream)) return -1;
    fs_mfile_t* f = (fs_mfile_t*)(void*)stream;
    if (!f->magic) {
        static int slog FS_BSS = 0;
        if (slog < 5) { slog++; printf("fs: fseek bad stream 0x%08X magic=%08X\n",
                                       (unsigned)(uintptr_t)stream, (unsigned)f->magic); }
        return -1;
    }
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
    if (!slot_is_ours(stream)) return -1;
    fs_mfile_t* f = (fs_mfile_t*)(void*)stream;
    return f->pos;
}

void rewind(FILE* stream) {
    if (!slot_is_ours(stream)) return;
    fs_mfile_t* f = (fs_mfile_t*)(void*)stream;
    f->pos = 0; f->eof = 0; f->err = 0;
}

int (feof)(FILE* stream) {
    if (!slot_is_ours(stream)) return 0;
    fs_mfile_t* f = (fs_mfile_t*)(void*)stream;
    return f->eof ? 1 : 0;
}

int (ferror)(FILE* stream) {
    if (!slot_is_ours(stream)) return 0;
    fs_mfile_t* f = (fs_mfile_t*)(void*)stream;
    return f->err ? 1 : 0;
}

int fflush(FILE* stream) { (void)stream; return 0; }

// r1011: setvbuf/setbuf — no-op. VICE-VFS (libretro-common vfs_implementation.c,
// ветка #else) на каждый открытый файл вызывает
//   stream->buf = calloc(...); setvbuf(stream->fp, stream->buf, _IOFBF, 64K);
// Наш FILE* — это fs_mfile_t (32 байта), а НЕ настоящий newlib FILE. newlib
// setvbuf пишет в структуру FILE поля буфера/режима/размера → затирает
// magic/data/size/pos слота. Буферизация нашей виртуальной FS не нужна.
int setvbuf(FILE* restrict stream, char* restrict buf, int mode, size_t size) {
    (void)stream; (void)buf; (void)mode; (void)size;
    return 0;
}
void setbuf(FILE* restrict stream, char* restrict buf) {
    (void)stream; (void)buf;   // аналог setvbuf(_IONBF)
}

int fgetc(FILE* stream) {
    if (!slot_is_ours(stream)) return EOF;
    fs_mfile_t* f = (fs_mfile_t*)(void*)stream;
    if (f->pos >= f->size) { f->eof = 1; return EOF; }
    return (int)f->data[f->pos++];
}

char* fgets(char* s, int size, FILE* stream) {
    if (!s || size <= 0 || !slot_is_ours(stream)) return NULL;
    fs_mfile_t* f = (fs_mfile_t*)(void*)stream;
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

// r994: зарегистрированный образ должен выглядеть СУЩЕСТВУЮЩИМ файлом.
// Ядра проверяют контент через path_is_valid() → path_stat() → stat():
// раньше здесь всегда возвращалось -1, поэтому VICE (C64) считал путь
// невалидным (в libretro-core.c argv сбрасывался в "") и автозапуск диска
// отменялся — пользователь попадал в BASIC/READY. Теперь для пути, под
// которым зарегистрирован ROM-буфер, stat() отдаёт обычный файл с размером.
int stat(const char* path, struct stat* st) {
    uint32_t src_size = 0;
    if (!fs_source(path, 0, &src_size)) {
        // r1226: промах stat виден (ядро проверяет существование через
        // path_is_valid -> stat). Без этого «ROM not found» не отличить от
        // несовпадения пути; печатаем первые 8 промахов за сессию.
        static int slog FS_BSS = 0;
        if (slog < 8) {
            slog++;
            printf("fs: stat miss '%s' (reg '%s' bios '%s')\n",
                   path ? path : "(null)", g_rom_path, g_bios_path);
        }
        return -1;
    }
    if (st) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFREG | 0444;
        st->st_size = (off_t)src_size;
    }
    return 0;
}
int fstat(int fd, struct stat* st) { (void)fd; if (st) memset(st, 0, sizeof(*st)); return -1; }

// r1227: libretro-овский path_is_valid() — одна реализация на всю прошивку.
// Раньше её подменяла заглушка fceumm/libretro_compat.c (всегда 0), поэтому
// ядра считали несуществующим любой путь: PUAE — Kickstart и диск («insert
// disk»), остальные — свои образы. Семантика libretro: путь существует, если
// это файл (у нас — зарегистрированный буфер) ненулевого размера.
bool path_is_valid(const char* path) {
    struct stat st;
    return stat(path, &st) == 0 && st.st_size > 0;
}

// access(): зарегистрированный образ читается (F_OK/R_OK), но не пишется.
int access(const char* path, int mode) {
    if (!fs_source(path, 0, 0)) return -1;
    if (mode & 2 /* W_OK */) return -1;
    return 0;
}

// ---- r997: позиционный (fd) доступ к зарегистрированному образу ----
// VICE открывает образы двояко: buffered (fopen → наш слой) и unbuffered
// (retro_vfs_file_open_impl → open()+read()+lseek(), ветка RFILE_HINT_
// UNBUFFERED). Второй путь упирался в newlib-заглушки (open=-1), поэтому
// ядро писало «Tape: Cannot open file» / «not a valid file». Даём виртуальные
// дескрипторы: для зарегистрированного пути open() возвращает fd из своего
// диапазона, read/lseek/close работают по буферу g_rom_data.
#define FS_VFD_BASE 0x70000000
static long g_fd_pos[FS_MAX_OPEN] FS_BSS;
static int  g_fd_used[FS_MAX_OPEN] FS_BSS;
// r1225: дескриптор помнит свой буфер (образ или вшитый Kickstart) — слотов
// теперь два, и read/lseek не могут брать g_rom_* напрямую.
static const uint8_t* g_fd_data[FS_MAX_OPEN] FS_BSS;
static uint32_t g_fd_size[FS_MAX_OPEN] FS_BSS;
static int  g_fd_log FS_BSS = 0;   // r998: диагностика fd-пути (первые вызовы)

int open(const char* path, int flags, ...) {
    (void)flags;
    const uint8_t* src = 0;
    uint32_t src_size = 0;
    if (!fs_source(path, &src, &src_size)) {
        if (g_fd_log < 5) {
            g_fd_log++;
            printf("fs: fd open miss '%s' (reg '%s')\n",
                   path ? path : "(null)", g_rom_path);
        }
        return -1;
    }
    for (int i = 0; i < FS_MAX_OPEN; i++) {
        if (g_fd_used[i]) continue;
        g_fd_used[i] = 1;
        g_fd_pos[i]  = 0;
        g_fd_data[i] = src;
        g_fd_size[i] = src_size;
        if (g_fd_log < 10) {
            g_fd_log++;
            printf("fs: fd open ok '%s' fd=0x%08X\n", path,
                   (unsigned)(FS_VFD_BASE + i));
        }
        return FS_VFD_BASE + i;
    }
    printf("fs: fd open '%s' — все %d слотов заняты\n", g_rom_path, FS_MAX_OPEN);
    return -1;
}

int read(int fd, void* buf, size_t n) {
    g_fs_read_ops++; g_fs_read_bytes += (unsigned)n;
    int i = fd - FS_VFD_BASE;
    if (i < 0 || i >= FS_MAX_OPEN || !g_fd_used[i]) return -1;
    long pos = g_fd_pos[i];
    const uint8_t* src = g_fd_data[i];
    long src_size = (long)g_fd_size[i];
    if (pos >= src_size) return 0;
    long want = (long)n;
    if (pos + want > src_size) want = src_size - pos;
    if (want <= 0) return 0;
    if (buf) memcpy(buf, src + pos, (size_t)want);
    g_fd_pos[i] = pos + want;
    // r998: первое чтение по fd — видно, доходят ли данные до ядра.
    if (g_fd_log < 15) {
        g_fd_log++;
        const uint8_t* d = (const uint8_t*)buf;
        printf("fs: fd read fd=0x%08X req=%u got=%ld b0=%02X %02X %02X %02X\n",
               (unsigned)fd, (unsigned)n, want,
               d ? d[0] : 0, d ? d[1] : 0, d ? d[2] : 0, d ? d[3] : 0);
    }
    return (int)want;
}

off_t lseek(int fd, off_t off, int whence) {
    int i = fd - FS_VFD_BASE;
    if (i < 0 || i >= FS_MAX_OPEN || !g_fd_used[i]) return (off_t)-1;
    long base;
    if (whence == SEEK_SET)      base = 0;
    else if (whence == SEEK_CUR) base = g_fd_pos[i];
    else if (whence == SEEK_END) base = (long)g_fd_size[i];
    else return (off_t)-1;
    long np = base + (long)off;
    if (np < 0) np = 0;
    g_fd_pos[i] = np;
    return (off_t)np;
}

int close(int fd) {
    int i = fd - FS_VFD_BASE;
    if (i < 0 || i >= FS_MAX_OPEN) return -1;
    if (g_fd_used[i]) { g_fd_used[i] = 0; g_fd_pos[i] = 0; g_fd_data[i] = 0; g_fd_size[i] = 0; return 0; }
    return -1;
}

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