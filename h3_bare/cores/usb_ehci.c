// usb_ehci.c — минимальный EHCI host controller driver для H3.
// Без split-transaction: работает только с HighSpeed-устройствами.
// USB HID клавиатуры обычно FullSpeed (1.1) — но через chipidea root hub
// (HS) FullSpeed-устройства уходят в companion OHCI. EHCI сам не обслуживает FS.
// Поэтому этот файл — каркас: инициализация + диагностика, контрольные передачи.
#include "h3.h"
#include "h3_ccu.h"
#include "uart.h"
#include "usb_ehci.h"

// ---- EHCI registers (Host Controller Capability/Operational) ----
typedef struct {
    volatile uint32_t caplen_hi_version;   // 0x00 HCIVERSION + CAPLENGTH in [0:7]
    volatile uint32_t hcsparams;           // 0x04
    volatile uint32_t hccparams;           // 0x08
    volatile uint32_t reserved0[4];        // 0x0C-0x18
} ehci_cap_t;

typedef struct {
    volatile uint32_t cmd;        // 0x00 USBCMD
    volatile uint32_t status;     // 0x04 USBSTS
    volatile uint32_t intr;       // 0x08 USBINTR
    volatile uint32_t frindex;    // 0x0C FRINDEX
    volatile uint32_t ctrldsseg;  // 0x10
    volatile uint32_t periodic;   // 0x14
    volatile uint32_t async;      // 0x18 ASYNCLISTADDR
    volatile uint32_t res1[9];    // 0x1C-0x3C
    volatile uint32_t configflag; // 0x40 CONFIGFLAG
    volatile uint32_t portsc[4];  // 0x44-0x50 PORTSC0-3
} ehci_op_t;

#define CMD_RUN      (1<<0)
#define CMD_HCRESET  (1<<1)
#define CMD_ASYNC_EN (1<<5)
#define CMD_ASYNC_SW (1<<15)
#define CMD_HOST_RUN (CMD_RUN | CMD_ASYNC_EN)

#define STS_HCHALTED  (1<<0)

#define PORTCCS   (1<<0)
#define PORTPEC   (1<<3)
#define PORT_PP   (1<<12)
#define PORT_PSPEED_MSK (3<<10)
#define PORT_PSPEED_HS  (1<<10)   /* bit[11:10]=01 => High Speed */
#define PORT_PSPEED_LS  (3<<10)   /* bit[11:10]=11 => Low Speed */
#define PORT_PSPEED_FS  (2<<10)   /* bit[11:10]=10 => Full Speed */

typedef struct __attribute__((packed)) {
    uint32_t next;        // bits [31:5] pointer, [1:0] type (0=ITD,1=QH,2=SITD,3=FSTN)
    uint32_t token;       // status [31:24] + pid [19:16] + ep [15:8] + addr [6:0]
    uint32_t buff[5];     // буферы (page pointers)
    uint32_t ext;         // Extended buffer pointers (not used)
} ehci_qtd_t;

typedef struct __attribute__((packed)) {
    uint32_t hptr;        // horizontal link pointer + type
    uint32_t epchar;      // endpoint characteristics
    uint32_t epcap;       // endpoint capabilities
    uint32_t currqtd;     // overlay current qtd
    uint32_t nextqtd;     // overlay next qtd
    uint32_t altqtd;      // overlay alt qtd
    uint32_t token;       // overlay token
    uint32_t buff[5];     // overlay buffer
    uint32_t ext;
} ehci_qh_t;

static ehci_cap_t*  cap;
static ehci_op_t*   op;
static uint32_t     g_addr;
static uint32_t     g_ehci_base;
static int          g_initialized = 0;

int usb_ehci_init(uint32_t base) {
    g_ehci_base = base;
    cap = (ehci_cap_t*)base;
    uint32_t caplen = cap->caplen_hi_version & 0xFF;
    op = (ehci_op_t*)(base + caplen);

    // Diag
    extern int uart0_printf(const char* fmt, ...);
    uart0_printf("ehci: cap=0x%X caplen=%u hcs=0x%X hcc=0x%X\n",
                 base, caplen, cap->hcsparams, cap->hccparams);

    // Инициализация: Reset controller
    op->cmd = CMD_HCRESET;
    uint32_t tmo = 100000;
    while ((op->cmd & CMD_HCRESET) && tmo--) {}
    uart0_printf("ehci: reset done, status=0x%X\n", op->status);

    // ConfigureTiming: отключаем async, включаем run
    op->cmd = CMD_HOST_RUN;
    // Set CONFIGFLAG (route ports to this HC)
    op->configflag = 1;
    udelay(10000);

    // Включаем питание портов
    int n = (cap->hcsparams & 0xF) + 1;  // N_PORTS
    if (n > 4) n = 4;
    for (int p = 0; p < n; p++) {
        op->portsc[p] |= PORT_PP;
        udelay(5000);
        uart0_printf("ehci: port%d portsc=0x%X\n", p, op->portsc[p]);
    }

    g_initialized = 1;
    uart_puts("ehci: ready\n");
    return 0;
}

int usb_ehci_dev_connected(uint32_t base) {
    (void)base;
    if (!op) return 0;
    int n = (cap->hcsparams & 0xF);
    if (n > 3) n = 3;
    for (int p = 0; p <= n; p++) {
        if (op->portsc[p] & PORTCCS) return 1;
    }
    return 0;
}

// ---- EHCI control transfer via async QH ----
// Мини-реализация: создаёт QH + QTDs для setup/data/status
static int ehci_async_transfer(uint8_t addr, uint32_t ep, const uint8_t* setup,
                               uint8_t* data, int data_len, int dir_in,
                               uint32_t timeout_ms) {
    // выделяем память под QH и 3 QTd (выравнивание 32 для qh, 32 для qtd)
    static ehci_qh_t qh __attribute__((aligned(32)));
    static ehci_qtd_t td_setup __attribute__((aligned(32)));
    static ehci_qtd_t td_data  __attribute__((aligned(32)));
    static ehci_qtd_t td_stat  __attribute__((aligned(32)));

    // чекаем что аллокации где-то в драме; память один раз
    // (это упрощённо, на практике нужно выделение)

    // ---- Build QH ----
    // endpoint characteristics: addr=6:0, ep=15:8, eps=12:11 (0=CTRL),
    // dtc=14, h=15, maxpkt=16:26, rl=30:28
    // для ep0 maxpkt=8 обычно (после чтения дескриптора 8-64)
    uint32_t maxpkt = 8; // default, подстраивается из дескриптора
    uint32_t epchar = (addr & 0x7F)
                    | ((ep & 0xF) << 8)
                    | (0 << 12)       // eps=0 control
                    | (0 << 14)       // dtc
                    | (0 << 15)       // H=0 (head of reclamation)
                    | ((maxpkt & 0x7FF) << 16)   // max pkt 0x800 ок
                    | (1 << 28);      // RL

    qh.hptr = 0;                 // last in list
    qh.epchar = epchar;
    qh.epcap = 1;                // nak counter reload = 1
    qh.currqtd = 0;
    qh.nextqtd = 0;
    qh.altqtd = 0;
    qh.token = (1 << 30);        // halt it initially? no - bit 30 = HALT on overlay

    // ---- Build QTds ----
    td_setup.next = (uint32_t)&td_data;
    td_setup.token = (0 << 31)  // active
                   | (0 << 30)  // halt=0
                   | (0 << 28)  // buffer err
                   | (8 << 16)  // PID: 00 setup? actually 2 bits at 19:16: 00=setup? 
                   ...
    return -1;
}

int usb_ehci_ctrl(uint32_t base, uint8_t addr, uint8_t ep,
                  const uint8_t* setup, uint8_t setup_len,
                  uint8_t* data, uint32_t data_len, int dir_in,
                  uint32_t timeout_ms) {
    (void)base; (void)addr; (void)ep; (void)setup; (void)setup_len;
    (void)data; (void)data_len; (void)dir_in; (void)timeout_ms;
    return -1; // TODO: full impl
}