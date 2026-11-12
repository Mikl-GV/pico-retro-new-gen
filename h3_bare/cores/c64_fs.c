// c64_fs.c — регистрация ROM-буфера для VICE (Commodore 64/128).
//
// Файловый слой «fopen/fread/fseek поверх ROM_BUF» в прошивке ЕДИН на все
// ядра — с r1013 его предоставляет fs_layer.c (раньше a8_fs.c). Слой VICE тянет
// голые fopen/fread/... (через libretro-VFS filestream_*), и эти символы уже
// глобально определены в fs_layer.c. Поэтому здесь НЕ дублируем FILE-API
// (иначе linkcheck ловит «дубль: fopen/readdir/stat/...»), а только
// регистрируем ROM-буфер под путём, который увидит ядро — делегируем в
// a8_fs_register (тонкий мост к fs_register, тот же единый буфер ROM_BUF).
#include <stdint.h>

// a8_fs.c: мост к единому файловому слою (fs_register из fs_layer.c).
// Сигнатура совпадает с нашей регистрацией.
void a8_fs_register(const char* path, const void* data, uint32_t size);

void c64_fs_register(const char* path, const void* data, uint32_t size) {
    a8_fs_register(path, data, size);
}