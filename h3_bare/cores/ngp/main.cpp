// main.cpp for bare-metal H3
#include "StdAfx.h"
#include "memory.h"
#include "tlcs900h.h"
#include "input.h"
#include "graphics.h"
#include "neopopsound.h"
#include "z80.h"

BOOL m_bIsActive;
int exitNow = 0;
EMUINFO m_emuInfo;
SYSTEMINFO m_sysInfo[NR_OF_SYSTEMS];

void mainemuinit()
{
	mem_init();
	graphics_init(NULL);
	tlcs_init();
	z80Init();
	tlcsMemWriteB(0x6F91,tlcsMemReadB(0x00200023));
	tlcsMemWriteB(0x00006F87,0x01);
	switch (tlcsMemReadW(0x00200020))
	{
		case 0x0059:
		case 0x0061:
			*get_address(0x0020001F) = 0xFF;
			break;
	}
	ngpSoundOff();
}

void SetActive(BOOL bActive)
{
	m_bIsActive = bActive;
}

void SetEmu(int machine)
{
	m_emuInfo.machine = machine;
	m_emuInfo.drv = &m_sysInfo[machine];
}

void initSysInfo()
{
	m_bIsActive = FALSE;
	m_emuInfo.machine = NGPC;
	m_emuInfo.drv = &m_sysInfo[m_emuInfo.machine];
	m_emuInfo.romSize = 0;
	strcpy(m_emuInfo.RomFileName, "");
#define NO_SOUND_OUTPUT
#ifdef NO_SOUND_OUTPUT
	m_emuInfo.sample_rate = 0;
#else
	m_emuInfo.sample_rate = 48000;
#endif
	m_emuInfo.stereo = 1;

	m_sysInfo[NGP].hSize = 160;
	m_sysInfo[NGP].vSize = 152;
	m_sysInfo[NGP].Ticks = 6*1024*1024;

	m_sysInfo[NGPC].hSize = 160;
	m_sysInfo[NGPC].vSize = 152;
	m_sysInfo[NGPC].Ticks = 6*1024*1024;
}

bool initRom()
{
	char *licenseInfo = (char *) " BY SNK CORPORATION";
	BOOL romFound = TRUE;
	int i, m;

	SetEmu(NGPC);
	SetActive(FALSE);

	for (i=0;i<19;i++)
	{
		if (mainrom[0x000009 + i] != licenseInfo[i])
			romFound = FALSE;
	}
	if (romFound)
	{
		i = mainrom[0x000023];
		if (i == 0x10 || i == 0x00)
		{
			if (i == 0x10) {
				m = NGPC;
			} else {
				if (mainrom[0x000020] == 0x34 && mainrom[0x000021] == 0x12)
					m = NGPC;
				else m = NGP;
			}

			SetEmu(m);
			mainemuinit();
			SetActive(TRUE);
			return TRUE;
		}
		return FALSE;
	}
	return FALSE;
}