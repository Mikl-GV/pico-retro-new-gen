// r1666: см. tms34010_compat.h — тонкий слой над новым ядром TMS34010.
#include "burnint.h"
#include "tms34010_compat.h"

// Новое ядро однопроцессорное (Open/Close — заглушки), Init без номера.
void TMS34010Init(INT32 nCpu)
{
	(void)nCpu;
	TMS34010Init();
}

// Состояние и карта живут внутри интерфейса; отдельного Exit в нём нет.
void TMS34010Exit()
{
}

// r1668: TMS34010SetPixClock живёт в tms34010_intf.cpp — он задаёт
// g_tms_pixperclock для перевода HEBLNK/HSBLNK в пиксели.

void TMS34010SetCpuCyclesPerFrame(INT32)
{
}

// Старое API ждало void(*)(UINT32, UINT16*), новое — void(*)(UINT32, void*).
void TMS34010SetToShift(void (*reader)(UINT32, UINT16 *))
{
	TMS34010SetToShift(reinterpret_cast<void (*)(UINT32, void *)>(reader));
}

void TMS34010SetFromShift(void (*writer)(UINT32, UINT16 *))
{
	TMS34010SetFromShift(reinterpret_cast<void (*)(UINT32, void *)>(writer));
}
