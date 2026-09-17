// gameboy_stubs.c — stubs для ядра binjgb на freestanding H3.
#include <stdint.h>
#include <stddef.h>

// _impure_ptr
struct _reent { int _errno; };
struct _reent _impure_ptr = { 0 };

// fprintf — заглушка (emulator.c: PRINT_ERROR использует fprintf)
int fprintf(void*, const char*, ...) { return 0; }

// пул-аллокатор
// Пул 12 МБ: Snes9x 2005 (~9.7 МБ), Handy (~1.5 МБ), binjgb (~0.5 МБ).
static uint8_t gb_heap[12 * 1024 * 1024];
static size_t gb_heap_pos = 0;

static void* gb_alloc(size_t sz) {
    sz = (sz + 3) & ~3;
    if (gb_heap_pos + sz > sizeof(gb_heap)) return 0;
    void* p = (void*)(gb_heap + gb_heap_pos);
    gb_heap_pos += sz;
    return p;
}

void gb_heap_reset(void) { gb_heap_pos = 0; }

void* malloc(size_t sz) { return gb_alloc(sz); }
void* calloc(size_t count, size_t sz) {
    size_t n = count * sz;
    void* p = gb_alloc(n);
    if (!p) return p;
    unsigned char* cp = (unsigned char*)p;
    for (size_t i = 0; i < n; i++) cp[i] = 0;
    return p;
}
void* realloc(void* p, size_t sz) {
    if (!p) return gb_alloc(sz);
    /* bump-аллокатор: копировать неоткуда, но данные лежат в пуле —
       перераспределение вниз по позиции не требуется для нашего использования */
    return gb_alloc(sz);
}
void free(void*) {}

void* memchr(const void* s, int c, size_t n) {
    const unsigned char* p = (const unsigned char*)s;
    while (n--) { if (*p == (unsigned char)c) return (void*)p; p++; }
    return 0;
}

char* strrchr(const char* s, int c) {
    const char* r = 0;
    while (*s) { if (*s == (char)c) r = s; s++; }
    return (char*)r;
}

void __assert_fail(const char*, const char*, int, const char*) { while (1) {} }

// file-функции (заглушки — host-слой не вызывает file_read/write, но линкеру нужно)
struct FileData { unsigned char* data; unsigned long size; };
void file_data_resize(struct FileData* fd, unsigned long new_sz) { fd->size = new_sz; }
void file_data_delete(struct FileData* fd) { fd->data = 0; fd->size = 0; }
int file_read(const char* name, struct FileData* out) { (void)name; (void)out; return 0; }
int file_write(const char* name, const struct FileData* fd) { (void)name; (void)fd; return 1; }
