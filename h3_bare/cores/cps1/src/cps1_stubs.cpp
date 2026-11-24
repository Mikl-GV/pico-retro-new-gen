// cps1_stubs.cpp — host/OS glue для вендора CPS (входит в основную сборку как c1x_stubs.o).
// Звук на первом этапе off: чипы — no-op стабы (реальные ядра подключатся позже).
#include "burnint.h"
#include "ay8910.h"

// ---- Звук: no-op стабы (C-linkage, как ожидают FBNeo-обёртки) ----
extern "C" {
#include "fm.h"
#include "ymdeltat.h"


UINT8 YM_DELTAT_ADPCM_Read(YM_DELTAT* DELTAT) { (void)DELTAT; return 0; }
void YM_DELTAT_ADPCM_Write(YM_DELTAT* DELTAT, int r, int v) { (void)DELTAT; (void)r; (void)v; }
void YM_DELTAT_ADPCM_Reset(YM_DELTAT* DELTAT, int pan, int emulation_mode) { (void)DELTAT; (void)pan; (void)emulation_mode; }
void YM_DELTAT_ADPCM_CALC(YM_DELTAT* DELTAT) { (void)DELTAT; }
void YM_DELTAT_postload(YM_DELTAT* DELTAT, UINT8* regs) { (void)DELTAT; (void)regs; }
void YM_DELTAT_savestate(const char* statename, int num, YM_DELTAT* DELTAT) { (void)statename; (void)num; (void)DELTAT; }
}

// AY8910 (r1610): настоящее ядро snd/ay8910.c включено в собрку (PSG во всех
// аркадных наборах: CPS, Toaplan slapfght, Pac-Man, Galaga, SN76496-игры).
// Заглушки убраны: они перекрывали реальные AY8910Init/Write/Read, и порты
// ввода через PSG не работали бы. DebugSnd_AY8910Initted живёт в
// debug_track.cpp (туда же ушли остальные Debug*-флаги).
// Debug*-флаги, DebugTrackerExit, ProcessAnalog/AnalogDeadZone, nSocd —
// определены в debug_track.cpp; здесь их НЕ дублируем (linkcheck r1610).
clock_t clock() { return 0; }

// ---- OS/приложение ----
INT32 nInputIntfMouseDivider = 1;
bool bDrvOkay = false;
bool bDoIpsPatch = false;
UINT32 nIpsDrvDefine = 0;
UINT32 nIpsMemExpLen[SND2_ROM + 1];
RomDataInfo* pRDI = NULL;
BurnRomInfo* pDataRomDesc = NULL;
char szAppBlendPath[MAX_PATH] = "";
char szAppEEPROMPath[MAX_PATH] = "";
char szAppHiscorePath[MAX_PATH] = "";
char szAppSamplesPath[MAX_PATH] = "";

// MovieInfo — из FBNeo replay.cpp (не вендорен); символ нужен burn.cpp
struct MovieExtInfo { char d[256]; } MovieInfo;

void Reinitialise() {}
void IpsApplyPatches(UINT8* base, char* rom_name, UINT32 rom_crc, bool readonly)
{ (void)base; (void)rom_name; (void)rom_crc; (void)readonly; }

void TCHARToANSI(const char* in, char* out, int len) { (void)in; (void)len; if (out && len > 0) out[0] = 0; }

// ---- ZIP (реализуется в host zlib; стаб) ----
INT32 ZipLoadOneFile(char* zipname, const char* filename, void** dest, int* bufsize)
{ (void)zipname; (void)filename; (void)dest; (void)bufsize; return 0; }

// YM2413/YM2612 — настоящие ядра (snd/ym2413.c, ym2612.c + burn_*.cpp):
// r0.383): стабов НЕТ, звук делается послойно.
extern "C" void BurnMD2612UpdateRequest() {}

// ---- прочие запросы обновления (C++ linkage, как ожидают burn_sound.h) ----
void BurnYM2608UpdateRequest() {}
// r1627: YM2610 больше не стаб — реальный core в burn_ym2610.cpp.
void BurnYM2612UpdateRequest() {}
