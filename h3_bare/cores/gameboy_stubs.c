// gameboy_stubs.c — stubs для ядра binjgb на freestanding H3.
#include <stdint.h>
#include <stddef.h>

// _impure_ptr
struct _reent { int _errno; };
struct _reent _impure_ptr = { 0 };

// fprintf — заглушка (emulator.c: PRINT_ERROR использует fprintf)
int fprintf(void*, const char*, ...) { return 0; }

// пул-аллокатор
static uint8_t gb_heap[1024 * 1024];
static size_t gb_heap_pos = 0;

static void* gb_alloc(size_t sz) {
    sz = (sz + 3) & ~3;
    void* p = (void*)(gb_heap + gb_heap_pos);
    gb_heap_pos += sz;
    if (gb_heap_pos > sizeof(gb_heap)) { while (1) {} }
    return p;
}

void gb_heap_reset(void) { gb_heap_pos = 0; }

void* malloc(size_t sz) { return gb_alloc(sz); }
void* calloc(size_t count, size_t sz) {
    size_t n = count * sz;
    void* p = gb_alloc(n);
    unsigned char* cp = (unsigned char*)p;
    for (size_t i = 0; i < n; i++) cp[i] = 0;
    return p;
}
void* realloc(void* p, size_t sz) { (void)p; return gb_alloc(sz); }
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
int file_read(const char* name, struct FileData* out) { (void)name; (void)out; return 1; }
int file_write(const char* name, const struct FileData* fd) { (void)name; (void)fd; return 1; }
