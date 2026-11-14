// resampler44.h — общий полифазный ресемплер 44100→48000 для host-слоёв.
//
// r1326 (П.9 аудита): единая реализация вместо 6 локальных линейных
// ресемплеров (pce/cpc/a8/fuse/uae4all/bk/ms1504/ngp). Полифазная схема
// 64 фазы × 8 тапов (таблица rs44_poly_h вынесена сюда из gba_mgba_host.c —
// она проверена на железе в GBA-тракте). Стерео и моно-варианты с
// непрерывностью фазы между кадрами (tail = последняя пара прошлого кадра).
#ifndef RESAMPLER44_H
#define RESAMPLER44_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RS44_PHASES 64
#define RS44_TAPS   8
#define RS44_STEP   60211u      /* (44100<<16)/48000 */

/* Полифазные коэффициенты (shared: gba_mgba_host.c использует эту же таблицу). */
extern const int16_t rs44_poly_h[RS44_PHASES][RS44_TAPS];

/* Стерео 44100→48000. in/out — пары L,R. *tail[2] — последняя пара прошлого
 * кадра (непрерывность интерполяции между кадрами). Возвращает число
 * выходных пар (≤ max_out), пишет в out. Фаза *phase переносится дробной. */
uint32_t rs44fs_stereo(uint32_t* phase, int16_t* tail,
                       const int16_t* in, uint32_t in_n,
                       int16_t* out, uint32_t max_out);

/* Моно 44100→48000. in — моно-сэмплы; out — пары L,R (дупликация).
 * *tail — последний моно-сэмпл прошлого кадра. Возвращает число выходных пар. */
uint32_t rs44fs_mono(uint32_t* phase, int16_t* tail,
                     const int16_t* in, uint32_t in_n,
                     int16_t* out, uint32_t max_out);

#ifdef __cplusplus
}
#endif

#endif /* RESAMPLER44_H */