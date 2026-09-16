// snes_compat.c — минимальные стабы для Snes9x 2005
// Всё, что уже есть в fceumm_libretro_compat.c, опускаем.
// Нужен только qsort (clip.c) — newlib-шный тянет __aeabi_uidiv и _sbrk.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static void swap(char* a, char* b, size_t esize) {
    char tmp[16];
    size_t n = esize;
    while (n > sizeof(tmp)) {
        memcpy(tmp, a, sizeof(tmp));
        memcpy(a, b, sizeof(tmp));
        memcpy(b, tmp, sizeof(tmp));
        a += sizeof(tmp); b += sizeof(tmp); n -= sizeof(tmp);
    }
    memcpy(tmp, a, n);
    memcpy(a, b, n);
    memcpy(b, tmp, n);
}

void qsort(void* base, size_t nmemb, size_t size,
           int (*cmp)(const void*, const void*)) {
    if (nmemb < 2) return;
    char* b = (char*)base;
    for (size_t i = 1; i < nmemb; i++) {
        size_t j = i;
        while (j > 0 && cmp(b + j * size, b + (j-1) * size) < 0) {
            swap(b + j * size, b + (j-1) * size, size);
            j--;
        }
    }
}