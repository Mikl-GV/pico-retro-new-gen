// r1669: inline-доступ к памяти TMS34010 для горячего пути интерпретатора.
// В эталоне это внешние TMS34010ReadWord/WriteWord: вызов функции на каждый
// доступ эмулятора. Тот же приём, что tms34_fast.h в r1663 (старое ядро).
// Карта одна на процесс — см. docs/MEMORY.md (карта живёт в куче FBNeo).
#ifndef TMS34010_FAST_H
#define TMS34010_FAST_H

#include "burnint.h"

#define TMS_ADDR_BITS   32
#define TMS_PAGE_SIZE   0x1000
#define TMS_PAGE_SIZE_8 (0x1000 >> 3)
#define TMS_PAGE_SHIFT  12
#define TMS_PAGE_MASK   0xFFF
#define TMS_PAGE_COUNT  (1 << (TMS_ADDR_BITS - TMS_PAGE_SHIFT))
#define TMS_PAGE_WADD   (TMS_PAGE_COUNT)
#define TMS_MAXHANDLER  32
#define TMS_PFN(x)      (((x) >> TMS_PAGE_SHIFT) & 0xFFFFF)
#define TMS_TOBYTE(a)   ((a) >> 3)

struct TMS34010MemoryMap
{
	UINT8 *map[TMS_PAGE_COUNT * 2];

	UINT16 (*read[TMS_MAXHANDLER])(UINT32);
	void (*write[TMS_MAXHANDLER])(UINT32, UINT16);
};

extern TMS34010MemoryMap *g_tms_mmap;
#define g_mmap (*g_tms_mmap)

template<typename T>
static inline T tms_fast_read(UINT8 *ptr, UINT32 adr)
{
	return *((T *)((UINT8 *)ptr + TMS_TOBYTE(adr & TMS_PAGE_MASK)));
}

template<typename T>
static inline void tms_fast_write(UINT8 *ptr, UINT32 adr, T value)
{
	T *p = ((T *)((UINT8 *)ptr + TMS_TOBYTE(adr & TMS_PAGE_MASK)));
	*p = value;
}

static inline UINT8 tms_read_byte(UINT32 address)
{
	UINT8 *pr = g_mmap.map[TMS_PFN(address)];
	if ((uintptr_t)pr >= TMS_MAXHANDLER)
		return tms_fast_read<UINT8>(pr, address);
	return g_mmap.read[(uintptr_t)pr](address) & 0xFF;
}

static inline UINT16 tms_read_word(UINT32 address)
{
	UINT8 *pr = g_mmap.map[TMS_PFN(address)];
	if ((uintptr_t)pr >= TMS_MAXHANDLER)
		return BURN_ENDIAN_SWAP_INT16(tms_fast_read<UINT16>(pr, address));
	return g_mmap.read[(uintptr_t)pr](address);
}

static inline void tms_write_word(UINT32 address, UINT16 value)
{
	UINT8 *pr = g_mmap.map[TMS_PAGE_WADD + TMS_PFN(address)];
	if ((uintptr_t)pr >= TMS_MAXHANDLER)
		tms_fast_write<UINT16>(pr, address, BURN_ENDIAN_SWAP_INT16(value));
	else
		g_mmap.write[(uintptr_t)pr](address, value);
}

// Для cheat-engine (TMS34010WriteROM): пишет байт и в rom-, и в ram-карту.
static inline void tms_write_byte(UINT32 address, UINT8 value)
{
	UINT8 *pr = g_mmap.map[TMS_PAGE_WADD + TMS_PFN(address)];
	if ((uintptr_t)pr >= TMS_MAXHANDLER)
		tms_fast_write<UINT8>(pr, address, value);
	else
		g_mmap.write[(uintptr_t)pr](address, value);

	pr = g_mmap.map[TMS_PFN(address)];
	if ((uintptr_t)pr >= TMS_MAXHANDLER)
		tms_fast_write<UINT8>(pr, address, value);
}

#endif
