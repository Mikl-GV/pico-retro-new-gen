// neostubs.cpp — стабы NEOGEO/NeoCD для картриджных сетов.
// r1627: YM2610 больше не стаб — реальный core (burn_ym2610.cpp) собирается
// для Psikyo, и им же пользуется NEOGEO (стабы давали дубли символов).
#include "burnint.h"
#include "burn_ym2610.h"
#include "cd_interface.h"
#include "timer.h"

// ---- NeoCD: no-op (картриджные сеты) ----
TCHAR CDEmuImage[MAX_PATH] = "";
UINT8  CDEmuImageTOCSHA1[MAX_PATH] = { 0 };
CDEmuStatusValue CDEmuStatus = idle;

INT32 CDEmuInit(void) { return 1; }
INT32 CDEmuExit(void) { return 1; }
INT32 CDEmuStop(void) { return 1; }
INT32 CDEmuPlay(UINT8 M, UINT8 S, UINT8 F) { (void)M; (void)S; (void)F; return 1; }
INT32 CDEmuLoadSector(INT32 LBA, char* pBuffer) { (void)LBA; (void)pBuffer; return 1; }
INT32 CDEmuReadDataSector(INT32 nLba, UINT8* pBuffer) { (void)nLba; (void)pBuffer; return 1; }
UINT8* CDEmuReadTOC(INT32 track) { (void)track; return NULL; }
UINT8* CDEmuReadQChannel(void) { return NULL; }
INT32 CDEmuSetVolume(double v) { (void)v; return 1; }
INT32 CDEmuGetCurrentLBA(void) { return 0; }
INT32 CDEmuGetSoundBuffer(INT16* buffer, INT32 samples) { (void)buffer; (void)samples; return 1; }
INT32 CDEmuScan(INT32 nAction, INT32* pnMin) { (void)nAction; (void)pnMin; return 1; }
void NeoCDInfo_Exit(void) {}

// ---- прочие символы, на которые ссылается neo-код ----
// Toaplan (r0.330): отладочные флаги звука из burn_ym3812.cpp / ymz280b.cpp.
// NEC V25/V30 (r0.337): флаг инициализации из nec_intf.cpp (FBNEO_DEBUG).
// Toaplan r0.338: Z180 (ghox), M6805 (slapfght).
UINT8 DebugCPU_Z180Initted = 0;

// compute_resistor_weights НЕ стабим: реальная реализация в
// src/burn/devices/resnet.cpp (c1d_resnet.o) — дубль здесь давал бы
// зависимость от порядка линковки (см. --allow-multiple-definition).
