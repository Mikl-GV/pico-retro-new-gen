// FB Neo memory management module

// The purpose of this module is to offer replacement functions for standard C/C++ ones 
// that allocate and free memory.  This should help deal with the problem of memory
// leaks and non-null pointers on game exit.

#include "burnint.h"

#define LOG_MEMORY_USAGE    0

// Eh: H3-порт (r0.264): вместо newlib malloc (sbrk растёт без возврата и
// исчерпывался за несколько запусков эмулятора) — собственный bump-пул
// в BSS. BurnInitMemoryManager() вызывается из BurnDrvInit() при КАЖДОМ
// запуске игры, поэтому пул переиспользуется — повторные запуски чистые.
// max размер ROM+GFX+времён: wof — 8 МБ (rom 1М + gfx 4М + qsam 2М),
// CPS-2 — крупные сеты (ddsom: gfx ~28 МБ, PRG/decrypt +), поэтому 96 МБ.
#define LOG_MEMORY_USAGE    0
#define CPS1_MEM_POOL_SIZE  (96 * 1024 * 1024)

#define MAX_MEM_PTR	2048 // more than 1024 malloc calls should be insane... (fm.c needs a _lot_ for state-ing, increased to 2k)

static UINT8 *memptr[MAX_MEM_PTR]; // pointer to allocated memory
static INT32 memsize[MAX_MEM_PTR];
static INT32 mem_allocated;
static UINT8* s_pool = NULL;      // начало bump-пула (статический BSS)
static INT32 s_pool_used = 0;

// this should be called early on... BurnDrvInit?
void BurnInitMemoryManager()
{
	static UINT8 pool_mem[CPS1_MEM_POOL_SIZE];   // BSS — не занимает место в bin
	s_pool = pool_mem;
	s_pool_used = 0;
	memset (pool_mem, 0, sizeof(pool_mem));
	memset (memptr, 0, sizeof(memptr));
	memset (memsize, 0, sizeof(memsize));
	mem_allocated = 0;
}

// call BurnMalloc() instead of 'malloc' (see macro in burnint.h)
UINT8 *_BurnMalloc(INT32 size, char *file, INT32 line)
{
	INT32 sz = (size + 7) & ~7;   // выравнивание 8 для arm

	if (s_pool_used + sz > CPS1_MEM_POOL_SIZE) {
		bprintf (0, _T("BurnMalloc pool exhausted (%d + %d of %d)!\n"), s_pool_used, sz, CPS1_MEM_POOL_SIZE);
		return NULL;
	}

	for (INT32 i = 0; i < MAX_MEM_PTR; i++)
	{
		if (memptr[i] == NULL) {
			memptr[i] = s_pool + s_pool_used;
			s_pool_used += sz;
			memset (memptr[i], 0, sz); // set contents to 0

			mem_allocated += size; // важно: не учитывать выравнивание
			memsize[i] = size;

#if LOG_MEMORY_USAGE
			bprintf (0, _T("(%S:%d) BurnMalloc(%x): index %d.  %d total!\n"), file, line, size, i, mem_allocated);
#endif

			return memptr[i];
		}
	}

	bprintf (0, _T("BurnMalloc called too many times!\n"));

	return NULL; // Freak out!
}

UINT8 *BurnRealloc(void *ptr, INT32 size)
{
	UINT8 *mptr = (UINT8*)ptr;
	INT32 sz = (size + 7) & ~7;

	for (INT32 i = 0; i < MAX_MEM_PTR; i++)
	{
		if (memptr[i] == mptr) {
			// сжатие/без изменений — можно оставить как есть
			if (size <= memsize[i]) {
				mem_allocated -= memsize[i];
				mem_allocated += size;
				memsize[i] = size;
				return memptr[i];
			}
			// рост: новый блок в bump-пуле, старый перестаёт быть адресуемым
			if (s_pool_used + sz > CPS1_MEM_POOL_SIZE) {
				bprintf (0, _T("BurnRealloc pool exhausted!\n"));
				return NULL;
			}
			UINT8* nb = s_pool + s_pool_used;
			s_pool_used += sz;
			memcpy(nb, mptr, memsize[i]);
			memset(nb + memsize[i], 0, sz - memsize[i]);
			memptr[i] = nb;
			mem_allocated -= memsize[i];
			mem_allocated += size;
			memsize[i] = size;
			return nb;
		}
	}

	return NULL;
}

// call BurnFree() instead of "free" (see macro in burnint.h)
void _BurnFree(void *ptr)
{
	UINT8 *mptr = (UINT8*)ptr;

	for (INT32 i = 0; i < MAX_MEM_PTR; i++)
	{
		if (mptr != NULL && memptr[i] == mptr) {
			memptr[i] = NULL;   // bump: память переиспользуется при следующем BurnDrvInit

			mem_allocated -= memsize[i];
#if LOG_MEMORY_USAGE
			bprintf(0, _T("BurnFree(): index %d, size %x.  %d total!\n"), i, memsize[i], mem_allocated);
#endif
			memsize[i] = 0;
			break;
		}
	}
}

// Swap contents of src with dst
void BurnSwapMemBlock(UINT8 *src, UINT8 *dst, INT32 size)
{
	UINT8 *temp = BurnMalloc(size);

	memcpy(temp,	src,	size);
	memcpy(src,		dst,	size);
	memcpy(dst,		temp,	size);

	BurnFree(temp);
}


// call in BurnDrvExit?

void BurnExitMemoryManager()
{
	// bump-пул: ничего не освобождаем — BurnInitMemoryManager() при
	// следующем BurnDrvInit() обнулит пул и слоты.
	memset (memptr, 0, sizeof(memptr));
	memset (memsize, 0, sizeof(memsize));
	mem_allocated = 0;
}

UINT32 BurnRoundPowerOf2(UINT32 in)
{ // bonus feature: even rounds 0 up to 1! -dink
	unsigned int t = 1;
	while (in > t) {
		t <<= 1;
	}
	return t;
}
