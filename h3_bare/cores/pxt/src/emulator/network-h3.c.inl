#pragma once
/*
 * network-h3.c.inl — H3-заглушка сетевого редиректора (INT 2Fh LAN redirector).
 *
 * Оригинал в pico-286: network-redirector.c.inl — перенаправление сетевых
 * дисков DOS через FATFS-поиск файлов на хост-FS (win32/findfirst). На H3
 * сетевых дисков нет, FATFS-поиск не нужен. CPU при неизвестном AH на
 * INT 2Fh вызывает redirector_handler(): на H3 всегда возвращаем false,
 * чтобы INT 2Fh обрабатывался далее обычным путём (не наш сервис).
 *
 * Включается из cpu.c (#elif defined(H3_BARE_METAL)).
 */
static inline bool redirector_handler(void) {
    return false;
}