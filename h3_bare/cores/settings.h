#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

void settings_run(void);

// ---- глобальные настройки (правятся в Settings, читаются эмуляторами) ----
extern uint16_t emu_period_us;      // период кадра: 16667 мкс = 60 Гц (фикс, r158)
extern uint8_t  a2600_diff_expert;  // A2600 сложность: 0 = Novice, 1 = Expert

#endif