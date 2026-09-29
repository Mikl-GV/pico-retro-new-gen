// neostubs.cpp — стабы NEOGEO/NeoCD/YM2610 для картриджных сетов (звук off).
// NEOGEO-картриджи не используют CD; YM2610 (звук) стабится как остальные.
#include "burnint.h"
#include "burn_ym2610.h"
#include "cd_interface.h"

// ---- YM2610: no-op ----
static void neo_ym_update(INT16* pBuf, INT32 nLen) { (void)pBuf; (void)nLen; }
void (*BurnYM2610Update)(INT16* pSoundBuf, INT32 nSegmentEnd) = neo_ym_update;

INT32 BurnYM2610Init(INT32 nClock, UINT8* ROMA, INT32* nASize, UINT8* ROMB,
                     INT32* nBSize, FM_IRQHANDLER irq, INT32 add)
{ (void)nClock; (void)ROMA; (void)nASize; (void)ROMB; (void)nBSize; (void)irq; (void)add; return 0; }

INT32 BurnYM2610Init(INT32 nClock, UINT8* ROMA, INT32* nASize, UINT8* ROMB,
                     INT32* nBSize, FM_IRQHANDLER irq,
                     INT32 (*Stream)(INT32), double (*GetTime)(), INT32 add)
{ (void)nClock; (void)ROMA; (void)nASize; (void)ROMB; (void)nBSize; (void)irq;
  (void)Stream; (void)GetTime; (void)add; return 0; }

void BurnYM2610Reset(void) {}
void BurnYM2610Exit(void) {}
void BurnYM2610Scan(INT32 nAction, INT32* pnMin) { (void)nAction; (void)pnMin; }
void BurnYM2610SetRoute(INT32 nIndex, double nVolume, INT32 nRouteDir)
{ (void)nIndex; (void)nVolume; (void)nRouteDir; }
void BurnYM2610MapADPCMROM(UINT8* a, INT32 sA, UINT8* b, INT32 sB)
{ (void)a; (void)sA; (void)b; (void)sB; }

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
UINT8 DebugSnd_YM2610Initted = 0;

double compute_resistor_weights(INT32 minval, INT32 maxval, double scaler,
    INT32 count_1, const INT32* resistances_1, double* weights_1, INT32 pulldown_1, INT32 pullup_1,
    INT32 count_2, const INT32* resistances_2, double* weights_2, INT32 pulldown_2, INT32 pullup_2,
    INT32 count_3, const INT32* resistances_3, double* weights_3, INT32 pulldown_3, INT32 pullup_3)
{
    (void)minval; (void)maxval; (void)scaler;
    (void)count_1; (void)resistances_1; (void)weights_1; (void)pulldown_1; (void)pullup_1;
    (void)count_2; (void)resistances_2; (void)weights_2; (void)pulldown_2; (void)pullup_2;
    (void)count_3; (void)resistances_3; (void)weights_3; (void)pulldown_3; (void)pullup_3;
    return 0.0;
}