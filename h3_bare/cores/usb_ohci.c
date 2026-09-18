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

static ohci_hcca_t g_hcca[2];       // по одной на OHCI-порт
// ED/TD на порт — не общие, чтобы concurrent прерывания не сломали контроль.
static ohci_ed_t   g_head_ed[2];   // control head ED (skip=1 по умолчанию), своё на порт
static ohci_td_t   g_td[2][8];     // свой TD-пул на порт
static uint16_t    g_mps = 8;

// ---- периодический interrupt-IN (для HID-тача), по ED/TD на порт ----
static ohci_ed_t   g_int_ed[2];
static ohci_td_t   g_int_td[2];

static int ohci_idx(uint32_t base) {
    return (base == 0x01C1B400u) ? 0 : 1;
}

// ---- D-cache maintenance по MVA (Cortex-A7) ----
static inline void cache_clean(uint32_t addr, uint32_t size);
static inline void cache_invalidate(uint32_t addr, uint32_t size);

// ---- периодический interrupt-IN (для HID-тача) ----

int usb_ohci_intr_in_start(uint32_t base, uint8_t addr, uint8_t ep,
                           uint8_t* buf, uint16_t len) {
    int idx = ohci_idx(base);
    ohci_regs_t* ohci = (ohci_regs_t*)base;
    int low_speed = usb_ohci_port_low_speed(base, 0) ? 1 : 0;

    memset(&g_int_ed[idx], 0, sizeof(g_int_ed[0]));
    memset(&g_int_td[idx], 0, sizeof(g_int_td[0]));

    g_int_ed[idx].cfg = (addr & 0x7f)
                 | ((uint32_t)(ep & 0x0f) << 7)
                 | (2u << 11)            // DIR=IN
                 | (1u << 25)            // toggle carry
                 | (low_speed ? ED_LOWSPEED : 0)
                 | ((uint32_t)len << 16);
    g_int_ed[idx].head = (uint32_t)&g_int_td[idx];
    g_int_ed[idx].tail = (uint32_t)&g_int_td[idx];

    g_int_td[idx].cfg = (TD_CC_NOTACC << TD_CC_SHIFT) | TD_T_DATA0 | TD_DP_IN | TD_R;
    g_int_td[idx].cbp = (uint32_t)buf;
    g_int_td[idx].be  = (uint32_t)(buf + len - 1);
    g_int_td[idx].next = (uint32_t)&g_int_td[idx];

    cache_clean((uint32_t)&g_int_ed[idx], sizeof(g_int_ed[0]));
    cache_clean((uint32_t)&g_int_td[idx], sizeof(g_int_td[0]));
    cache_clean((uint32_t)buf, len);

    // периодическая таблица HCCA этого порта -> ED
    for (int i = 0; i < 32; i++) g_hcca[idx].intr[i] = (uint32_t)&g_int_ed[idx];
    cache_clean((uint32_t)&g_hcca[idx], sizeof(g_hcca[0]));

    // Правильный PLE: OHCI HcControl bit 2 (Periodic List Enable)
    // также надо записать HcPeriodicCurrentED
    ohci->peried = (uint32_t)&g_int_ed[idx];
    ohci->ctrl |= (1u << 2);   // PLE
    return 0;
}

int usb_ohci_intr_in_poll(uint32_t base, uint8_t* buf, uint16_t len) {
    int idx = ohci_idx(base);
    (void)len;
    cache_invalidate((uint32_t)&g_int_td[idx], sizeof(g_int_td[0]));
    uint32_t cc = g_int_td[idx].cfg >> TD_CC_SHIFT;
    if (cc != TD_CC_NOERR)
        return 0;
    cache_invalidate((uint32_t)buf, 64);
    // re-arm
    uint32_t cfg = g_int_td[idx].cfg;
    cfg = (cfg & ~(0xFu << TD_CC_SHIFT)) | (TD_CC_NOTACC << TD_CC_SHIFT);
    g_int_td[idx].cfg = cfg;
    cache_clean((uint32_t)&g_int_td[idx], sizeof(g_int_td[0]));
    return 1;
}

void usb_ohci_set_mps(uint16_t mps) {
    if (mps >= 8 && mps <= 64) g_mps = mps;
}

// ---- D-cache maintenance по MVA (Cortex-A7) ----
// U-Boot оставляет MMU + write-back D-cache включёнными. OHCI-DMA читает
// физическую DRAM, а наши ED/TD пишутся через кэш — поэтому перед запуском
// передачи чистим (clean) кэш для структур, после IN — инвалидируем кэш
// для буфера данных.
static inline void cache_clean(uint32_t addr, uint32_t size) {
    addr &= ~0x1Fu;
    uint32_t end = addr + size + 32;
    for (; addr < end; addr += 32) {
        __asm volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(addr)); // clean MVA
    }
    __asm volatile("dsb" ::: "memory");
}

static inline void cache_invalidate(uint32_t addr, uint32_t size) {
    addr &= ~0x1Fu;
    uint32_t end = addr + size + 32;
    for (; addr < end; addr += 32) {
        __asm volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(addr)); // invalidate MVA
    }
    __asm volatile("dsb" ::: "memory");
}

// Полный цикл тактов/PHY (по uli reference + ICR для AHB burst)
static void usb_port_hw_init(uint32_t ohci_base) {
    int port_num = (ohci_base == OHCI1_BASE) ? 1 : (ohci_base == OHCI2_BASE) ? 2 : 0;
    if (port_num == 0) return;

    uint32_t e_bit = (1u << (24 + port_num));
    uint32_t o_bit = (1u << (28 + port_num));
    uint32_t phy_rst = (1u << port_num);
    uint32_t phy_cal = (1u << (8 + port_num));
    uint32_t phy_clk = (1u << (16 + port_num));

    H3_CCU->BUS_CLK_GATING0 &= ~(e_bit | o_bit);
    H3_CCU->BUS_SOFT_RESET0  &= ~(e_bit | o_bit);
    H3_CCU->USBPHY_CFG &= ~(phy_rst | phy_cal | phy_clk);
    udelay(10000);

    H3_CCU->BUS_CLK_GATING0 |= e_bit | o_bit;
    H3_CCU->BUS_SOFT_RESET0  |= e_bit | o_bit;
    H3_CCU->USBPHY_CFG |= phy_rst | phy_cal | phy_clk;
    udelay(30000);

    // ICR — AHB burst config (INCR16/8/4), base+0x800
    uint32_t ehci_base = (port_num == 1) ? 0x01C1B000u : 0x01C1C000u;
    volatile uint32_t* icr = (volatile uint32_t*)(ehci_base + 0x800);
    icr[0] = 0x00000701u;
    icr[4] = 0;
}

int usb_ohci_init(uint32_t base) {
    ohci_regs_t* ohci = (ohci_regs_t*)base;
    extern int uart0_printf(const char* fmt, ...);

    uart0_printf("usb: base=0x%X rev=0x%X rha=0x%X\n",
                 base, ohci->rev, ohci->rha_des);

    // Диагностика D-cache: читаем SCTLR (bit 2 = D-cache enable)
    uint32_t sctlr;
    __asm volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(sctlr));
    uart0_printf("usb: SCTLR=0x%X %s\n", sctlr,
                 (sctlr & 4) ? "DCACHE=ON" : "DCACHE=OFF");

    usb_port_hw_init(base);

    if (ohci->rev != 0x10) { uart_puts("usb: OHCI dead\n"); return -1; }

    // ---- Init attach-режим: НЕ делаем HCR, чтобы не сбрасывать
    // head→current в 0 и не терять write-ability ctrlcur
    // (в этой реализации OHCI ctrlcur read-only после HCR).

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

    // Пустые списки, но ctrlcur НЕ трогаем — U-Boot что-то там держит
    ohci->bulkhead = 0;
    ohci->bulkcur  = 0;
    ohci->peried   = 0;

    // Не трогаем ctrlhead и ctrlcur — пусть остаются, как U-Boot оставил
    // (U-Boot после usb stop не обязательно их чистит)

    // OPER + CLE
    ohci->ctrl = (ohci->ctrl & ~(3u << 6)) | (2u << 6) | (1u << 4);
    udelay(1000);

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
    (void)ep_in;
    ohci_regs_t* ohci = (ohci_regs_t*)base;
    extern int uart0_printf(const char* fmt, ...);

    // Используем ED/TD своего порта — у каждого порта свой пул
    // (иначе прерывания/контроль одного порта ломали бы другой).
    int pi = ohci_idx(base);
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

    // ed уже висит в control list после init. Но HC закэшировал
    // его со SKIP=1 при загрузке head→current в init. Простое изменение
    // в памяти + cache_clean не заставляет HC перечитать ED.
    // Перезаписываем head + current при каждой передаче (как в рабочем
    // прототипе) — это гарантирует, что HC увидит новый ED.
    ohci->ctrlhead = (uint32_t)ed;
    ohci->ctrlcur  = (uint32_t)ed;
    ohci->cmdstatus = (1u << 1);   // CLF

    // Ждём, пока HC завершит передачу: проверяем CC последнего TD
    // (t_data для data_len=0 — это status phase; для data_len>0 — t_stat).
    ohci_td_t* last = (data_len > 0) ? t_stat : t_data;
    uint32_t elapsed = 0;
    while (1) {
        cache_invalidate((uint32_t)td, sizeof(g_td[0]));
        if ((last->cfg >> TD_CC_SHIFT) != TD_CC_NOTACC) break;
        udelay(1000);
        if (++elapsed > timeout_ms) {
            return -1;
        }
    }

    // Проверяем CC
    uint32_t cc;
    cc = t_setup->cfg >> TD_CC_SHIFT;
    if (cc != TD_CC_NOERR) { uart0_printf("usb: cc setup=%u addr=%u\n", cc, addr); return -1; }
    cc = t_data->cfg >> TD_CC_SHIFT;
    if (cc != TD_CC_NOERR) { uart0_printf("usb: cc data=%u addr=%u\n", cc, addr); return -1; }
    cc = t_stat->cfg >> TD_CC_SHIFT;
    if (cc != TD_CC_NOERR) { uart0_printf("usb: cc stat=%u addr=%u\n", cc, addr); return -1; }

    // IN: инвалидируем кэш для буфера, чтобы прочитать свежие данные (записанные DMA)
    if (data_len > 0 && dir_in) cache_invalidate((uint32_t)data, data_len);

    return (int)data_len;
}