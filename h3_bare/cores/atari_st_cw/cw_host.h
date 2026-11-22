/* cw_host.h — host-слой Castaway для H3 (r1566)
 *
 * Ядро atari_st_cw (Castaway, GPLv2) не знает о железе: всё, что ему нужно
 * снаружи (память, кадр, звук, ввод, диски, ROM), реализуется здесь.
 */
#ifndef CW_HOST_H
#define CW_HOST_H

#include <stdint.h>

/* Вшитый EmuTOS (tos_data.S): mem.c копирует его вместо чтения файла. */
extern const unsigned char cw_tos_emutos[];
extern const unsigned char cw_tos_emutos_end[];

/* Диски через FAT (образы .st/.msa читаются из /roms/atarist). */
int  cw_disk_open(int idx, const char *path);      /* 0 = ok */
int  cw_disk_size(int idx);                         /* размер в байтах, -1 = нет диска */
int  cw_disk_read(int idx, uint32_t off, void *buf, uint32_t len);
int  cw_disk_write(int idx, uint32_t off, const void *buf, uint32_t len);

/* Диагностика (r1570): обращения ядра вне карты памяти. */
extern volatile uint32_t cw_unmapped_cnt, cw_unmapped_addr;

/* Точка входа: инициализация ядра и цикл кадров до выхода (ESC). */
void cw_run(const char *disk_path);

#endif /* CW_HOST_H */
