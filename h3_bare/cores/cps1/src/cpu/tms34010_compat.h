// r1666: слой совместимости драйверов Midway T-unit (midtunit/d_tunit) с
// новым ядром TMS34010 (src/cpu/tms34010 — переписанное MAME/FBNeo).
// Драйвер писался под старое ядро tms34 (удалено в r1667); здесь только те
// вызовы, которых в новом интерфейсе нет.
#ifndef TMS34010_COMPAT_H
#define TMS34010_COMPAT_H

#include "tms34010_intf.h"

void TMS34010Init(INT32 nCpu);                     // старое API — с номером CPU
void TMS34010Open(INT32 num);                      // есть в .cpp, но не в .h
void TMS34010Close();
void TMS34010Exit();                               // в новом intf отсутствует
void TMS34010SetPixClock(INT32 pxlclock, INT32 pix_per_clock);
void TMS34010SetCpuCyclesPerFrame(INT32 cpf);
void TMS34010SetToShift(void (*reader)(UINT32 addr, UINT16 *dst));
void TMS34010SetFromShift(void (*writer)(UINT32 addr, UINT16 *src));

#endif
