// input.cpp: implementation of the input class.
//
//////////////////////////////////////////////////////////////////////

#ifndef __GP32__
#include "StdAfx.h"
#endif
#include "main.h"
#include "input.h"
#include "memory.h"

// address where the state of the input device(s) is stored
unsigned char	ngpInputState = 0;
unsigned char	*InputByte = &ngpInputState;

BOOL InitInput(HWND hwnd)
{
	// input state is updated by ngp_host.cpp (UpdateInputState)
	return TRUE;
}

void FreeInput()
{
}