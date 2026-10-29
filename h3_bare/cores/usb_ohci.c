// usb_ohci.c — OHCI-драйвер для H3. Паттерн — TinyUSB hcd_ohci.c
// (тот же стек, что использует uli/allwinner-bare-metal на H3).
//
// Ключевые принципы из TinyUSB:
//   1. HCR делается при init, head/списки пишутся ТОЛЬКО после HCR
//      (HC не в OPER — запись не игнорируется).
//   2. control_head_ed настраивается ОДИН раз. Каждая передача обновляет
//      содержимое ED в памяти (HC читает через DMA) + CLF.
//      Писать HcControlHeadED при работающем HC нельзя — игнорируется.
//   3. frame_interval пишется с toggle FIT (bit31), как Linux/TinyUSB.
//   4. Передача завершена, когда CC setup TD != NOT_ACCESSED.
#include <string.h>
#include "h3.h"
#include "h3_ccu.h"
#include "uart.h"
#include "usb_ohci.h"

// ---- OHCI register block ----
typedef struct {
    volatile uint32_t rev;
    volatile uint32_t ctrl;
    volatile uint32_t cmdstatus;
    volatile uint32_t intrstatus;
    volatile uint32_t intrenable;
    volatile uint32_t intrdisable;
    volatile uint32_t hcca;
    volatile uint32_t peried;
    volatile uint32_t ctrlhead;
    volatile uint32_t ctrlcur;
    volatile uint32_t bulkhead;
    volatile uint32_t bulkcur;
    volatile uint32_t donehead;
    volatile uint32_t fminterval;
    volatile uint32_t fmremaining;
    volatile uint32_t fmnumber;
    volatile uint32_t periodstart;
    volatile uint32_t lsperiod;
    volatile uint32_t rha_des;
    volatile uint32_t rhb_des;
    volatile uint32_t rhstatus;
    volatile uint32_t rhport[2];
} ohci_regs_t;

#define RH_PS_CCS   (1u << 0)
#define RH_PS_PPS   (1u << 8)
#define RH_PS_LSDA  (1u << 9)
#define RH_PS_PRS   (1u << 4)
#define RH_PS_PES   (1u << 1)
#define RH_PS_CSC   (1u << 16)
#define RH_PS_PRSC  (1u << 20)

#define ED_SKIP        (1u << 14)
#define ED_LOWSPEED    (1u << 13)
#define ED_FROM_TD     (0u << 11)   // DIR=00: направление из TD (control)

#define TD_T_DATA0     (2u << 24)
#define TD_T_DATA1     (3u << 24)
#define TD_DP_SETUP    (0u << 19)
#define TD_DP_OUT      (1u << 19)
#define TD_DP_IN       (2u << 19)
#define TD_R           (1u << 18)
#define TD_CC_SHIFT    28
#define TD_CC_NOTACC   0xFu
#define TD_CC_NOERR    0x0u

typedef struct __attribute__((aligned(16))) {
    uint32_t cfg;     // w0: FUNC[6:0] ENDP[10:7] DIR[12:11] S[13] SKIP[14] ISO[15] MPS[26:16]
    uint32_t tail;    // w1: TailP (dummy TD)
    uint32_t head;    // w2: HeadP
    uint32_t next;    // w3: NextED
} ohci_ed_t;

typedef struct __attribute__((aligned(32))) {
    uint32_t cfg;     // w0: CC[31:28] EC[27:26] T[25:24] DI[23:21] DP[20:19] R[18]
    uint32_t cbp;
    uint32_t next;
    uint32_t be;
    uint32_t pad[4];
} ohci_td_t;

typedef struct __attribute__((aligned(256))) {
    uint32_t intr[32];
    uint16_t framenumber;
    uint16_t pad;
    uint32_t donehead;
    uint8_t  reserved[120];
} ohci_hcca_t;

static ohci_hcca_t g_hcca[2] __attribute__((section(".coherent"), aligned(256)));  // по одной на OHCI-порт
// ED/TD на порт — не общие, чтобы concurrent прерывания не сломали контроль.
static ohci_ed_t   g_head_ed[2] __attribute__((section(".coherent"), aligned(16)));  // control head ED
static ohci_td_t   g_td[2][8] __attribute__((section(".coherent"), aligned(32)));    // свой TD-пул на порт
static uint16_t    g_mps = 8;

// ---- периодический interrupt-IN (для HID), по [порт][слот] ----
// slot 0 = клавиатура, slot 1 = тачпад/мышь (второй интерфейс композита).
// Оба ED цепляются через NextED: HCCA → ED0 → ED1.
// PLE и HCCA-таблицу настраивает только slot 0; slot 1 только цепляет ED.
//
// ВСЕ эти структуры живут в uncached-области libh3_coherent_region (секция
// .coherent в linker.ld, 1 МБ), помечены uncached через MMU. Без этого
// write-back D-cache затирает toggle-биты, которые HC обновляет в DRAM.

static ohci_ed_t   g_int_ed[2][2] __attribute__((section(".coherent"), aligned(16)));
static ohci_td_t   g_int_td[2][2][2] __attribute__((section(".coherent"), aligned(32)));

// r788: bulk-пул для FS/LS-флешки (USB Mass Storage через OHCI). [порт][направление].
// Toggle отдельный по направлению (EP-IN и EP-OUT — независимые эндпоинты).
static ohci_ed_t g_bulk_ed[2][2] __attribute__((section(".coherent"), aligned(16)));
static ohci_td_t g_bulk_td[2][2][2] __attribute__((section(".coherent"), aligned(32)));
static uint8_t   g_bulk_toggle[2][2];

static int ohci_idx(uint32_t base) {
    return (base == 0x01C1B400u) ? 0 : 1;
}

// ---- D-cache maintenance по MVA (Cortex-A7) ----
static inline void cache_clean(uint32_t addr, uint32_t size);
static inline void cache_invalidate(uint32_t addr, uint32_t size);

// ---- периодический interrupt-IN (для HID) ----

int usb_ohci_intr_in_start(uint32_t base, uint8_t addr, uint8_t ep,
                           uint8_t* buf, uint16_t len, int slot) {
    // r0.417 (L2 аудита): API-защита — иначе slot>=2 пишет в соседние
    // .coherent-структуры, а buf+len-1 при len==0 — UB (указатель за пределы).
    if (slot < 0 || slot >= 2 || !buf || len == 0) return -1;
    int idx = ohci_idx(base);
    ohci_regs_t* ohci = (ohci_regs_t*)base;
    int low_speed = usb_ohci_port_low_speed(base, 0);

    ohci_ed_t* ed  = &g_int_ed[idx][slot];
    ohci_td_t* td  = &g_int_td[idx][slot][0];
    ohci_td_t* dum = &g_int_td[idx][slot][1];

    memset(ed, 0, sizeof(*ed));
    memset(&g_int_td[idx][slot], 0, sizeof(g_int_td[idx][slot]));

    ed->cfg = (addr & 0x7f)
             | ((uint32_t)(ep & 0x0f) << 7)
             | (2u << 11)            // DIR=IN
             | (0u << 25)            // toggle carry = DATA0
             | (low_speed ? ED_LOWSPEED : 0)
             | ((uint32_t)len << 16);
    ed->head = (uint32_t)td;
    ed->tail = (uint32_t)dum;
    ed->next = 0;

    // TD toggle: оба слота одинаково — T=00 (toggle from ED), HC сам ведёт
    // DATA0/DATA1 через ED toggle carry.
    td->cfg = (TD_CC_NOTACC << TD_CC_SHIFT) | TD_DP_IN | TD_R;
    td->cbp = (uint32_t)buf;
    td->be  = (uint32_t)(buf + len - 1);
    td->next = (uint32_t)dum;

    dum->cfg = (TD_CC_NOTACC << TD_CC_SHIFT);
    dum->cbp = dum->be = 0;
    dum->next = (uint32_t)dum;

    cache_clean((uint32_t)ed, sizeof(*ed));
    cache_clean((uint32_t)&g_int_td[idx][slot], sizeof(g_int_td[idx][slot]));
    cache_clean((uint32_t)buf, len);

    if (slot == 0) {
        // Первый слот: HCCA-таблица → ED0, цеплялка NextED, PLE
        g_int_ed[idx][0].next = (uint32_t)&g_int_ed[idx][1];
        cache_clean((uint32_t)&g_int_ed[idx][0], sizeof(g_int_ed[idx][0]));

        for (int i = 0; i < 32; i++) g_hcca[idx].intr[i] = (uint32_t)&g_int_ed[idx][0];
        cache_clean((uint32_t)&g_hcca[idx], sizeof(g_hcca[0]));

        __asm volatile("dmb" ::: "memory");
        ohci->peried = (uint32_t)&g_int_ed[idx][0];
        ohci->ctrl |= (1u << 2);   // PLE
    } else {
        // Второй слот: цепляется через ED0.NextED (уже настроен в slot=0)
        __asm volatile("dmb" ::: "memory");
    }
    return 0;
}

// Опрос периодического interrupt-IN.
// Возврат: 1 = снят свежий пакет (данные в buf), 0 = пакета нет (TD ждёт,
//          ED/TD НЕ трогаем), -1 = TD завершился с ошибкой (CRC/timeout) —
//          вызывающий может перезапустить цепочку.
// ВАЖНО: переарм делаем ТОЛЬКО после снятия пакета. Прежний вариант
// переармливал TD на каждом опросе — при частых поллах это гонка с HC
// (HC пишет HEAD в DRAM, мы затирали из кэша) и «замирание» клавиатуры.
int usb_ohci_intr_in_poll(uint32_t base, uint8_t* buf, uint16_t len, int slot) {
    // r0.417 (L2 аудита): API-защита (как в intr_in_start)
    if (slot < 0 || slot >= 2 || !buf || len == 0) return -1;
    int idx = ohci_idx(base);
    ohci_td_t* td = &g_int_td[idx][slot][0];

    cache_invalidate((uint32_t)td, sizeof(td[0]));
    uint32_t cc = td->cfg >> TD_CC_SHIFT;

    if (cc == TD_CC_NOTACC)
        return 0;                       // передача ещё идёт / пакета нет

    // Переарм (общий для «пакет» и «ошибка»): вернуть TD в цепочку.
    uint32_t cfg = (TD_CC_NOTACC << TD_CC_SHIFT) | TD_DP_IN | TD_R;
    td->cfg = cfg;
    td->cbp = (uint32_t)buf;
    td->be  = (uint32_t)(buf + len - 1);
    td->next = (uint32_t)&g_int_td[idx][slot][1];
    cache_clean((uint32_t)td, sizeof(td[0]));
    cache_clean((uint32_t)buf, len);

    // head/tail: возвращаем на активный TD. Не инвалидируем и не читаем ED —
    // stale-копия кары HC из кэша не должна затираться (она и так у HC в DRAM).
    g_int_ed[idx][slot].head = (uint32_t)td;
    g_int_ed[idx][slot].tail = (uint32_t)&g_int_td[idx][slot][1];
    cache_clean((uint32_t)&g_int_ed[idx][slot], sizeof(g_int_ed[idx][slot]));

    if (cc != TD_CC_NOERR)
        return -1;                      // ошибка завершения

    // r0.417 (L2): инвалидируем по фактической длине, а не фиксированные
    // 64 байта (для 8-байтного отчёта убивались чужие dirty-линии).
    cache_invalidate((uint32_t)buf, len);
    return 1;
}

void usb_ohci_set_mps(uint16_t mps) {
    if (mps >= 8 && mps <= 64) g_mps = mps;
}

// ---- D-cache maintenance: L1 (by MVA) + L2/PL310 (by PA) ----
// U-Boot оставляет MMU + write-back D-cache включёнными. OHCI-DMA читает
// физическую DRAM НАПРЯМУЮ. Критично (эталон h3_mmu.c:73-77): на H3 под L1
// стоит L2-кэш; обычный L1-clean (DCCMVAC) сбрасывает строку ЛИШЬ в L2, НЕ в
// DRAM. Без L2-операции (by-PA, PL310 @0x01C43000) DMA-мастер видит
// УСТАРЕВШИЕ ED/TD (лог r804: td=0xF2080000 — мусор из DRAM, свежие данные
// застряли в L2), bulk падает «TD NotAccessed». Поэтому clean/invalidate
// делаем на ОБОИХ уровнях: L1 by MVA + L2 by PA.
#define PL310_BASE        0x01C43000u
#define PL310_CACHE_SYNC  0x730u
#define PL310_CLEAN_INV_PA 0x7B0u

static int pl310_present(void) {
    uint32_t id = ((volatile uint32_t*)PL310_BASE)[0];   /* CACHE_ID */
    return ((id & 0xFFFFFF00u) == 0x41000000u) && ((id & 0x0Fu) >= 8u);
}

static inline void pl310_sync(void) {
    if (!pl310_present()) return;
    volatile uint32_t* r = (volatile uint32_t*)PL310_BASE;
    r[PL310_CACHE_SYNC / 4] = 0;
    for (volatile int i = 0; i < 1000 && (r[PL310_CACHE_SYNC / 4] & 1u); i++) ;
}

// L1 clean(+invalidate) by MVA → строка уходит в L2; затем L2 clean+invalidate
// by PA → строка уходит в DRAM. Точечно, без глобального flush.
static inline void cache_clean(uint32_t addr, uint32_t size) {
    uint32_t start = addr & ~0x1Fu;
    uint32_t end = (addr + size + 0x1Fu) & ~0x1Fu;
    volatile uint32_t* l2 = (volatile uint32_t*)PL310_BASE;
    for (uint32_t a = start; a < end; a += 32)
        __asm volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"(a));  /* DCCIMVAC */
    __asm volatile("dsb" ::: "memory");
    pl310_sync();
    for (uint32_t a = start; a < end; a += 32)
        l2[PL310_CLEAN_INV_PA / 4] = a;
    pl310_sync();
    __asm volatile("dsb" ::: "memory");
}

// Сперва L2 (clean+invalidate by PA), затем L1 invalidate by MVA.
static inline void cache_invalidate(uint32_t addr, uint32_t size) {
    uint32_t start = addr & ~0x1Fu;
    uint32_t end = (addr + size + 0x1Fu) & ~0x1Fu;
    volatile uint32_t* l2 = (volatile uint32_t*)PL310_BASE;
    pl310_sync();
    for (uint32_t a = start; a < end; a += 32)
        l2[PL310_CLEAN_INV_PA / 4] = a;
    pl310_sync();
    __asm volatile("dsb" ::: "memory");
    for (uint32_t a = start; a < end; a += 32)
        __asm volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(a));  /* DCIMVAC */
    __asm volatile("dsb" ::: "memory");
}

// Полный цикл тактов/PHY (по uli reference + ICR для AHB burst)
// r801 (по замечанию владельца): сбрасываем и ставим ТОЛЬКО свои биты (маска),
// не трогая чужие — U-Boot мог оставить свои значения в битах PHY/USB.
static void usb_port_hw_init(uint32_t ohci_base) {
    int port_num = (ohci_base == OHCI1_BASE) ? 1 : (ohci_base == OHCI2_BASE) ? 2 : 0;
    if (port_num == 0) return;

    // e/o/phy-rst/phcal/phy-clk — биты ИМЕННО этого порта (см. h3_ccu.h)
    uint32_t e_bit = (1u << (24 + port_num));
    uint32_t o_bit = (1u << (28 + port_num));
    uint32_t phy_rst = (1u << port_num);
    uint32_t phy_cal = (1u << (8 + port_num));
    uint32_t phy_clk = (1u << (16 + port_num));
    uint32_t mask = e_bit | o_bit | phy_rst | phy_cal | phy_clk;

    H3_CCU->BUS_CLK_GATING0  &= ~(e_bit | o_bit);
    H3_CCU->BUS_SOFT_RESET0  &= ~(e_bit | o_bit);
    H3_CCU->USBPHY_CFG &= ~mask;              /* только свои биты PHY */
    udelay(10000);

    H3_CCU->BUS_CLK_GATING0  |= e_bit | o_bit;
    H3_CCU->BUS_SOFT_RESET0  |= e_bit | o_bit;
    H3_CCU->USBPHY_CFG |= mask;               /* снова только свои */
    udelay(30000);
    // (замечание владельца: при конфигурации своих регистров маской не менять
    //  не нужные нам биты — чужие значения U-Boot сохраняются)

    // ICR — AHB burst config (INCR16/8/4), base+0x800 (EHCI того же PHY).
    // Это регистры конфигурации (целиком, не RMW), нужны EHCI-партнёру.
    uint32_t ehci_base = (port_num == 1) ? 0x01C1B000u : 0x01C1C000u;
    volatile uint32_t* icr = (volatile uint32_t*)(ehci_base + 0x800);
    icr[0] = 0x00000701u;
    icr[4] = 0;
}

int usb_ohci_init(uint32_t base) {
    ohci_regs_t* ohci = (ohci_regs_t*)base;
    extern int uart0_printf(const char* fmt, ...);

    // Пометить когерентную (uncached) область, где лежат ED/TD/HCCA/буферы.
    // Только один раз на первом порту.
    {
        static int mmu_done = 0;
        if (!mmu_done) {
            mmu_mark_uncached(H3_MEM_COHERENT_REGION);
            mmu_done = 1;
        }
    }

    uart0_printf("usb: base=0x%X rev=0x%X rha=0x%X\n",
                 base, ohci->rev, ohci->rha_des);

    // Диагностика D-cache: читаем SCTLR (bit 2 = D-cache enable)
    uint32_t sctlr;
    __asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    uart0_printf("usb: SCTLR=0x%X %s\n", sctlr,
                 (sctlr & 4) ? "DCACHE=ON" : "DCACHE=OFF");

    usb_port_hw_init(base);

    if (ohci->rev != 0x10) { uart_puts("usb: OHCI dead\n"); return -1; }

    // ---- r801: снимаем блокеры U-Boot (по замечанию владельца) ----
    // U-Boot после `usb start`/`usb scan` оставил в HcControlHeadED/
    // HcControlCurrentED/HcBulkHeadED свои ED в уже освобождённой памяти и
    // включённые CLE/BLE. HC продолжает обходить чужие списки — наши передачи
    // не продвигаются (лог r794/r800: ctrlcur на чужом адресе, device gone).
    // Эталон Bare_linux делает HCR + зануление всех указателей списков.
    // HCR сбрасывает внутреннее состояние HC (указатели списков, тайминги),
    // НО НЕ root port — устройство остаётся подключено.
    ohci->cmdstatus = (1u << 0);                       /* HCR */
    __asm volatile("dsb" ::: "memory");
    for (uint32_t t = 0; t < 1000u && (ohci->cmdstatus & 1u); t++) udelay(10);
    udelay(2000);

    ohci->intrdisable = 0xFFFFFFFFu;

    int idx = ohci_idx(base);
    memset(&g_hcca[idx], 0, sizeof(g_hcca[0]));
    memset(&g_head_ed[idx], 0, sizeof(g_head_ed[0]));
    memset(&g_td[idx], 0, sizeof(g_td[0]));

    g_head_ed[idx].cfg = ED_SKIP | ((uint32_t)g_mps << 16);
    g_head_ed[idx].head = 1;
    g_head_ed[idx].tail = 1;

    ohci->hcca = (uint32_t)&g_hcca[idx];

    uint32_t fi = 0x2edf;
    uint32_t fsmps = (6 * (fi - 210)) / 7;
    ohci->fminterval = (fsmps << 16) | fi;
    ohci->fminterval ^= (1u << 31);
    ohci->periodstart = (fi * 9) / 10;
    ohci->lsperiod = 0x628;

    // Списки: ВСЕ обнуляем (эталон) — HC не должен ходить по чужим ED U-Boot.
    // При этом конфигурируем маской (сохраняем не нужные нам биты регистра).
    ohci->bulkhead = 0;
    ohci->bulkcur  = 0;
    ohci->peried   = 0;
    ohci->ctrlhead = 0;
    ohci->ctrlcur  = 0;
    ohci->donehead = 0;
    g_hcca[idx].donehead = 0;

    // OPER (маской: сохраняем биты 0..5 кроме CLE/BLE) + CLE + BLE
    ohci->ctrl = (ohci->ctrl & ~(3u << 6)) | (2u << 6) | (1u << 4) | (1u << 5);
    udelay(3000);

    // W1C: снять «залипший» WDH (WritebackDoneHead) и прочие флаги — иначе
    // HC считает done-очередь не обслуженной и не продвигает списки.
    ohci->intrstatus = 0xFFFFFFFFu;
    __asm volatile("dsb" ::: "memory");

    ohci->rhstatus = (1u << 16);
    ohci->rhport[0] |= RH_PS_PPS;
    uint32_t potpgt = (ohci->rha_des >> 24) & 0xFF;
    udelay(potpgt * 2000 + 5000);

    uint32_t st = ohci->rhport[0];
    uart0_printf("usb: ohci port=0x%X\n", st);
    if (st & RH_PS_CCS) uart_puts("usb: DEVICE on OHCI!\n");
    return 0;
}

int usb_ohci_root_port_connected(uint32_t base, int port) {
    return (((ohci_regs_t*)base)->rhport[port] & RH_PS_CCS) ? 1 : 0;
}

uint32_t usb_ohci_port_status(uint32_t base, int port) {
    return ((ohci_regs_t*)base)->rhport[port];
}

int usb_ohci_port_low_speed(uint32_t base, int port) {
    return (((ohci_regs_t*)base)->rhport[port] & RH_PS_LSDA) ? 1 : 0;
}

int usb_ohci_port_reset(uint32_t base, int port) {
    ohci_regs_t* ohci = (ohci_regs_t*)base;
    extern int uart0_printf(const char* fmt, ...);

    ohci->rhport[port] |= RH_PS_PRS;
    udelay(50000);                       // 50ms PRS pulse (USB spec: >= 10ms)
    for (uint32_t t = 0; t < 200000; t++) {
        uint32_t st = ohci->rhport[port];
        if (!(st & RH_PS_PRS)) {
            ohci->rhport[port] = RH_PS_CSC | RH_PS_PRSC;  // w1c stale changes
            udelay(100000);              // USB recovery time — 100ms перед первым get_descriptor!
            st = ohci->rhport[port];
            uart0_printf("usb: port after reset=0x%X\n", st);
            if (!(st & RH_PS_CCS)) { uart_puts("usb: device gone\n"); return -1; }
            if (!(st & RH_PS_PES)) { uart_puts("usb: PES not set\n"); return -1; }
            return 0;
        }
        udelay(1);
    }
    uart_puts("usb: port reset timeout\n");
    return -1;
}

static void td_link(ohci_td_t* a, ohci_td_t* b) { a->next = (uint32_t)b; }

int usb_ohci_ctrl_transfer(uint32_t base, uint8_t addr, uint8_t ep_in,
                           const uint8_t* setup, uint8_t setup_len,
                           uint8_t* data, uint32_t data_len, int dir_in,
                           uint32_t timeout_ms) {
    // r0.417 (L2 аудита): API-защита (setup+setup_len-1 и data+data_len-1)
    if (!setup || setup_len == 0) return -1;
    if (data_len > 0 && !data) return -1;
    (void)ep_in;
    ohci_regs_t* ohci = (ohci_regs_t*)base;
    extern int uart0_printf(const char* fmt, ...);
    int pi = ohci_idx(base);

    // r801 (владелец: «проверь, снимаем ли мы блокеры»): перед переписыванием
    // ED гасим CLE и ждём, пока HC дочитает старый ED (ctrlcur==0). Без этого
    // контроллер застревает на старом ED и все следующие control-передачи
    // падают. Раньше (r794) ожидание висело, потому что init НЕ чистил
    // ctrlcur U-Boot'а (не было HCR) — теперь init снимает блокеры (HCR +
    // обнуление списков), поэтому gating работает как в эталоне Bare_linux.
    ohci->ctrl &= ~(1u << 4);   /* CLE off */
    __asm volatile("dsb" ::: "memory");
    for (uint32_t w = 0; w < 2000u && ohci->ctrlcur; w++) udelay(10);
    // После CLE-off дочитываем зависшую done-очередь (эталон 665-678):
    // HcDoneHead залипает, пока не снимем WDH.
    cache_invalidate((uint32_t)&g_hcca[pi], sizeof(g_hcca[0]));
    if (g_hcca[pi].donehead != 0) {
        g_hcca[pi].donehead = 0;
        cache_clean((uint32_t)&g_hcca[pi], sizeof(g_hcca[0]));
    }
    ohci->donehead = 0;
    __asm volatile("dsb" ::: "memory");
    ohci->intrstatus = (1u << 1);   /* W1C: WDH */

    // Используем ED/TD своего порта — у каждого порта свой пул
    // (иначе прерывания/контроль одного порта ломали бы другой).
    ohci_ed_t* ed = &g_head_ed[pi];
    ohci_td_t* td = &g_td[pi][0];

    memset(td, 0, sizeof(g_td[0]));

    ohci_td_t* t_setup = &td[0];
    ohci_td_t* t_data  = &td[1];
    ohci_td_t* t_stat  = &td[2];
    ohci_td_t* t_dummy = &td[3];

    int low_speed = usb_ohci_port_low_speed(base, 0) ? 1 : 0;

    // ED в памяти (head-ED уже висит в control list с init)
    ed->cfg = (addr & 0x7F)
            | (low_speed ? ED_LOWSPEED : 0)
            | ED_FROM_TD
            | ED_SKIP
            | ((uint32_t)g_mps << 16);
    ed->next = 0;

    // SETUP: DP=00, DATA0
    t_setup->cfg = (TD_CC_NOTACC << TD_CC_SHIFT) | TD_T_DATA0 | TD_DP_SETUP;
    t_setup->cbp = (uint32_t)setup;
    t_setup->be  = (uint32_t)(setup + setup_len - 1);
    td_link(t_setup, t_data);

    if (data_len > 0) {
        // DATA: DP=OUT/IN, DATA1; для IN ставим R (короткий пакет — ок)
        uint32_t dp = dir_in ? TD_DP_IN : TD_DP_OUT;
        t_data->cfg = (TD_CC_NOTACC << TD_CC_SHIFT) | TD_T_DATA1 | dp | (dir_in ? TD_R : 0);
        t_data->cbp = (uint32_t)data;
        t_data->be  = (uint32_t)(data + data_len - 1);
        td_link(t_data, t_stat);

        // STATUS: DATA1, противоположное направление
        t_stat->cfg = (TD_CC_NOTACC << TD_CC_SHIFT) | TD_T_DATA1 | (dir_in ? TD_DP_OUT : TD_DP_IN);
        t_stat->cbp = t_stat->be = 0;
        td_link(t_stat, t_dummy);
    } else {
        // Без DATA-фазы: t_data = статусный IN/OUT, сразу на dummy
        t_data->cfg = (TD_CC_NOTACC << TD_CC_SHIFT) | TD_T_DATA1 |
                       (dir_in ? TD_DP_OUT : TD_DP_IN);
        t_data->cbp = t_data->be = 0;
        td_link(t_data, t_dummy);
    }

    // ---- r806: САМОЗАМКНУТЫЙ dummy (эталон usb_ohci.c:474-476). ----
    // memset обнулил t_dummy->next = 0 (терминатор). На этом H3-OHCI
    // терминатор НЕ работает — HC ждёт циклического dummy, чтобы завершить
    // TD через WDH (комментарий эталона в bulk, 623-628). Без самоцикла
    // status-TD остаётся NotAccessed (лог r805: cc=0/8/15 — SETUP ок,
    // DATA IN короткий пакет cc=8 DataUnderrun, status не выполнен).
    t_dummy->cfg = (TD_CC_NOTACC << TD_CC_SHIFT);
    t_dummy->cbp = t_dummy->be = 0;
    t_dummy->next = (uint32_t)t_dummy;

    // ED голову на setup, хвост на dummy
    ed->head = (uint32_t)t_setup;
    ed->tail = (uint32_t)t_dummy;

    // Снять SKIP (HC игнорирует ED со SKIP=1)
    ed->cfg &= ~ED_SKIP;

    // ---- D-cache maintenance: clean ED, TDs, setup, data. ----
    // ДОЛЖНО быть ПОСЛЕ снятия SKIP (иначе HC видит SKIP=1 в DRAM)
    cache_clean((uint32_t)ed, sizeof(*ed));
    cache_clean((uint32_t)td, sizeof(g_td[0]));
    cache_clean((uint32_t)setup, setup_len);
    if (data_len > 0 && !dir_in) cache_clean((uint32_t)data, data_len);

    // DMB: гарантирует, что все store в ED/TD (и cache_clean) видны
    // до того, как HC прочитает управляющие регистры и начнёт DMA.
    // Без этого на Cortex-A7 возможна гонка (HC читает старый ED в DRAM).
    __asm volatile("dmb" ::: "memory");

    // ed уже висит в control list после init. Но HC закэшировал
    // его со SKIP=1 при загрузке head→current в init. Простое изменение
    // в памяти + cache_clean не заставляет HC перечитать ED.
    // Перезаписываем head + current при каждой передаче (как в рабочем
    // прототипе) — это гарантирует, что HC увидит новый ED.

    // r801: ОЧИСТИТЬ DONE-ОЧЕРЕДЬ ПЕРЕД ЗАПУСКОМ CONTROL (эталон usb_ohci.c:493).
    // Если в HCCA.DoneHead/регистре остался TD (например после сбоя bulk),
    // HC не продвигает НИКАКОЙ список, пока очередь непуста — control висел
    // на «cc setup=15» / BOT recovery на TIMEOUT.
    cache_invalidate((uint32_t)&g_hcca[pi], sizeof(g_hcca[0]));
    if (g_hcca[pi].donehead != 0) {
        g_hcca[pi].donehead = 0;
        cache_clean((uint32_t)&g_hcca[pi], sizeof(g_hcca[0]));
    }
    ohci->donehead = 0;
    __asm volatile("dsb" ::: "memory");
    ohci->intrstatus = (1u << 1);   /* W1C: WDH */

    ohci->ctrlhead = (uint32_t)ed;
    ohci->ctrlcur  = (uint32_t)ed;
    ohci->ctrl |= (1u << 4);        /* CLE: список снова активен (выключали в gating!) */
    __asm volatile("dsb" ::: "memory");
    ohci->cmdstatus = (1u << 1);   // CLF

    // Ждём, пока HC завершит передачу: проверяем CC последнего TD
    // (t_data для data_len=0 — это status phase; для data_len>0 — t_stat).
    // r802: ОБСЛУЖИВАЕМ done-очередь ВНУТРИ цикла (эталон usb_ohci.c:689).
    // Пока HcDoneHead/HCCA.DoneHead непусты, HC считает done-очередь неполной:
    // берёт наш ED в current и СРАЗУ отпускает, TD остаётся NotAccessed
    // (лог r802: ctrl TIMEOUT cur=0x0 cc=0/15). Чистим ОБА (HCCA с invalidate
    // + регистр) и снимаем WDH (W1C).
    ohci_td_t* last = (data_len > 0) ? t_stat : t_data;
    uint32_t elapsed = 0;
    for (;;) {
        cache_invalidate((uint32_t)td, sizeof(g_td[0]));
        if ((last->cfg >> TD_CC_SHIFT) != TD_CC_NOTACC) break;

        cache_invalidate((uint32_t)&g_hcca[pi], sizeof(g_hcca[0]));
        if (g_hcca[pi].donehead != 0 || ohci->donehead != 0) {
            g_hcca[pi].donehead = 0;
            cache_clean((uint32_t)&g_hcca[pi], sizeof(g_hcca[0]));
            ohci->donehead = 0;
            __asm volatile("dsb" ::: "memory");
            ohci->intrstatus = (1u << 1);   /* W1C: WDH */
            __asm volatile("dsb" ::: "memory");
        }

        udelay(1000);
        if (++elapsed > timeout_ms) {
            /* r856: короткий IN (DataUnderrun) — буфер заполнен, но status-TD
               на этом OHCI не выполнился. Принимаем, если принято ровно
               data_len байт (CBP после полного IN = data + data_len). */
            uint32_t cc_s = t_setup->cfg >> TD_CC_SHIFT;
            uint32_t cc_d = t_data->cfg >> TD_CC_SHIFT;
            if (cc_s == TD_CC_NOERR && dir_in && data_len > 0 && cc_d == 0x8u) {
                uint32_t cbp = t_data->cbp;
                uint32_t got = (cbp >= (uint32_t)data &&
                                cbp <= (uint32_t)data + data_len)
                             ? (cbp - (uint32_t)data) : 0;
                if (got >= data_len) {
                    uart0_printf("usb: ctrl short-IN ok a=%u len=%u\n",
                                 addr, (unsigned)data_len);
                    cache_invalidate((uint32_t)data, data_len);
                    return (int)data_len;
                }
            }
            uart0_printf("usb: ctrl TIMEOUT a=%u len=%u cmd=0x%X cur=0x%X isr=0x%X cc=%u/%u/%u\n",
                         addr, (unsigned)data_len,
                         (unsigned)ohci->cmdstatus, (unsigned)ohci->ctrlcur,
                         (unsigned)ohci->intrstatus,
                         (unsigned)(t_setup->cfg >> TD_CC_SHIFT),
                         (unsigned)(t_data->cfg >> TD_CC_SHIFT),
                         (unsigned)(last->cfg >> TD_CC_SHIFT));
            return -1;
        }
    }

    // Проверяем CC — как эталон (usb_ohci.c:540-544): только setup + last.
    // Для IN с коротким пакетом (полный дескриптор 18 байт < mps 64) DATA-фаза
    // завершается cc=8 (DataUnderrun) — это НОРМАЛЬНЫЙ успех, не ошибка.
    // Проверять t_data отдельно нельзя — фейлил бы все короткие IN.
    uint32_t cc;
    cc = t_setup->cfg >> TD_CC_SHIFT;
    if (cc != TD_CC_NOERR) { uart0_printf("usb: cc setup=%u addr=%u\n", cc, addr); return -1; }
    cc = last->cfg >> TD_CC_SHIFT;
    if (cc != TD_CC_NOERR) { uart0_printf("usb: cc stat=%u addr=%u\n", cc, addr); return -1; }
    // IN с коротким пакетом (строковые дескрипторы, полный конфиг) — норма;
    // CC-контроль уже прошёл, данные инвалидируем ниже.
    (void)t_data;

    // IN: инвалидируем кэш для буфера, чтобы прочитать свежие данные (записанные DMA)
    if (data_len > 0 && dir_in) cache_invalidate((uint32_t)data, data_len);

    return (int)data_len;
}

// ---- r788: bulk (для USB Mass Storage через OHCI, FS/LS-флешка) ----
// Перенос отлаженного паттерна из эталона Bare_linux/src/usb_ohci.c:
//  - ED = from-TD (направление в TD), dummy-самоцикл, BLE через RMW;
//  - toggle раздельный по направлениям, переключается после каждой передачи
//    по числу пакетов;
//  - перед запуском гасим BLE и ждём bulkcur==0 (HC дочитал ED);
//  - пауза udelay(200) перед BLE (L2-синхронизация by-PA);
//  - HALTED сбрасываем только после того, как HC отпустил ED.
static int usb_ohci_bulk(uint32_t base, uint8_t addr, uint8_t ep,
                         const uint8_t* out, uint8_t* in,
                         uint32_t len, uint16_t mps, int dir_in) {
    int idx = ohci_idx(base);
    if (idx < 0 || len == 0 || mps == 0) return -1;
    extern int uart0_printf(const char* fmt, ...);
    ohci_regs_t* ohci = (ohci_regs_t*)base;
    int di = dir_in ? 1 : 0;
    ohci_ed_t* ed  = &g_bulk_ed[idx][di];
    ohci_td_t* td  = &g_bulk_td[idx][di][0];
    ohci_td_t* dum = &g_bulk_td[idx][di][1];
    uint8_t tog = g_bulk_toggle[idx][di];

    // Пока HC держит список, ED переписывать нельзя: гасим BLE и ждём.
    ohci->ctrl &= ~(1u << 5);   /* BLE off */
    __asm volatile("dsb" ::: "memory");
    for (uint32_t w = 0; w < 2000u && ohci->bulkcur; w++) udelay(10);

    memset(&g_bulk_td[idx][di], 0, sizeof(g_bulk_td[idx][di]));

    ed->cfg = (uint32_t)(addr & 0x7F)
            | ((uint32_t)(ep & 0x0F) << 7)
            | ED_FROM_TD
            | ((uint32_t)mps << 16);
    ed->next = 0;

    // IN: БЕЗ TD_R — иначе завершение по первому NAK и недочитывание блока.
    td->cfg = (TD_CC_NOTACC << TD_CC_SHIFT)
            | (tog ? TD_T_DATA1 : TD_T_DATA0)
            | (dir_in ? TD_DP_IN : TD_DP_OUT);
    td->cbp = (uint32_t)(uintptr_t)(dir_in ? in : out);
    td->be  = td->cbp + len - 1;
    td->next = (uint32_t)(uintptr_t)dum;

    dum->cfg = (TD_CC_NOTACC << TD_CC_SHIFT);
    dum->cbp = dum->be = 0;
    dum->next = (uint32_t)(uintptr_t)dum;   /* САМОЦИКЛ dummy — как в control-пути */

    ed->cfg |= ED_SKIP;
    cache_clean((uint32_t)(uintptr_t)ed, sizeof(*ed));
    ed->head = (uint32_t)(uintptr_t)td;
    ed->tail = (uint32_t)(uintptr_t)dum;
    cache_clean((uint32_t)(uintptr_t)ed, sizeof(*ed));
    ed->cfg &= ~ED_SKIP;
    cache_clean((uint32_t)(uintptr_t)ed, sizeof(*ed));
    cache_clean((uint32_t)(uintptr_t)&g_bulk_td[idx][di], sizeof(g_bulk_td[idx][di]));
    if (!dir_in) cache_clean((uint32_t)(uintptr_t)out, len);
    __asm volatile("dmb" ::: "memory");

    // Пауза перед запуском списка (L2 sync + запись ED/TD в DRAM).
    udelay(200);

    /* ЗЕРКАЛЬНО control-пути: bulkhead/bulkcur = адрес ED, BLE через RMW,
       затем done-очередь и только потом BLF (эталон usb_ohci.c:648-701).
       Порядок ВАЖЕН: снять WDH ДО включения BLE (наш прошлый вариант) —
       HC успевает прочитать старый ED; эталон включает BLE, потом чистит
       done, потом BLF. */
    ohci->bulkhead = (uint32_t)(uintptr_t)ed;
    ohci->bulkcur  = (uint32_t)(uintptr_t)ed;
    ohci->ctrl |= (1u << 5);        /* BLE */
    __asm volatile("dsb" ::: "memory");

    /* DONE-ОЧЕРЕДЬ ОБЯЗАТЕЛЬНА К ОБНУЛЕНИЮ ДО BLF (эталон 656-678). */
    cache_invalidate((uint32_t)(uintptr_t)&g_hcca[idx], sizeof(g_hcca[idx]));
    if (g_hcca[idx].donehead != 0) {
        g_hcca[idx].donehead = 0;
        cache_clean((uint32_t)(uintptr_t)&g_hcca[idx], sizeof(g_hcca[idx]));
    }
    ohci->donehead = 0;
    __asm volatile("dsb" ::: "memory");
    ohci->intrstatus = (1u << 1);   /* W1C: WDH */
    __asm volatile("dsb" ::: "memory");
    ohci->cmdstatus = (1u << 2);    /* BLF */
    __asm volatile("dsb" ::: "memory");

    uint32_t elapsed = 0;
    for (;;) {
        // Обслужить done-очередь (иначе HC не продвигает списки).
        cache_invalidate((uint32_t)(uintptr_t)&g_hcca[idx], sizeof(g_hcca[idx]));
        if (g_hcca[idx].donehead != 0 || ohci->donehead != 0) {
            g_hcca[idx].donehead = 0;
            cache_clean((uint32_t)(uintptr_t)&g_hcca[idx], sizeof(g_hcca[idx]));
            ohci->donehead = 0;
            __asm volatile("dsb" ::: "memory");
            ohci->intrstatus = (1u << 1);
            __asm volatile("dsb" ::: "memory");
            ohci->cmdstatus = (1u << 2);
            __asm volatile("dsb" ::: "memory");
        }
        cache_invalidate((uint32_t)(uintptr_t)td, sizeof(td[0]));
        if (((td->cfg >> TD_CC_SHIFT) & 0xFu) != TD_CC_NOTACC) break;
        udelay(1000);
        if (++elapsed > 2000) {
            uart0_printf("ohci-bulk: TIMEOUT d=%d ep=%d len=%u edcfg=0x%X ctl=0x%X cmd=0x%X isr=0x%X\n"
                         "  bh=0x%X bcur=0x%X td=0x%X hcca=0x%X fr=%u intr0=0x%X\n",
                         dir_in, ep, (unsigned)len,
                         (unsigned)ed->cfg, (unsigned)ohci->ctrl,
                         (unsigned)ohci->cmdstatus, (unsigned)ohci->intrstatus,
                         (unsigned)ohci->bulkhead, (unsigned)ohci->bulkcur,
                         (unsigned)td->cfg, (unsigned)ohci->hcca,
                         (unsigned)(g_hcca[idx].framenumber & 0xFFFFu),
                         (unsigned)g_hcca[idx].intr[0]);
            return -1;
        }
    }
    uint32_t cc = (td->cfg >> TD_CC_SHIFT) & 0xFu;
    if (dir_in) {
        cache_invalidate((uint32_t)(uintptr_t)td, sizeof(td[0]));
        cache_invalidate((uint32_t)(uintptr_t)in, len);
        uint32_t cbp = td->cbp;
        uint32_t accepted = len;
        if (cbp != 0 && td->be >= cbp) accepted = td->be - cbp + 1u;
        if (cc == TD_CC_NOERR && accepted != len) {
            uart0_printf("ohci-bulk: short IN got=%u want=%u\n", (unsigned)accepted, (unsigned)len);
            cc = 0x08u;   /* DataUnderrun: BOT recovery на слое выше */
        }
    }
    if (cc == TD_CC_NOERR) {
        uint32_t packets = (len + mps - 1) / mps;
        g_bulk_toggle[idx][di] = (uint8_t)(tog ^ (packets & 1u));
    }
    if (cc != TD_CC_NOERR) {
        uart0_printf("ohci-bulk: fail d=%d ep=%d cc=0x%X len=%u\n", dir_in, ep, cc, (unsigned)len);
        // Сброс HALTED только после того, как HC отпустил ED.
        ohci->ctrl &= ~(1u << 5);   /* BLE off */
        __asm volatile("dsb" ::: "memory");
        for (uint32_t w = 0; w < 2000u && ohci->bulkcur; w++) udelay(10);
        ed->head = (uint32_t)(uintptr_t)td;   /* снять Halted */
        cache_clean((uint32_t)(uintptr_t)ed, sizeof(*ed));
        g_bulk_toggle[idx][di] = 0;
        ohci->bulkhead = 0;
        ohci->bulkcur  = 0;
        __asm volatile("dsb" ::: "memory");
    }
    return (cc == TD_CC_NOERR) ? 0 : -1;
}

int usb_ohci_bulk_out(uint32_t base, uint8_t addr, uint8_t ep,
                      const uint8_t* data, uint32_t len, uint16_t mps) {
    return usb_ohci_bulk(base, addr, ep, data, 0, len, mps, 0);
}

int usb_ohci_bulk_in(uint32_t base, uint8_t addr, uint8_t ep,
                     uint8_t* data, uint32_t len, uint16_t mps) {
    return usb_ohci_bulk(base, addr, ep, 0, data, len, mps, 1);
}

void usb_ohci_bulk_toggle_reset(uint32_t base) {
    int idx = ohci_idx(base);
    if (idx < 0) return;
    g_bulk_toggle[idx][0] = 0;
    g_bulk_toggle[idx][1] = 0;
}

// BOT Reset Recovery через OHCI (control-передачи на устройство):
// Bulk-Only Reset (0xFF), GET_MAX_LUN, CLEAR_FEATURE(HALT) на оба bulk-EP.
void usb_ohci_bot_recover(uint32_t base) {
    extern int usb_ohci_ctrl_transfer(uint32_t base, uint8_t addr, uint8_t ep_in,
                                      const uint8_t* setup, uint8_t setup_len,
                                      uint8_t* data, uint32_t data_len, int dir_in,
                                      uint32_t timeout_ms);
    extern uint8_t usb_msd_get_eps(uint8_t* in, uint8_t* out);
    uint8_t in_ep = 2, out_ep = 1;   // дефолт (типичные bulk-EP для MSD)
    usb_msd_get_eps(&in_ep, &out_ep);
    uint8_t s[8];
    uint8_t lun = 0;

    s[0]=0xA1; s[1]=0xFE; s[2]=0; s[3]=0; s[4]=0; s[5]=0; s[6]=1; s[7]=0;
    usb_ohci_ctrl_transfer(base, 1, 0, s, 8, &lun, 1, 1, 1000);
    s[0]=0x21; s[1]=0xFF; s[2]=0; s[3]=0; s[4]=0; s[5]=0; s[6]=0; s[7]=0;
    usb_ohci_ctrl_transfer(base, 1, 0, s, 8, 0, 0, 0, 1000);
    s[0]=0x02; s[1]=0x01; s[2]=0; s[3]=0; s[4]=out_ep; s[5]=0; s[6]=0; s[7]=0;
    usb_ohci_ctrl_transfer(base, 1, 0, s, 8, 0, 0, 0, 1000);
    s[4]=in_ep | 0x80;
    usb_ohci_ctrl_transfer(base, 1, 0, s, 8, 0, 0, 0, 1000);
    usb_ohci_bulk_toggle_reset(base);
}