/****************************************************************************
 *  yx5200.c — заглушка (не тащим minimp3-декодер MP3 в bare-metal)
 *  YX5200-24SS audio player: аппаратный MP3-модуль, в нашем стеке нет звука.
 *  Оставляем только сигнатуры, чтобы md_cart.c линковался.
 ****************************************************************************/
#include "shared.h"
#include "yx5200.h"

void yx5200_init(int samplerate) { (void)samplerate; }
void yx5200_reset(void) {}
void yx5200_write(unsigned int rx_data) { (void)rx_data; }
void yx5200_update(unsigned int samples) { (void)samples; }
int yx5200_context_save(uint8 *state) { (void)state; return 0; }
int yx5200_context_load(uint8 *state) { (void)state; return 0; }