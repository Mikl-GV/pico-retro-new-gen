#include "burnint.h"
#include "tms34010/tms34010.h"
#include "tms34010_intf.h"
#include "tms34010_fast.h"

static TMS34010State tms34010;
static pTMS34010ScanlineRender scanlineRenderCallback = NULL;

// r1666 (правка порта): карта 8 МБ статикой не влезает в .bss H3 (кончается
// на 0x44C00000, CPS ZIP_BUF_BIG). Держим в куче FBNeo и пересоздаём в
// TMS34010Init(): пул обнуляется в каждом BurnDrvInit.
TMS34010MemoryMap *g_tms_mmap = NULL;

static UINT16 default_read(UINT32 address) { return ~0; }
static void default_write(UINT32 address, UINT16 value) {}

// r1667 (правка порта): dword ядра — это uint32_t, а в arm-none-eabi newlib
// это unsigned long; UINT32 FBNeo — unsigned int. Типы не совпадают, поэтому
// shift-колбэки драйвера (void(*)(UINT32,void*)) заводим через свой trampoline.
static void (*g_shift_read)(UINT32 addr, void *dst) = NULL;
static void (*g_shift_write)(UINT32 addr, void *src) = NULL;

static void shift_read_tramp(tms::dword address, void *dst)
{
	if (g_shift_read) g_shift_read((UINT32)address, dst);
}

static void shift_write_tramp(tms::dword address, void *src)
{
	if (g_shift_write) g_shift_write((UINT32)address, src);
}


static UINT16 IO_read(UINT32 address) { return tms::read_ioreg(&tms34010,address); }
static void IO_write(UINT32 address, UINT16 value) { tms::write_ioreg(&tms34010, address, value); }

// cheat-engine hook-up
void TMS34010Open(INT32 num)
{
	// not used, single core.
}

void TMS34010Close()
{
	// not used, single core.
}

INT32 TMS34010GetActive()
{
	return 0; // cpu is always active
}

int TMS34010Idle(int cycles)
{
    return 0; // not impl.
}

void TMS34010WriteROM(UINT32 address, UINT8 value);
UINT8 TMS34010ReadByte(UINT32 address);
void TMS34010Exit();

INT32 TMS34010TotalCyclesi32()
{
	return TMS34010TotalCycles();
}

void TMS34010SetIRQLine(INT32 cpu, INT32 vector, INT32 status)
{
	if (status == CPU_IRQSTATUS_NONE) {
		TMS34010ClearIRQ(vector);
	}

	if (status == CPU_IRQSTATUS_ACK) {
		TMS34010GenerateIRQ(vector);
	}
}

// r1667 (правка порта): в cpu_core_config порта scan возвращает INT32, у ядра
// TMS34010Scan — void.
static INT32 TMS34010ScanConfig(INT32 nAction) { TMS34010Scan(nAction); return 0; }

cpu_core_config TMS34010Config =
{
	"TMS34010",
	TMS34010Open,
	TMS34010Close,
	TMS34010ReadByte,
	TMS34010WriteROM,
	TMS34010GetActive,
	TMS34010TotalCyclesi32,
	TMS34010NewFrame,
	TMS34010Idle,
	TMS34010SetIRQLine,
	TMS34010Run,
	TMS34010RunEnd,
	TMS34010Reset,
	TMS34010ScanConfig,
	TMS34010Exit,
	0x100000000ULL,
	0
};
// end cheat-engine hook-up

void TMS34010Init()
{
    g_tms_mmap = (TMS34010MemoryMap*)BurnMalloc(sizeof(TMS34010MemoryMap));
    TMS34010MapReset();

    tms34010.shift_read_cycle = shift_read_tramp;
    tms34010.shift_write_cycle = shift_write_tramp;

    // map IO registers
    TMS34010SetHandlers(TMS_MAXHANDLER-1, IO_read, IO_write);
	TMS34010MapHandler(TMS_MAXHANDLER-1, 0xc0000000, 0xc00001ff, MAP_READ | MAP_WRITE);

	CpuCheatRegister(0, &TMS34010Config);
}

int TMS34010Run(int cycles)
{
    return tms::run(&tms34010, cycles);
}

void TMS34010TimerSetCB(void (*timer_cb)())
{
	tms::timer_set_cb(&tms34010, timer_cb);
}

void TMS34010TimerSet(int cycles)
{
	tms::timer_arm(&tms34010, cycles);
}

INT64 TMS34010TotalCycles()
{
	return tms::total_cycles(&tms34010);
}

void TMS34010NewFrame()
{
	tms::new_frame(&tms34010);
}

void TMS34010RunEnd()
{
	tms::stop(&tms34010);
}

void TMS34010Scan(INT32 nAction)
{
	tms::scan(&tms34010, nAction);
}

UINT32 TMS34010GetPC()
{
	return tms::get_pc(&tms34010);
}

UINT32 TMS34010GetPPC()
{
	return tms::get_ppc(&tms34010);
}

void TMS34010Reset()
{
    tms::reset(&tms34010);
}

void TMS34010GenerateIRQ(UINT32 line)
{
    tms::generate_irq(&tms34010, line);
}

void TMS34010ClearIRQ(UINT32 line)
{
    tms::clear_irq(&tms34010, line);
}

void TMS34010SetScanlineRender(pTMS34010ScanlineRender sr)
{
    scanlineRenderCallback = sr;
}

// r1668 (правка порта): новое ядро отдаёт HEBLNK/HSBLNK в пиксель-клоках,
// а драйвер T-unit считает их пикселями — храним pixperclock драйвера.
int g_tms_pixperclock = 1;

void TMS34010SetPixClock(INT32 pxlclock, INT32 pix_per_clock)
{
    g_tms_pixperclock = pix_per_clock > 0 ? pix_per_clock : 1;
}

void TMS34010SetToShift(void (*reader)(UINT32 addr, void *dst))
{
    g_shift_read = reader;
}

void TMS34010SetFromShift(void (*writer)(UINT32 addr, void *src))
{
    g_shift_write = writer;
}

int TMS34010GenerateScanline(int line)
{
    return tms::generate_scanline(&tms34010, line, scanlineRenderCallback);
}

TMS34010State *TMS34010GetState()
{
    return &tms34010;
}

UINT8 TMS34010ReadByte(UINT32 address)
{
    return tms_read_byte(address);
}

UINT16 TMS34010ReadWord(UINT32 address)
{
    return tms_read_word(address);
}

void TMS34010WriteWord(UINT32 address, UINT16 value)
{
    tms_write_word(address, value);
}

void TMS34010WriteROM(UINT32 address, UINT8 value) // for cheat-engine
{
    UINT8 *pr = g_mmap.map[TMS_PAGE_WADD + TMS_PFN(address)];
    if ((uintptr_t)pr >= TMS_MAXHANDLER) {
        // address is bit-address
		tms_fast_write<UINT8>(pr,address, value);
    } else {
		g_mmap.write[(uintptr_t)pr](address, value);
	}

	pr = g_mmap.map[TMS_PFN(address)];
    if ((uintptr_t)pr >= TMS_MAXHANDLER) {
        // address is bit-address
		tms_fast_write<UINT8>(pr,address, value);
	}
}

void TMS34010MapReset()
{
    for (int page = 0; page < TMS_PAGE_COUNT; page++) {
        g_mmap.map[page] = NULL;
        g_mmap.map[page + TMS_PAGE_WADD] = NULL;
    }
    for (int handler = 0; handler < TMS_MAXHANDLER; handler++) {
        g_mmap.read[handler] = default_read;
        g_mmap.write[handler] = default_write;
    }
}

void TMS34010MapMemory(UINT8 *mem, UINT32 start, UINT32 end, UINT8 type)
{
    const int max_pages = (TMS_PFN(end) - TMS_PFN(start)) + 1;

    int page = TMS_PFN(start);
    for (int i = 0; i < max_pages; i++, page++) {

        if (type & MAP_READ)
            g_mmap.map[page] = mem + (TMS_PAGE_SIZE_8 * i);

        if (type & MAP_WRITE)
            g_mmap.map[page + TMS_PAGE_WADD] = mem + (TMS_PAGE_SIZE_8 * i);
    }
}

void TMS34010MapHandler(uintptr_t num, UINT32 start, UINT32 end, UINT8 type)
{
    const int max_pages = (TMS_PFN(end) - TMS_PFN(start)) + 1;

    int page = TMS_PFN(start);
    for (int i = 0; i < max_pages; i++, page++) {

        if (type & MAP_READ)
            g_mmap.map[page] = (UINT8*) num;

        if (type & MAP_WRITE)
            g_mmap.map[page + TMS_PAGE_WADD] = (UINT8*) num;
    }
}

int TMS34010SetReadHandler(UINT32 num, pTMS34010ReadHandler handler)
{
    if (num >= TMS_MAXHANDLER)
        return 1;
    g_mmap.read[num] = handler;
    return 0;
}

int TMS34010SetWriteHandler(UINT32 num, pTMS34010WriteHandler handler)
{
    if (num >= TMS_MAXHANDLER)
        return 1;
    g_mmap.write[num] = handler;
    return 0;
}

int TMS34010SetHandlers(UINT32 num, pTMS34010ReadHandler rhandler, pTMS34010WriteHandler whandler)
{
    if (num >= TMS_MAXHANDLER)
        return 1;
    g_mmap.read[num] = rhandler;
    g_mmap.write[num] = whandler;
    return 0;
}

