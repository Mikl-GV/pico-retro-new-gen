/* wARM -> bare-metal для H3 (Cortex-A7).
 * Замена Linux-драйвера wARM: cache-операции через CP15.
 * Ядро gpSP (dynarec) вызывает эти функции для сброса/инвалидации кэша
 * после генерации машинного кода. */

#include <stdint.h>

static void dsb(void) { __asm volatile("dsb" ::: "memory"); }
static void isb(void) { __asm volatile("isb" ::: "memory"); }

/* Clean & invalidate entire L1 data cache (by set/way).
 * Читаем CCSIDR через CSSELR — стандартная процедура для ARMv7-A. */
static void dcache_clean_invalidate_all(void)
{
    uint32_t ccsidr, ways, sets, w, s;

    __asm volatile("mcr p15, 2, %0, c0, c0, 0" :: "r"(0));   /* CSSELR: L1 data */
    isb();
    __asm volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr)); /* CCSIDR */
    sets = ((ccsidr >> 13) & 0x3FFF) + 1;
    ways = ((ccsidr >> 3) & 0x3FF) + 1;
    for (w = 0; w < ways; w++)
        for (s = 0; s < sets; s++)
        {
            uint32_t val = (w << 30) | (s << 6);
            __asm volatile("mcr p15, 0, %0, c7, c14, 2" :: "r"(val)); /* clean+inv set/way */
        }
    dsb();
}

static void dcache_clean_all(void)
{
    uint32_t ccsidr, ways, sets, w, s;

    __asm volatile("mcr p15, 2, %0, c0, c0, 0" :: "r"(0));
    isb();
    __asm volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
    sets = ((ccsidr >> 13) & 0x3FFF) + 1;
    ways = ((ccsidr >> 3) & 0x3FF) + 1;
    for (w = 0; w < ways; w++)
        for (s = 0; s < sets; s++)
        {
            uint32_t val = (w << 30) | (s << 6);
            __asm volatile("mcr p15, 0, %0, c7, c10, 2" :: "r"(val)); /* clean set/way */
        }
    dsb();
}

static void dcache_invalidate_all(void)
{
    uint32_t ccsidr, ways, sets, w, s;

    __asm volatile("mcr p15, 2, %0, c0, c0, 0" :: "r"(0));
    isb();
    __asm volatile("mrc p15, 1, %0, c0, c0, 0" : "=r"(ccsidr));
    sets = ((ccsidr >> 13) & 0x3FFF) + 1;
    ways = ((ccsidr >> 3) & 0x3FF) + 1;
    for (w = 0; w < ways; w++)
        for (s = 0; s < sets; s++)
        {
            uint32_t val = (w << 30) | (s << 6);
            __asm volatile("mcr p15, 0, %0, c7, c6, 2" :: "r"(val)); /* invalidate set/way */
        }
    dsb();
}

static void icache_invalidate_all(void)
{
    uint32_t dummy = 0;
    __asm volatile("mcr p15, 0, %0, c7, c5, 0" :: "r"(dummy)); /* invalidate I-cache */
    __asm volatile("mcr p15, 0, %0, c8, c7, 0" :: "r"(dummy)); /* invalidate TLBs (заодно) */
    dsb();
    isb();
}

int warm_init(void) { return 0; }
void warm_finish(void) {}

int warm_cache_op_range(int ops, void *virt_addr, unsigned long size)
{
    /* На bare-metal ренж-операции упрощаем до полных — корректно, хоть и
     * медленнее. Размер кэш-линии 64 байта, но нас устраивает полный сброс. */
    (void)virt_addr; (void)size;
    if (ops & WOP_D_CLEAN)
        dcache_clean_all();
    if (ops & WOP_D_INVALIDATE)
        dcache_invalidate_all();
    if (ops & WOP_I_INVALIDATE)
        icache_invalidate_all();
    return 0;
}

int warm_cache_op_all(int ops)
{
    if (ops & WOP_D_CLEAN)
        dcache_clean_all();
    if (ops & WOP_D_INVALIDATE)
        dcache_invalidate_all();
    if (ops & WOP_I_INVALIDATE)
        icache_invalidate_all();
    return 0;
}

int warm_change_cb_upper(int cb, int is_set)
{
    /* Управление C/B битами MMU не используется на нашем образе — no-op. */
    (void)cb; (void)is_set;
    return -1;
}

int warm_change_cb_range(int cb, int is_set, void *virt_addr, unsigned long size)
{
    (void)cb; (void)is_set; (void)virt_addr; (void)size;
    return -1;
}

unsigned long warm_virt2phys(const void *ptr)
{
    /* Без MMU-трансляции вернём сам адрес. */
    return (unsigned long)ptr;
}