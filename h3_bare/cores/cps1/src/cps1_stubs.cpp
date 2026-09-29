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

// AY8910 (ay8910.h уже открыл extern "C")
// r0.370: Slap Fight и MISC-платы читают ВВОД через AY8910 port A/B
// (AY8910SetPorts(0, read_input0, read_input1, ...) — звуковой Z80 опрашивает
// кнопки и кладёт их в ShareRAM для main'а). Без этого main получает мусор и
// игра идёт «по пустому» (нет ввода/старта/врагов). Звук остаётся off
// (nBurnSoundRate=0): настоящий core при нулевой частоте даёт деление на 0,
// поэтому только эмуляция портов.
#define AY_STUBS_MAX 5
static read8_handler s_ay_pa[AY_STUBS_MAX] = { 0, 0, 0, 0, 0 };
static read8_handler s_ay_pb[AY_STUBS_MAX] = { 0, 0, 0, 0, 0 };
static UINT8 s_ay_reg[AY_STUBS_MAX] = { 0, 0, 0, 0, 0 };
// глобалы настоящего ay8910.c (fm.c на них ссылается в YM2203-совместимости)
INT32 ay8910_index_ym = 0;
INT32 ay8910burgertime_mode = 0;
INT16* pAY8910Buffer[(MAX_8910 + 1) * 3] = { 0 };

void AY8910Exit(INT32 chip) { (void)chip; }
INT32 AY8910InitYM(INT32 chip, INT32 clock, INT32 sample_rate,
	read8_handler portAread, read8_handler portBread,
	write8_handler portAwrite, write8_handler portBwrite,
	void (*update_callback)(void))
{ (void)chip; (void)clock; (void)sample_rate; (void)portAwrite; (void)portBwrite; (void)update_callback; return 0; }
void AY8910Reset(INT32 chip) { (void)chip; }
void AY8910Scan(INT32 nAction, INT32* pnMin) { (void)nAction; (void)pnMin; }
void AY8910_set_clock(INT32 chip, INT32 clock) { (void)chip; (void)clock; }
void AY8910Update(INT32 chip, INT16** buffer, INT32 length) { (void)chip; (void)buffer; (void)length; }
void AY8910Render(INT16* dest, INT32 length) { (void)dest; (void)length; }
void AY8910SetBuffered(INT32 (*pCPUCyclesCB)(), INT32 nCpuMHZ) { (void)pCPUCyclesCB; (void)nCpuMHZ; }
// slapfght (Toaplan, r0.338): 3-арг AY8910Init + сеттер портов/маршрутов.
// AY8910SetAllRoutes — МАКРОС (вызывает AY8910SetRoute по маршрутам).
extern "C" {
INT32 AY8910Init(INT32 chip, INT32 clock, INT32 add_signal) { (void)chip; (void)clock; (void)add_signal; return 0; }
INT32 AY8910SetPorts(INT32 chip, read8_handler portAread, read8_handler portBread,
	write8_handler portAwrite, write8_handler portBwrite)
{
	(void)portAwrite; (void)portBwrite;
	if (chip >= 0 && chip < AY_STUBS_MAX) { s_ay_pa[chip] = portAread; s_ay_pb[chip] = portBread; }
	return 0;
}
void AY8910SetRoute(INT32 chip, INT32 nIndex, double nVolume, INT32 nRouteDir)
{ (void)chip; (void)nIndex; (void)nVolume; (void)nRouteDir; }
// Чтение PSG: select-запись (a&1==0) выбирает регистр; читаются port A=B
// регистры 14/15 через колбэки драйвера (ввод/DIP). Иначе 0xFF (ничего).
void AY8910Write(INT32 chip, INT32 a, INT32 data)
{
	if (chip >= 0 && chip < AY_STUBS_MAX && (a & 1) == 0) s_ay_reg[chip] = (UINT8)(data & 0x0F);
}
INT32 AY8910Read(INT32 chip)
{
	// read8_handler в FBNeo: UINT8 (*)(UINT32 offset) — аргумент обязателен
	if (chip < 0 || chip >= AY_STUBS_MAX) return 0xFF;
	if (s_ay_reg[chip] == 14 && s_ay_pa[chip]) return s_ay_pa[chip](0);
	if (s_ay_reg[chip] == 15 && s_ay_pb[chip]) return s_ay_pb[chip](0);
	return 0xFF;
}
}

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

// YM2413/YM2612 — настоящие ядра (snd/ym2413.c, ym2612.c + burn_*.cpp,
// r0.383): стабов НЕТ, звук делается послойно.
UINT8 DebugSnd_YM2413Initted = 0;
UINT8 DebugSnd_YM2612Initted = 0;
UINT8 DebugSnd_DACInitted = 0;
UINT8 DebugSnd_SegaPCMInitted = 0;
UINT8 DebugSnd_UPD7759Initted = 0;
UINT8 DebugSnd_RF5C68Initted = 0;
UINT8 DebugDev_8255PPIInitted = 0;
UINT8 DebugCPU_I8039Initted = 0;
extern "C" void BurnMD2612UpdateRequest() {}

// ---- прочие запросы обновления (C++ linkage, как ожидают burn_sound.h) ----
void BurnYM2608UpdateRequest() {}
void BurnYM2610UpdateRequest() {}
void BurnYM2612UpdateRequest() {}