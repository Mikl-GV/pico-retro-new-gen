// cpc_fs.c — файловый слой для Caprice32 (Amstrad CPC) поверх памяти.
//
// Ядро cap32/libretro завязано на file_stream (RFILE): макросы
// file_stream_transforms.h переименовывают fopen→rfopen, FILE→RFILE.
// Хост подаёт ROM буфером в памяти (ROM_BUF) через cpc_fs_register(), а путь
// задаёт в retro_game_info. Системные ROM/BIOS вшиты в ядро (rom/*.h) — здесь
// файловая подсистема нужна только для загрузки контента (SNA/DSK/TAP/CPR).
//
// Каталог не поддерживается (сканирование не используется). Запись/другие
// мутации — стабы. Имена rf*/filestream_* перекрывают libretro-common (vfs)
// и newlib. DISK-слой (мультидисковые образы через rfopen по пути) работает
// на том же единственном зарегистрированном буфере.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

// RFILE — opaque-тип libretro-common. Здесь НЕ подключаем чужие заголовки
// (gpgx/libretro_inc в общих -I даёт другую сигнатуру rfgets) — объявляем
// собственный совместимый тип (cap32-вендор компилируется с его libretro.h).
typedef struct cpc_rfile RFILE;
struct cpc_rfile {
    int used;
    const uint8_t* data;
    int64_t size;
    int64_t pos;
    int eof;
};

#define CPC_MAX_OPEN 8

static struct cpc_rfile g_files[CPC_MAX_OPEN];

// ---- один зарегистрированный "ROM-файл" в памяти ----
#define CPC_ROM_PATH_MAX 160
static char            g_rom_path[CPC_ROM_PATH_MAX];
static const uint8_t*  g_rom_data = 0;
static uint32_t        g_rom_size = 0;

void cpc_fs_register(const char* path, const void* data, uint32_t size) {
    // r968: новая сессия — закрыть ВСЕ ранее открытые «файлы». Иначе слоты
    // CPC_MAX_OPEN утекали между запусками и rfopen возвращал NULL: ядро
    // молча оставалось на встроенном BASIC вместо выбранного диска/снапшота.
    for (int i = 0; i < CPC_MAX_OPEN; i++) {
        g_files[i].used = 0;
        g_files[i].data = 0;
        g_files[i].size = 0;
        g_files[i].pos  = 0;
        g_files[i].eof  = 0;
    }
    if (!path || !data || size == 0) {
        g_rom_data = 0; g_rom_size = 0; g_rom_path[0] = 0;
        return;
    }
    size_t n = strlen(path);
    if (n >= CPC_ROM_PATH_MAX) n = CPC_ROM_PATH_MAX - 1;
    memcpy(g_rom_path, path, n);
    g_rom_path[n] = 0;
    g_rom_data = (const uint8_t*)data;
    g_rom_size = size;
}

static int path_matches(const char* path) {
    if (!path || !g_rom_path[0] || !g_rom_data) return 0;
    if (strcmp(path, g_rom_path) == 0) return 1;
    // cap32 зовёт название файла как есть (rfopen(retro_content_filepath));
    // доп. допуск по базовому имени на случай добавленных путей.
    const char* slash = strrchr(path, '/');
    const char* base  = slash ? slash + 1 : path;
    return strcmp(base, g_rom_path) == 0;
}

// ---- RFILE API (обёртка над зарегистрированным буфером) ----

// Примечание: cpc_rename.sh префиксует ВСЕ глобальные символы объектов ядра
// (включая rf*/filestream_*) в cpc_*. Здесь определения под теми же cpc_*
// именами — они резолвят переименованные ссылки ядра и не конфликтуют с
// gpgx_missing.c/pce_stubs.c (свои rf*/*_vfs_init).
RFILE* cpc_rfopen(const char* path, const char* mode) {
    (void)mode;
    if (!path_matches(path)) {
        // r968: ядро спрашивает файл, которого мы не регистрировали — без
        // этого лога причина «остался BASIC вместо диска» невидима.
        extern int uart0_printf(const char* fmt, ...);
        uart0_printf("  cpc: rfopen miss '%s' (registered '%s', %u B)\r\n",
                     path ? path : "(null)", g_rom_path, (unsigned)g_rom_size);
        return NULL;
    }
    for (int i = 0; i < CPC_MAX_OPEN; i++) {
        if (g_files[i].used) continue;
        g_files[i].used = 1;
        g_files[i].data = g_rom_data;
        g_files[i].size = (int64_t)g_rom_size;
        g_files[i].pos  = 0;
        g_files[i].eof  = 0;
        return &g_files[i];
    }
    return NULL;
}

int cpc_rfclose(RFILE* stream) {
    if (stream) { stream->used = 0; stream->data = 0; stream->size = 0; stream->pos = 0; stream->eof = 0; }
    return 0;
}

int64_t cpc_rfread(void* buffer, size_t size, size_t count, RFILE* stream) {
    if (!stream || !stream->used || size == 0 || count == 0) return 0;
    int64_t want = (int64_t)(size * count);
    if (stream->pos >= stream->size) { stream->eof = 1; return 0; }
    if (stream->pos + want > stream->size)
        want = stream->size - stream->pos;
    if (want > 0 && buffer) memcpy(buffer, stream->data + stream->pos, (size_t)want);
    stream->pos += want;
    if (stream->pos >= stream->size) stream->eof = 1;
    return want / (int64_t)(size ? size : 1);
}

int64_t cpc_rfwrite(const void* buffer, size_t size, size_t count, RFILE* stream) {
    (void)buffer; (void)size; (void)count; (void)stream;
    return 0;   // запись не поддерживается
}

int64_t cpc_rfseek(RFILE* stream, int64_t offset, int origin) {
    if (!stream || !stream->used) return -1;
    int64_t base;
    switch (origin) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = stream->pos; break;
        case SEEK_END: base = stream->size; break;
        default: return -1;
    }
    int64_t np = base + offset;
    if (np < 0) np = 0;
    if (np > stream->size) np = stream->size;
    stream->pos = np;
    stream->eof = 0;
    return np;
}

int64_t cpc_rftell(RFILE* stream) {
    return (stream && stream->used) ? stream->pos : -1;
}

char* cpc_rfgets(char* buffer, int maxCount, RFILE* stream) {
    if (!stream || !stream->used || !buffer || maxCount <= 0) return NULL;
    int i = 0;
    while (i < maxCount - 1 && stream->pos < stream->size) {
        char c = (char)stream->data[stream->pos++];
        buffer[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) { buffer[0] = 0; return NULL; }
    buffer[i] = 0;
    if (stream->pos >= stream->size) stream->eof = 1;
    return buffer;
}

int64_t cpc_filestream_get_size(RFILE* stream) {
    return (stream && stream->used) ? stream->size : 0;
}

// ---- стабы, которые могут понадобиться линковке ----
// rfputc/rfeof/rferror в наборе cap32 встречаются ТОЛЬКО как ссылки (не были
// переименованы rename-скриптом) — даём их голыми. Дублей у других ядер нет.
int64_t rfflush(RFILE* stream) { (void)stream; return 0; }
int     rferror(RFILE* stream) { (void)stream; return 0; }
int     rfeof(RFILE* stream) { return (stream && stream->used && stream->eof) ? 1 : 0; }
int     rfputc(int character, RFILE* stream) { (void)character; (void)stream; return 0; }
int     rfprintf(RFILE* stream, const char* format, ...) { (void)stream; (void)format; return 0; }
int     rfscanf(RFILE* stream, const char* format, ...) { (void)stream; (void)format; return -1; }
int     rfputs(const char* str, RFILE* stream) { (void)str; (void)stream; return 0; }

// VFS-инициализация (обязательна: libretro-core.c зовёт их при снятии
// RETRO_ENVIRONMENT_GET_VFS_INTERFACE). Файловый доступ у нас — через cpc_fs,
// VFS-интерфейс не используется, стабы. dirent_vfs_init нигде больше не
// определён — даём голым (в наборе кап32 он только ссылка).
void dirent_vfs_init(const void* iface) { (void)iface; }