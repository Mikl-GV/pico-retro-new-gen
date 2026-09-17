#ifndef GP_CHEATS_H
#define GP_CHEATS_H

#include <stdint.h>

// Декодирование GG/AR-кода: 1=GG8(ROM) 2=AR8(RAM) 3=GG16(MD ROM) 4=AR16(MD RAM) 0=не распознан
int gp_cheat_decode(const char* code, uint32_t* addr, uint16_t* data, uint8_t* compare);

// Сборка списка патчей из отмеченных читов (cheats_get/enabled)
void gp_cheats_compile(int is_md);

// Применить/снять (ROM-патчи + RAM раз в кадр)
void gp_cheats_apply(void);
void gp_cheats_clear(void);

// Host передаёт указатель на ROM и размер (чтобы патчить MD ROM напрямую)
void gp_cheats_set_rom(uint8_t* rom, uint32_t size);

// Хуки ядра (CHEATS_UPDATE / раз в кадр)
void ROMCheatUpdate(void);
void RAMCheatUpdate(void);

#endif