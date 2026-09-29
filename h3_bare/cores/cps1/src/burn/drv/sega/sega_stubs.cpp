// sega_stubs.cpp — стабы периферии Sega-драйверов, которые не собраны
// (Hang-on/OutRun/Y-плата/X-плата используют общий sys16_run). r0.383.
// Звуковые ядра НЕ стабим — только шины/PPI несобранных игр.
// env: sys16_run.cpp (C++) — связка C++.
#include "burnint.h"

void HangonPPI0WritePortA(UINT8) {}
void HangonPPI0WritePortB(UINT8) {}
void HangonPPI0WritePortC(UINT8) {}
UINT8 HangonPPI0ReadPortC() { return 0; }
UINT8 HangonPPI1ReadPortC() { return 0; }
void HangonPPI1WritePortA(UINT8) {}
void Outrun2WriteWord(UINT32, UINT16) {}
UINT8 Outrun2ReadByte(UINT32) { return 0; }
void Outrun2WriteByte(UINT32, UINT8) {}
void OutrunPPI0WritePortA(UINT8) {}
void OutrunPPI0WritePortB(UINT8) {}
void OutrunPPI0WritePortC(UINT8) {}
UINT16 YBoardReadWord(UINT32) { return 0; }
void YBoardWriteWord(UINT32, UINT16) {}
UINT8 YBoardReadByte(UINT32) { return 0; }
void YBoardWriteByte(UINT32, UINT8) {}
UINT16 YBoard2ReadWord(UINT32) { return 0; }
void YBoard2WriteWord(UINT32, UINT16) {}
UINT16 YBoard3ReadWord(UINT32) { return 0; }
void YBoard3WriteWord(UINT32, UINT16) {}
UINT8 YBoard3ReadByte(UINT32) { return 0; }
UINT16 XBoardReadWord(UINT32) { return 0; }
void XBoardWriteWord(UINT32, UINT16) {}
UINT8 XBoardReadByte(UINT32) { return 0; }
void XBoardWriteByte(UINT32, UINT8) {}
// AUTOGEN r0.383: шинные хелперы несобранных плат (генератор из undefined)
void HamawayGfxBankWrite(unsigned int, unsigned short) {}
UINT8 Hangon_I8751ReadPort(int) { return 0; }
void Hangon_I8751WritePort(int, unsigned char) {}
UINT8 HangonReadByte(unsigned int) { return 0; }
UINT16 HangonReadWord(unsigned int) { return 0; }
void HangonWriteByte(unsigned int, unsigned char) {}
void HangonWriteWord(unsigned int, unsigned short) {}
UINT8 System16RoadControlRead(unsigned int) { return 0; }
void System16RoadControlWrite(unsigned int, unsigned short) {}
void System18GfxBankWrite(unsigned int, unsigned short) {}
void system18_io_chip_r(unsigned int) {}
void system18_io_chip_w(unsigned int, unsigned short) {}
UINT8 XBoard2ReadByte(unsigned int) { return 0; }
UINT16 XBoard2ReadWord(unsigned int) { return 0; }
void XBoard2WriteByte(unsigned int, unsigned char) {}
void XBoard2WriteWord(unsigned int, unsigned short) {}
float Pdrift_analog_target = 0;
