// gba_compat.c — bare-metal стабы для gpSP (GBA).
// filestream/sscanf уже есть в fceumm/libretro_compat.c — не дублируем.
// ROM подаётся из памяти (g_ram_rom), файловой системы нет.
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

// ---- время: RTC не нужен, даём фиксированное ----
time_t time(time_t* t) {
    static const time_t fixed = 1700000000;
    if (t) *t = fixed;
    return fixed;
}

static struct tm g_tm;

struct tm* localtime(const time_t* t) {
    (void)t;
    memset(&g_tm, 0, sizeof(g_tm));
    g_tm.tm_year = 123;
    g_tm.tm_mon = 10;
    g_tm.tm_mday = 14;
    return &g_tm;
}

// ---- netplay/serial — стабы (в gpSP не используем) ----
uint32_t netplay_num_clients = 0;
uint32_t netplay_client_id = 0;
void netpacket_send(int flags, const void* buf, size_t len, uint16_t client_id)
    { (void)flags; (void)buf; (void)len; (void)client_id; }
void netpacket_poll_receive(void) {}

// ---- savestate-стабы для input (gpSP не сохраняет/грузит) ----
bool input_check_savestate(const uint8_t* src) { (void)src; return 0; }
bool input_read_savestate(const uint8_t* src) { (void)src; return 0; }
unsigned input_write_savestate(uint8_t* dst) { (void)dst; return 0; }