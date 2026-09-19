// coleco_compat.c — стабы libstdc++/libc для ядра Gearcoleco (ColecoVision).
// Ядро Gearcoleco использует std::string/std::vector/ostream (SaveState,
// пути Cartridge, breakpoints Processor). В bare-metal без libc эти символы
// нужны, чтобы libstdc++/libgcc слинковались. Это заглушки: реальные
// деструкторы/исключения не используются (сборка с -fno-exceptions).
#include <stddef.h>
#include <stdint.h>

// abort() — libgcc unwind и libstdc++ ссылаются на него.
void abort(void) { for (;;) {} }

// __cxa_atexit — регистрация глобальных деструкторов. Заглушка:
// возвращаем 0 (успех), но ничего не регистрируем. Для bare-metal
// деструкторы global/static объектов не нужны (ядро живёт в одном
// цикле, память освобождается перезапуском/пулом).
int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
    (void)func; (void)arg; (void)dso;
    return 0;
}
void __cxa_finalize(void* dso) { (void)dso; }
// __cxa_pure_virtual уже определён в cxx_runtime.cpp

// __dso_handle — идентификатор DSO, нужен libstdc++ для atexit.
void* __dso_handle = (void*)0;

// __xpg_strerror_r — libstdc++ (system_error) использует; заглушка.
int __xpg_strerror_r(int errnum, char* buf, size_t buflen) {
    (void)errnum;
    if (buflen > 0) buf[0] = 0;
    return 0;
}

// getenv — libstdc++ (eh_alloc) может звать; заглушка NULL.
char* getenv(const char* name) { (void)name; return 0; }

// atexit — совместимость с libstdc++ (некоторые версии).
int atexit(void (*func)(void)) { (void)func; return 0; }

// ---- newlib syscall-стабы (libgloss/libc тянет при линковке с -lstdc++) ----
// _sbrk определён в libc_min.c, _gettimeofday — тоже. Остальные — заглушки:
// в bare-metal файлы/консоль/newlib-рантайм не используются, но линкеру
// нужны символы.
void _exit(int code) { (void)code; for (;;) {} }

int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, void* st) { (void)fd; (void)st; return -1; }
int _getentropy(void* buf, unsigned len) { (void)buf; (void)len; return -1; }
int _isatty(int fd) { (void)fd; return 0; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return -1; }
int _open(const char* path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
int _read(int fd, void* buf, unsigned len) { (void)fd; (void)buf; (void)len; return -1; }
int _write(int fd, const void* buf, unsigned len) { (void)fd; (void)buf; (void)len; return -1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid(void) { return 1; }

// ---- Gearcoleco: mcp_stdio_mode (использует Log_func в GearcolecoCore) ----
// В libretro-сборке это управление выводом MCP (AdamNet). Нам не нужно.
int g_mcp_stdio_mode = 0;