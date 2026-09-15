// cxx_runtime.cpp — минимальный C++ runtime для bare-metal H3.
// operator new/delete поверх глобальных malloc/free (предоставляет
// gameboy_stubs.c). Сборка с -fno-exceptions -fno-rtti.
#include <stddef.h>

extern "C" void* malloc(size_t size);
extern "C" void free(void* ptr);

void* operator new(size_t size) { return malloc(size); }
void* operator new[](size_t size) { return malloc(size); }
void operator delete(void* p) noexcept { free(p); }
void operator delete[](void* p) noexcept { free(p); }
void operator delete(void* p, size_t) noexcept { free(p); }
void operator delete[](void* p, size_t) noexcept { free(p); }

extern "C" void __cxa_pure_virtual() { while (1) {} }