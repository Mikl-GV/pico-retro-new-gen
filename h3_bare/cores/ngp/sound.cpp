//---------------------------------------------------------------------------
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version. See also the license.txt file for
//	additional informations.
//---------------------------------------------------------------------------

// sound.cpp for bare-metal H3 — NGP sound system (fast per-sample + band-limited Blip).
// Aligned with libretro/race sound.c: soundStep drives the band-limited path when
// neopop_audio_accurate=1; soundOutput flushes Blip-synthesised samples to I2S.

#include "StdAfx.h"
#include "main.h"
#include "sound.h"
#include "memory.h"

#include "z80.h"

#ifdef DRZ80
#include "DrZ80_support.h"
#else
#include "z80.h"
#endif

#include "neopopsound.h"
#include "neopop_blip.h"

int sndCycles = 0;

/* Runtime selector: 1 = band-limited (accurate) Blip path, 0 = fast per-sample.
 * Эталон (libretro/race) по умолчанию использует FAST (neopop_audio_accurate=0,
 * core option race_audio_quality default 'fast'); blip — опциональный accurate.
 * На этом железе (H3, I2S 48000) fast-путь даёт меньше алиасинга/шума —
 * он использует DC-блокер + аддитивный DAC без blip-свёртки. Включаем fast. */
int neopop_audio_accurate = 0;

/* Re-derive the Blip synth parameters from the live chip register state, then
 * advance the band-limited synth by 'cycles' chip cycles. Keeping the Blip path
 * a pure observer of toneChip/noiseChip avoids duplicating the register decode. */
static void neopop_blip_sync_from_chips(void)
{
   int c;
   for (c = 0; c < 3; c++)
      neopop_blip_sync_tone(c,
            neopop_sound_tone_divider(c),
            neopop_sound_tone_volume(c));

   neopop_blip_sync_noise(
         neopop_sound_noise_divider(),
         neopop_sound_noise_volume(),
         neopop_sound_noise_feedback_periodic());
}

void soundStep(int cycles)
{
   sndCycles += cycles;

   if (neopop_audio_accurate)
   {
      neopop_blip_sync_from_chips();
      neopop_blip_run(cycles);
   }
}

/* Sound output: flush band-limited samples to the output callback. The core
 * emulates at 44100 Hz (NGP chip clock), we re-sample to I2S 48000 in the host.
 * This function is called once per frame from ngp_run_frame(). */
void soundOutput(void);

unsigned int ngpRunning;

void ngpSoundStart(void)
{
   ngpRunning = 1;
#ifdef DRZ80
   Z80_Reset();
#else
   z80Init();
   z80SetRunning(1);
#endif
}

/* Execute all gained cycles (divided by 2) */
void ngpSoundExecute(void)
{
#ifdef DRZ80
   int toRun = sndCycles/2;
   if (ngpRunning)
      Z80_Execute(toRun);
   sndCycles -= toRun;
#else
   int elapsed;
   while (sndCycles > 0)
   {
      elapsed = z80Step();
      // r630: защита от зависания. z80Step() возвращает тайминг инструкции, но
      // неопознанная опкода (z80Udef) возвращает 0 → sndCycles не уменьшается →
      // бесконечный цикл = «зависание процессора целиком» (проявляется, когда
      // Z80 доходит до неопознанной инструкции ~3-я минута). Приписываем min 1.
      if (elapsed < 1) elapsed = 1;
      sndCycles -= (2 * elapsed);
   }
#endif
}

/* Switch sound system off */
void ngpSoundOff(void)
{
   ngpRunning = 0;
#ifdef DRZ80
#else
   z80SetRunning(0);
#endif
}

/* Generate interrupt to the NGP sound system */
void ngpSoundInterrupt(void)
{
   if (ngpRunning)
   {
#ifdef DRZ80
      Z80_Cause_Interrupt(0x100);
#else
      z80Interrupt(0);
#endif
   }
}