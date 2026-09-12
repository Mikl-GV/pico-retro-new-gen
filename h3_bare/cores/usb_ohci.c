// usb_ohci.c — минимальный OHCI-драйвер для H3 (Full/Low-Speed USB).
// Покрывает: инициализацию контроллера, root hub, управляющие и прерывные передачи.
// Регистры OHCI: 0x01C1A400 (OHCI0), 0x01C1B400 (OHCI1), 0x01C1C400 (OHCI2), 0x01C1D400 (OHCI3).
// Orange Pi Lite использует OHCI1 (0x01C1B400) и OHCI2 (0x01C1C400) — физические USB-A порты.
// (Send Data Volume 2 + OpenHCI spec)
#include <string.h>
#include "h3.h"
#include "h3_ccu.h"
#include "uart.h"
#include "usb_ohci.h"

// ---- OHCI register block (HcRevision offset 0x00) ----
typedef struct {
    volatile uint32_t rev;           // 0x00
    volatile uint32_t ctrl;          // 0x04
    volatile uint32_t cmdstatus;     // 0x08
    volatile uint32_t intrstatus;    // 0x0C
    volatile uint32_t intrenable;    // 0x10
    volatile uint32_t intrdisable;   // 0x14
    volatile uint32_t hcca;          // 0x18
    volatile uint32_t peried;        // 0x1C
    volatile uint32_t ctrlhead;      // 0x20
    volatile uint32_t ctrlcur;       // 0x24
    volatile uint32_t bulkhead;      // 0x28
    volatile uint32_t bulkcur;       // 0x2C
    volatile uint32_t donehead;      // 0x30
    volatile uint32_t fminterval;    // 0x34
    volatile uint32_t fmremaining;   // 0x38
    volatile uint32_t fmnumber;      // 0x3C
    volatile uint32_t periodstart;   // 0x40
    volatile uint32_t lsperiod;      // 0x44
    volatile uint32_t rha_des;       // 0x48  Root Hub A (Descriptor)
    volatile uint32_t rhb_des;       // 0x4C  Root Hub B (Descriptor)
    volatile uint32_t rhstatus;      // 0x50  Root Hub Status
    volatile uint32_t rhport[2];     // 0x54-0x58  Root Hub Port Status
} ohci_regs_t;

// ---- OHCI control bits ----
#define OHCI_CTRL_CBSR_SHIFT  0       // Control-Bulk-Service Ratio
#define OHCI_CTRL_PLE         (1<<4)  // Periodic List Enable
#define OHCI_CTRL_IE          (1<<5)  // Isochronous Enable
#define OHCI_CTRL_CLE         (1<<6)  // Control List Enable
#define OHCI_CTRL_BLE         (1<<7)  // Bulk List Enable
#define OHCI_CTRL_HCFS_SHIFT  8
#define OHCI_CTRL_HCFS_RESET  (0<<8)
#define OHCI_CTRL_HCFS_OP    (2<<8)
#define OHCI_CTRL_RWE         (1<<9)  // Remote Wakeup Enable
#define OHCI_CTRL_HC_LPEN     (1<<10) // Legacy Power Enable (must clear)

// ---- CMDSTATUS (Host Controller Command & Status) ----
#define OHCI_CMD_HCR          (1<<0)  // Host Controller Reset
#define OHCI_CMD_CLF          (1<<1)  // Control List Filled
#define OHCI_CMD_BLF          (1<<2)  // Bulk List Filled
#define OHCI_CMD_OCR          (1<<3)  // Ownership Change Request
#define OHCI_CMD_SOC_SHIFT    16

// ---- Root Hub A descriptor ----
#define OHCI_RHA_NDP_SHIFT    0       // Number Downstream Ports
#define OHCI_RHA_PSM          (1<<8)  // Power Switching Mode
#define OHCI_RHA_NPS          (1<<9)  // No Power Switching
#define OHCI_RHA_DT           (1<<10) // Device Type
#define OHCI_RHA_OCPM         (1<<11) // Overcurrent Protection Mode
#define OHCI_RHA_NOCP         (1<<12) // No Overcurrent Protection
#define OHCI_RHA_POTPG_SHIFT  24      // Power On to Power Good Time

// ---- Root Hub Port Status bits (OHCI ver) ----
#define RHPS_CCS              (1<<0)  // Current Connect Status
#define RHPS_PSS              (1<<1)  // Port Enable Status
#define RHPS_LSDA             (1<<9)  // Low-Speed Device Attached
#define RHPS_CSC              (1<<16) // Connect Status Change
#define RHPS_PESC             (1<<17) // Port Enable Status Change

// ---- OHCI memory structures ----
typedef struct __attribute__((packed)) {
    uint32_t cfg;     // Endpoint Descriptor (ED) config
    uint32_t tail;    // Tail Pointer to TD
    uint32_t head;    // Head Pointer to TD
    uint32_t next;    // Next ED
} ohci_ed_t;

typedef struct __attribute__((packed)) {
    uint32_t cfg;     // Transfer Descriptor (TD) config
    uint32_t cbp;     // Current Buffer Pointer (or 0 if done)
    uint32_t next;    // Next TD
    uint32_t be;      // Buffer End
} ohci_td_t;

// ED config bits
#define ED_CONFIG_SKIP    (1<<14)
#define ED_CONFIG_DIR_IN  (0<<11)   // dir=0: OUT, dir=1: IN, dir=2: from TD
#define ED_CONFIG_DIR_OUT (1<<11)
#define ED_CONFIG_DIR_TD  (2<<11)
#define ED_CONFIG_ADDR_SHIFT 16
#define ED_CONFIG_EN_SHIFT   7
#define ED_CONFIG_F_SHIFT    4
#define ED_CONFIG_S          (1<<15) // Speed (0=FS, 1=LS)

// TD config bits
#define TD_CONFIG_R         (1<<24)   // Toggle: 0=DATA0, 1=DATA1
#define TD_CONFIG_DP_SHIFT  19        // DataPID: 0=SETUP, 1=OUT, 2=IN
#define TD_CONFIG_DP_SETUP  (0<<19)
#define TD_CONFIG_DP_OUT    (1<<19)
#define TD_CONFIG_DP_IN     (2<<19)
#define TD_CONFIG_T_SHIFT   24        // buffer routing
#define TD_CONFIG_CC        0xF0000000 // Condition Code (top 4 bits)

// ---- GCC (USB-CCU) bits ----
#define USB_PHY_GATE   (1<<1) // Reset deassert
#define PLL_GATE       (1<<8)

// ---- HCCA: 256-byte aligned ----
typedef struct __attribute__((packed)) {
    uint32_t intr[32];      // Interrupt table (32 entries)
    uint16_t framenumber;   // Current frame number
    uint16_t pad;           // pad
    uint32_t donehead;      // Done queue head
    uint8_t  reserved[120];
} ohci_hcca_t;

// Глобальные буферы (доступны из любого адреса)
static ohci_hcca_t g_hcca __attribute__((aligned(256)));
static ohci_ed_t   g_ed[8];    // до 8 устройств
static ohci_td_t   g_td[32];
static int g_ed_idx = 0, g_td_idx = 0;

static ohci_regs_t* ohci = 0;

// Сброс внутренних очередей
static void reset_descs(void) {
    memset(&g_ed, 0, sizeof(g_ed));
    memset(&g_td, 0, sizeof(g_td));
    g_ed_idx = 0;
    g_td_idx = 0;
}

// Выделить ED (для устройства)
static ohci_ed_t* alloc_ed(void) {
    if (g_ed_idx >= 8) return 0;
    ohci_ed_t* e = &g_ed[g_ed_idx++];
    memset(e, 0, sizeof(*e));
    return e;
}

// Выделить TD (для передачи)
static ohci_td_t* alloc_td(void) {
    if (g_td_idx >= 32) return 0;
    ohci_td_t* t = &g_td[g_td_idx++];
    memset(t, 0, sizeof(*t));
    return t;
}

int usb_ohci_init(uint32_t base) {
    ohci = (ohci_regs_t*)base;

    /* Диагностика: состояние контроллера после U-Boot */
    extern int uart0_printf(const char* fmt, ...);
    uart0_printf("usb: base=0x%X rev=0x%X ctrl=0x%X cmd=0x%X rha=0x%X\n",
                 base, ohci->rev, ohci->ctrl, ohci->cmdstatus, ohci->rha_des);

    /* Включаем такты и снимаем reset для этого порта:
     * H3: OHCI gate bits 28-31 в BUS_CLK_GATING0, EHCI gate bits 24-27 там же.
     * Нам достаточно OHCI, но для связки включаем и EHCI (общий AHB). */
    int port_idx = (base == OHCI1_BASE) ? 1 : (base == OHCI2_BASE) ? 2 :
                   (base == 0x01C1A400) ? 0 : 3;
    /* OHCI gate = 28 + port_idx */
    H3_CCU->BUS_CLK_GATING0 |= (1u << (28 + port_idx));
    H3_CCU->BUS_SOFT_RESET0  |= (1u << (28 + port_idx));
    udelay(2000);

    /* НЕ делаем HCR — он убивает Root Hub.
     * Просто переводим в Operational. */
    /* 1. HCCA */
    ohci->hcca = (uint32_t)&g_hcca;
    /* 2. Перевод в Operational (HCFS = 2) */
    uint32_t ctrl = ohci->ctrl;
    ctrl &= ~OHCI_CTRL_HC_LPEN;         /* снять Legacy Power Enable */
    ctrl &= ~(3u << 8);                 /* очистить HCFS */
    ctrl |= OHCI_CTRL_HCFS_OP;          /* Operational */
    ohci->ctrl = ctrl;
    /* 3. Включить списки */
    ohci->ctrl |= OHCI_CTRL_PLE | OHCI_CTRL_CLE;
    /* 4. Root hub: снять события */
    ohci->rhstatus = 0;
    /* 5. Поднять питание портов */
    int n_ports = ohci->rha_des & 0xFF;
    if (n_ports == 0 || n_ports > 4) n_ports = 1;
    for (int p = 0; p < n_ports; p++) {
        ohci->rhport[p] = (ohci->rhport[p] & ~1) | 1;  /* SetPower */
        udelay(2000);
    }
    for (int p = 0; p < n_ports; p++) {
        uart0_printf("usb: port%d status=0x%X\n", p, ohci->rhport[p]);
    }

    uart_puts("usb: ohci "); uart_putc('0' + n_ports); uart_puts(" port(s) @ "); uart_puts(base == OHCI1_BASE ? "1" : (base == OHCI2_BASE ? "2" : "?"));
    uart_puts("\n");
    return 0;
}

int usb_ohci_root_port_connected(uint32_t base, int port) {
    ohci_regs_t* r = (ohci_regs_t*)base;
    uint32_t s = r->rhport[port];
    return (s & RHPS_CCS) ? 1 : 0;
}

int usb_ohci_port_low_speed(uint32_t base, int port) {
    ohci_regs_t* r = (ohci_regs_t*)base;
    uint32_t s = r->rhport[port];
    return (s & RHPS_LSDA) ? 1 : 0;
}

// Простая управляющая передача на OHCI:
// alloc ED -> alloc TD(s) -> загоняем в список управления -> ждём done -> разбираем
int usb_ohci_ctrl_transfer(uint32_t base, uint8_t addr, uint8_t ep_in,
                           const uint8_t* setup, uint8_t setup_len,
                           uint8_t* data, uint32_t data_len, int dir_in,
                           uint32_t timeout_ms) {
    ohci = (ohci_regs_t*)base;
    if (!ohci) return -1;

    reset_descs();

    // ED для endpoint 0 (default control)
    ohci_ed_t* ed = alloc_ed();
    if (!ed) return -1;

    uint32_t ed_cfg = (addr << ED_CONFIG_ADDR_SHIFT) |
                       (0 << ED_CONFIG_EN_SHIFT) |       // endpoint 0
                       (0 << ED_CONFIG_F_SHIFT) |
                       ED_CONFIG_SKIP;                    // start skipped
    // Если LS — ставим S=1
    if (usb_ohci_port_low_speed(0, 0))
        ed_cfg |= ED_CONFIG_S;

    ed->cfg = ed_cfg;
    ed->head = 1; // Tail=1 означает, что очередь пуста (OHCI convention)

    // TD: SETUP
    ohci_td_t* td_setup = alloc_td();
    td_setup->cfg = TD_CONFIG_DP_SETUP;
    td_setup->cbp = (uint32_t)setup;
    td_setup->be  = (uint32_t)(setup + setup_len - 1);

    if (data_len > 0 && !dir_in) {
        // TD: DATA0 OUT
        ohci_td_t* td_data = alloc_td();
        td_data->cfg = TD_CONFIG_DP_OUT;
        td_data->cbp = (uint32_t)data;
        td_data->be  = (uint32_t)(data + data_len - 1);
        td_setup->next = (uint32_t)td_data;
    } else if (data_len > 0) {
        // TD: DATA1 IN
        ohci_td_t* td_data = alloc_td();
        td_data->cfg = TD_CONFIG_R | TD_CONFIG_DP_IN;
        td_data->cbp = (uint32_t)data;
        td_data->be  = (uint32_t)(data + data_len - 1);
        td_setup->next = (uint32_t)td_data;
    }

    // STATUS TD
    ohci_td_t* td_status = alloc_td();
    td_status->cfg = (dir_in ? TD_CONFIG_DP_OUT : TD_CONFIG_DP_IN);
    td_status->cbp = 0;
    td_status->be  = 0;

    // Цепочка: td_setup -> td_data? -> td_status
    // td_status не имеет next
    if (data_len > 0) {
        // последний td из data/data_in указывает на статус
        ohci_td_t* last = (data_len > 0) ? (td_setup->next ? (ohci_td_t*)td_setup->next : td_setup) : td_setup;
        if (td_setup->next) ((ohci_td_t*)td_setup->next)->next = (uint32_t)td_status;
        else td_setup->next = (uint32_t)td_status;
    } else {
        td_setup->next = (uint32_t)td_status;
    }

    // ED head = TD setup
    ed->head = (uint32_t)td_setup;
    ed->tail = (uint32_t)td_status; // tail указывает на последний TD (статус)

    // Снимаем SKIP, вешаем ED в управляющий список
    ed->cfg &= ~ED_CONFIG_SKIP;

    // Ставим ED в HC
    ohci->ctrlhead = (uint32_t)ed;
    ohci->ctrlcur   = (uint32_t)ed;
    ohci->cmdstatus = OHCI_CMD_CLF;  // Control List Filled

    // Ждём выполнения
    uint32_t start = ~H3_HS_TIMER->CURNT_LO;
    while (1) {
        if (ohci->ctrlcur == 0) {
            // ED завершён
            break;
        }
        if ((~(start - H3_HS_TIMER->CURNT_LO) / 100) > timeout_ms * 1000) {
            return -1; // timeout
        }
    }

    // Смотрим condition code первого TD (setup)
    uint32_t cc = td_setup->cfg >> 28;
    if (cc != 0) return -(int)cc;

    if (data_len > 0 && dir_in) {
        cc = ((ohci_td_t*)td_setup->next)->cfg >> 28;
        if (cc != 0) return -(int)cc;
    }

    return (int)data_len;
}