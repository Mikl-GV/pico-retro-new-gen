// cps1_stubs.cpp — host/OS glue для вендора CPS-1 (в основную сборку НЕ подключать).
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

// AY8910 (ay8910.h уже открыл extern "C")
void AY8910Exit(INT32 chip) { (void)chip; }
INT32 AY8910InitYM(INT32 chip, INT32 clock, INT32 sample_rate,
	read8_handler portAread, read8_handler portBread,
	write8_handler portAwrite, write8_handler portBwrite,
	void (*update_callback)(void))
{ (void)chip; (void)clock; (void)sample_rate; (void)portAread; (void)portBread; (void)portAwrite; (void)portBwrite; (void)update_callback; return 0; }
void AY8910Reset(INT32 chip) { (void)chip; }
void AY8910Scan(INT32 nAction, INT32* pnMin) { (void)nAction; (void)pnMin; }
void AY8910_set_clock(INT32 chip, INT32 clock) { (void)chip; (void)clock; }
void AY8910Write(INT32 chip, INT32 a, INT32 data) { (void)chip; (void)a; (void)data; }
INT32 AY8910Read(INT32 chip) { (void)chip; return 0; }
void AY8910Update(INT32 chip, INT16** buffer, INT32 length) { (void)chip; (void)buffer; (void)length; }

// ---- Debug-флаги (burn_debug/misc_debug в FBNeo; здесь UINT8) ----
UINT8 Debug_BurnGunInitted = 0;
UINT8 Debug_BurnLedInitted = 0;
UINT8 Debug_BurnShiftInitted = 0;
UINT8 Debug_BurnTransferInitted = 0;
UINT8 DebugCPU_SekInitted = 0;
UINT8 DebugCPU_ZetInitted = 0;
UINT8 DebugDev_EEPROMInitted = 0;
UINT8 DebugDev_TimeKprInitted = 0;
UINT8 Debug_GenericTilesInitted = 0;
UINT8 Debug_HiscoreInitted = 0;
UINT8 DebugSnd_AY8910Initted = 0;
UINT8 DebugSnd_MSM5205Initted = 0;
UINT8 DebugSnd_MSM6295Initted = 0;
UINT8 DebugSnd_SamplesInitted = 0;
UINT8 DebugSnd_YM2151Initted = 0;
UINT8 DebugSnd_YM2203Initted = 0;

// DebugTrackerExit — из burn_debug, не вендорен; no-op
void DebugTrackerExit() {}
clock_t clock() { return 0; }

// ---- OS/приложение ----
INT32 nInputIntfMouseDivider = 1;
INT32 nSocd[6] = { 0, 0, 0, 0, 0, 0 };
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

INT32 AnalogDeadZone(INT32 anaval, INT32 dz) { (void)anaval; (void)dz; return 0; }
UINT8 ProcessAnalog(INT16 anaval, INT32 reversed, INT32 flags, UINT8 scalemin, UINT8 scalemax)
{ (void)anaval; (void)reversed; (void)flags; (void)scalemin; (void)scalemax; return 0; }

void TCHARToANSI(const char* in, char* out, int len) { (void)in; (void)len; if (out && len > 0) out[0] = 0; }

// ---- ZIP (реализуется в host zlib; стаб) ----
INT32 ZipLoadOneFile(char* zipname, const char* filename, void** dest, int* bufsize)
{ (void)zipname; (void)filename; (void)dest; (void)bufsize; return 0; }

// ---- прочие запросы обновления (C++ linkage, как ожидают burn_sound.h) ----
void BurnYM2608UpdateRequest() {}
void BurnYM2610UpdateRequest() {}
void BurnYM2612UpdateRequest() {}