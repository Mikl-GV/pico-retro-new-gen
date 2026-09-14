#ifndef PICO_COMPAT_H
#define PICO_COMPAT_H

// Совместимость ядра InfoNES с платформами без Pico SDK (bare-metal H3).
// В оригинале код использует __not_in_flash_func() как подсказку
// размещения горячих функций в RAM (RP2040). На H3 / ARMv7 код и так
// исполняется из DDR, атрибут не нужен — объявляем пустым.

#ifndef __not_in_flash_func
#define __not_in_flash_func(x) x
#endif

#endif