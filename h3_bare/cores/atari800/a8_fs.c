// a8_fs.c — тонкий мост A8-регистрации к ЕДИНОМУ файловому слою прошивки.
//
// r1013 (рефакторинг по требованию владельца): весь FILE-слой (fopen/fread/
// stat/open/read/... с проверкой magic и .fs_bss) вынесен в cores/fs_layer.c —
// он ОБЩИЙ для всех ядер (Atari 800, C64/VICE, ...), которые читают ROM через
// stdio поверх зарегистрированного буфера (ROM_BUF). Здесь остаётся только
// `a8_fs_register()` — его зовут a8_host.c и c64_fs.c; он делегирует в
// fs_register() (тот же единый буфер/путь). Никаких новых глобалов и FILE-API
// в a8-каталоге больше нет — встройка других систем не трогает файлы Atari.
#include <stdint.h>

// fs_layer.c: единый файловый слой прошивки (fopen/stat/open поверх
// зарегистрированного буфера). Регистрация — fs_register.
void fs_register(const char* path, const void* data, uint32_t size);

void a8_fs_register(const char* path, const void* data, uint32_t size) {
    fs_register(path, data, size);
}