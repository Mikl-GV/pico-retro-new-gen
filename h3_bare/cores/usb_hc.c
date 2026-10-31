// usb_hc.c — EHCI-путь для USB High-Speed флешки (H3, Orange Pi Lite).
// r788: порт из эталона Bare_linux (src/usb_hc.c — EHCI-часть).
//
// Логика: PHY1 (EHCI 0x01C1B000 / OHCI 0x01C1B400) — High-Speed флешка через
// EHCI (OHCI HS не ведёт). PHY2 (0x01C1C000/0x01C1C400) — клавиатура через
// наш OHCI (usb_kbd.c) — ЭТОТ ФАЙЛ ЕЁ НЕ ТРОГАЕТ.
//
// Делаем: EHCI сброс/run, enum HS-устройства (GET_DESC/SET_ADDR/SET_CONFIG),
// поиск MSD-интерфейса, активация usb_msd, чтение MBR-сигнатуры. После
// активации main.c подключает флешку к FAT через fat_set_block_source.
//
// В отличие от Bare_linux, у нас НЕ отдельный фоновый OHCI-цикл: OHCI-порты
// инит'ятся usb_kbd_init (для клавиатуры). EHCI-порт — единственный путь
// для HS-флешки. FS/LS-флешка (если встретится) — через OHCI-bulk (наш
// usb_ohci), подключается отдельно (см. OHCI-bulk).
#include <stdint.h>
#include <string.h>
#include "h3.h"
#include "h3_ccu.h"
#include "uart.h"
#include "usb_msd.h"

extern int printf(const char* fmt, ...);

// ---- Базы (H3, см. usb.h эталона) ----
#define USB_EHCI_BASE1  0x01C1B000u   // PHY1: HS-флешка (EHCI)
#define USB_EHCI_OP1    (USB_EHCI_BASE1 + 0x10u)  // caplen у EHCI обычно 0x10

// State: адрес текущего устройства, MPS.
static uint8_t  g_usb_addr = 0;

// r788: полный enum FS/LS-флешки через OHCI (определена ниже).
int usb_ohci_enumerate_flash(void);

// ---- EHCI структуры (qH/qTD), в .coherent ----
struct ehci_qh {
    uint32_t qh_link;
    uint32_t qh_endpt;
    uint32_t qh_cap;
    uint32_t qh_cur;
    uint32_t qh_next;
    uint32_t qh_alt;
    uint32_t qh_token;
    uint32_t qh_buf[5];
} __attribute__((aligned(32)));

struct ehci_qtd {
    uint32_t qt_next;
    uint32_t qt_alt;
    uint32_t qt_token;
    uint32_t qt_buf[5];
} __attribute__((aligned(32)));

#define EHCI_QTD_PID_OUT   0u
#define EHCI_QTD_PID_IN    1u
#define EHCI_QTD_PID_SETUP 2u
#define EHCI_QTD_ACTIVE    (1u << 7)
#define EHCI_QTD_ERR_MASK  (EHCI_QTD_ACTIVE | 0x7Fu)
#define EHCI_QH_EPS_HS     (2u << 12)
#define EHCI_QH_C          (1u << 27)
#define EHCI_QH_MAXP(m)    ((uint32_t)(m) << 16)
#define EHCI_QH_EPN(n)     (((uint32_t)((n) & 0x0F)) << 8)

static struct ehci_qh  g_qh  __attribute__((section(".coherent"), aligned(32)));
static struct ehci_qtd g_qtd[4] __attribute__((section(".coherent"), aligned(32)));
static uint8_t g_ehci_buf[512] __attribute__((section(".coherent"), aligned(32)));
static uint8_t g_setup[8] __attribute__((section(".coherent"), aligned(32)));

static inline volatile uint32_t* ehci_reg(uint32_t off) {
    return (volatile uint32_t*)(uintptr_t)(USB_EHCI_OP1 + off);
}
static inline void ehci_wr(uint32_t off, uint32_t v) { *ehci_reg(off) = v; __asm volatile("dsb" ::: "memory"); }
static inline uint32_t ehci_rd(uint32_t off) { return *ehci_reg(off); }

static int usb_ehci_hc_reset(void) {
    // Остановить: RS=0, ждём HCHalted.
    ehci_wr(0x00, ehci_rd(0x00) & ~(1u << 0));
    for (uint32_t t = 0; t < 0xFFFFFu; t++)
        if (ehci_rd(0x04) & (1u << 12)) break;
    // HCRESET.
    ehci_wr(0x00, 1u << 1);
    for (uint32_t t = 0; t < 0xFFFFFu; t++)
        if (!(ehci_rd(0x00) & (1u << 1))) return 0;
    return -1;
}

static void ehci_qtd_fill(struct ehci_qtd* q, const uint8_t* buf, uint32_t len,
                          uint32_t pid, uint32_t next) {
    q->qt_next = next;
    q->qt_alt  = 1;   /* alt terminate */
    q->qt_token = (len << 16) | (3u << 12) | (pid << 10) | EHCI_QTD_ACTIVE;
    for (int i = 0; i < 5; i++) q->qt_buf[i] = 0;
    if (buf && len) {
        uintptr_t a = (uintptr_t)buf;
        q->qt_buf[0] = (uint32_t)a;
        uintptr_t end = a + len - 1;
        uintptr_t page = (a + 0xFFFu) & ~0xFFFu;
        for (int i = 1; i < 5 && page <= end; i++, page += 0x1000u)
            q->qt_buf[i] = (uint32_t)page;
    }
}

static int ehci_wait_done(struct ehci_qtd* t, int n) {
    ehci_wr(0x18, (uint32_t)(uintptr_t)&g_qh);   /* ASYNCLISTADDR */
    ehci_wr(0x00, ehci_rd(0x00) | (1u << 5) | (1u << 0));   /* ASE=1, RS=1 */
    for (uint32_t spin = 0; spin < 0xFFFFFu; spin++) {
        int done = 1;
        for (int i = 0; i < n; i++)
            if (t[i].qt_token & EHCI_QTD_ACTIVE) { done = 0; break; }
        if (done) break;
    }
    ehci_wr(0x00, ehci_rd(0x00) & ~(1u << 5));   /* ASE=0 */
    for (int i = 0; i < n; i++)
        if (t[i].qt_token & EHCI_QTD_ERR_MASK) return -1;
    return 0;
}

static int ehci_run_chain(struct ehci_qtd* t, int n) {
    g_qh.qh_cur   = (uint32_t)(uintptr_t)&t[0];
    g_qh.qh_next  = 0;
    g_qh.qh_token = 0;
    __asm volatile("dmb sy" ::: "memory");
    return ehci_wait_done(t, n);
}

static int usb_ehci_ctrl_mps(const uint8_t* setup, const uint8_t* out_data,
                             uint32_t out_len, uint8_t* in_buf, uint32_t in_len,
                             uint16_t mps) {
    if (!setup) return -1;
    struct ehci_qh* qh = &g_qh;
    struct ehci_qtd* t = g_qtd;

    qh->qh_link  = 1;
    qh->qh_endpt = (uint32_t)(g_usb_addr & 0x7Fu)
                 | EHCI_QH_EPN(0)
                 | EHCI_QH_EPS_HS
                 | EHCI_QH_C
                 | EHCI_QH_MAXP(mps);
    qh->qh_cap   = 0;
    qh->qh_alt   = 1;
    qh->qh_token = 0;
    for (int i = 0; i < 5; i++) qh->qh_buf[i] = 0;

    for (int i = 0; i < 8; i++) g_setup[i] = setup[i];
    ehci_qtd_fill(&t[0], g_setup, 8, EHCI_QTD_PID_SETUP, 0);
    t[0].qt_next = (uint32_t)(uintptr_t)&t[1];

    if (in_len) {
        ehci_qtd_fill(&t[1], in_buf, in_len, EHCI_QTD_PID_IN, 0);
        t[1].qt_next = (uint32_t)(uintptr_t)&t[2];
        ehci_qtd_fill(&t[2], 0, 0, EHCI_QTD_PID_OUT, 1);
        return ehci_run_chain(t, 3);
    }
    if (out_data) {
        for (uint32_t i = 0; i < out_len && i < sizeof(g_ehci_buf); i++)
            g_ehci_buf[i] = out_data[i];
        if (out_len > sizeof(g_ehci_buf)) return -1;
        ehci_qtd_fill(&t[1], g_ehci_buf, out_len, EHCI_QTD_PID_OUT, 0);
        t[1].qt_next = (uint32_t)(uintptr_t)&t[2];
        ehci_qtd_fill(&t[2], 0, 0, EHCI_QTD_PID_IN, 1);
        return ehci_run_chain(t, 3);
    }
    ehci_qtd_fill(&t[1], 0, 0, EHCI_QTD_PID_IN, 1);
    return ehci_run_chain(t, 2);
}

int usb_ehci_ctrl(const uint8_t* setup, const uint8_t* out_data, uint32_t out_len,
                  uint8_t* in_buf, uint32_t in_len) {
    return usb_ehci_ctrl_mps(setup, out_data, out_len, in_buf, in_len, 64);
}

static int usb_ehci_bulk_out(uint8_t ep, uint16_t mps, const uint8_t* data, uint32_t len) {
    if (len > sizeof(g_ehci_buf)) return -1;
    for (uint32_t i = 0; i < len; i++) g_ehci_buf[i] = data[i];
    g_qh.qh_link  = 1;
    g_qh.qh_endpt = (uint32_t)(g_usb_addr & 0x7Fu)
                  | EHCI_QH_EPN(ep) | EHCI_QH_EPS_HS | EHCI_QH_MAXP(mps);
    g_qh.qh_cap = g_qh.qh_alt = 1;
    g_qh.qh_token = 0;
    for (int i = 0; i < 5; i++) g_qh.qh_buf[i] = 0;
    ehci_qtd_fill(&g_qtd[0], g_ehci_buf, len, EHCI_QTD_PID_OUT, 1);
    return ehci_run_chain(g_qtd, 1);
}

static int usb_ehci_bulk_in(uint8_t ep, uint16_t mps, uint8_t* data, uint32_t len) {
    if (len > 20u * 1024u) return -1;
    g_qh.qh_link  = 1;
    g_qh.qh_endpt = (uint32_t)(g_usb_addr & 0x7Fu)
                  | EHCI_QH_EPN(ep) | EHCI_QH_EPS_HS | EHCI_QH_MAXP(mps);
    g_qh.qh_cap = g_qh.qh_alt = 1;
    g_qh.qh_token = 0;
    for (int i = 0; i < 5; i++) g_qh.qh_buf[i] = 0;
    ehci_qtd_fill(&g_qtd[0], data, len, EHCI_QTD_PID_IN, 1);
    return ehci_run_chain(g_qtd, 1);
}

// Bullk-маршрут выбирается глобально: EHCI (HS) или OHCI (FS/LS). Ставится
// usb_hc_init_bulk (для EHCI) или OHCI-bulk-инициализацией.
static int g_bulk_via_ohci = 0;

void usb_hc_set_bulk_ohci(void);   // определён ниже

/* ---- r856: однократная энумерация MSD ----
 * HID-скан (usb_kbd.c:enum_port) уже прочитал config-дескриптор флешки и
 * оставил её адресованной (addr=1, без SET_CONFIGURATION). Повторная
 * энумерация после EHCI→OHCI handover падала на 18-байтном GET_DESCRIPTOR
 * (cc=0/8/15). Здесь используем уже прочитанный конфиг: только
 * SET_CONFIGURATION + MSD. */
static uint8_t  g_pre_cfg[256] __attribute__((section(".coherent"), aligned(32)));
static uint32_t g_pre_cfg_len;
static uint16_t g_pre_mps = 8;
static int      g_pre_msd;
/* r903: база OHCI, с которой HID-скан уже прочитал конфиг MSD (PHY1=B400
   или PHY2=C400). Раньше жёстко B400 — флешка на PHY2 теряла pre-enum и
   уходила в падающий re-enum. */
static uint32_t g_pre_base = 0x01C1B400u;

void usb_hc_set_preenum_msd(const uint8_t* cfg, uint32_t len, uint16_t mps,
                            uint32_t base) {
    if (!cfg || len < 9 || len > sizeof(g_pre_cfg)) return;
    if (base != 0x01C1B400u && base != 0x01C1C400u) return;
    memcpy(g_pre_cfg, cfg, len);
    g_pre_cfg_len = len;
    g_pre_mps = mps ? mps : 8;
    g_pre_base = base;
    g_pre_msd = 1;
    printf("usb: MSD pre-enumerated (cfg=%u mps=%u base=0x%X)\n",
           (unsigned)len, (unsigned)g_pre_mps, (unsigned)g_pre_base);
}

static int usb_ohci_activate_flash_pre(void) {
    extern int usb_ohci_ctrl_transfer(uint32_t base, uint8_t addr, uint8_t ep_in,
                                      const uint8_t* setup, uint8_t setup_len,
                                      uint8_t* data, uint32_t data_len, int dir_in,
                                      uint32_t timeout_ms);
    extern void usb_ohci_set_mps(uint16_t mps);
    extern int usb_ohci_bulk_toggle_reset(uint32_t base);
    extern void usb_ohci_bot_recover(uint32_t base);
    const uint32_t base = g_pre_base;   /* r903: база из HID-скана (B400/C400) */
    static uint8_t mbr[512] __attribute__((section(".coherent"), aligned(32)));
    static uint8_t inq[36] __attribute__((section(".coherent"), aligned(32)));
    static struct usb_msd_dev mdev;
    uint8_t setup[8];

    usb_ohci_set_mps(g_pre_mps);
    setup[0]=0x00; setup[1]=0x09; setup[2]=0x01; setup[3]=0x00;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x00; setup[7]=0x00;
    if (usb_ohci_ctrl_transfer(base, 1, 0, setup, 8, 0, 0, 0, 1000) < 0) {
        printf("usb: pre-enum SET_CONFIGURATION fail\n");
        return -1;
    }
    g_usb_addr = 1;
    udelay(20000);

    if (usb_msd_init(&mdev, g_pre_cfg, g_pre_cfg_len) != 0) {
        printf("usb: pre-enum no MSD interface\n");
        return -1;
    }
    usb_hc_set_bulk_ohci();
    usb_ohci_bulk_toggle_reset(base);
    if (usb_msd_activate(&mdev) != 0) {
        printf("usb: pre-enum activate fail\n");
        return -1;
    }
    usb_ohci_bot_recover(base);

    if (usb_msd_inquiry(&mdev, inq) == 0) {
        printf("usb: pre-enum INQUIRY: ");
        for (int i = 8; i < 32 && inq[i]; i++) {
            char c = (char)inq[i];
            if (c < 0x20 || c > 0x7E) c = '.';
            printf("%c", c);
        }
        printf("\n");
    }
    uint32_t last = 0, blk = 0;
    if (usb_msd_wait_ready(&mdev, 3) != 0) printf("usb: pre-enum TUR fail\n");
    if (usb_msd_read_capacity(&mdev, &last, &blk) == 0)
        printf("usb: pre-enum block=%u last_lba=%u\n", (unsigned)blk, (unsigned)last);
    else
        printf("usb: pre-enum READ CAPACITY fail\n");
    if (usb_msd_block_read(0, mbr) != 0) {
        printf("usb: pre-enum MBR read fail\n");
        return -1;
    }
    printf("usb: pre-enum MBR sig=0x%04X — flash OK\n", mbr[510] | (mbr[511] << 8));
    return 0;
}

int usb_bulk_out(uint8_t ep, uint16_t mps, const uint8_t* data, uint32_t len) {
    if (g_bulk_via_ohci) {
        extern int usb_ohci_bulk_out(uint32_t base, uint8_t addr, uint8_t ep,
                                     const uint8_t* data, uint32_t len, uint16_t mps);
        return usb_ohci_bulk_out(g_pre_base, g_usb_addr, ep, data, len, mps);
    }
    return usb_ehci_bulk_out(ep, mps, data, len);
}

int usb_bulk_in(uint8_t ep, uint16_t mps, uint8_t* data, uint32_t len) {
    if (g_bulk_via_ohci) {
        extern int usb_ohci_bulk_in(uint32_t base, uint8_t addr, uint8_t ep,
                                    uint8_t* data, uint32_t len, uint16_t mps);
        return usb_ohci_bulk_in(g_pre_base, g_usb_addr, ep, data, len, mps);
    }
    return usb_ehci_bulk_in(ep, mps, data, len);
}

void usb_hc_bot_recover(void) {
    // BOT Reset Recovery: Bulk-Only Reset (0xFF) + CLEAR_FEATURE(HALT) на оба EP.
    // Устройство: адрес g_usb_addr. Для EHCI — control-передачи.
    uint8_t s[8];
    if (g_bulk_via_ohci) {
        extern void usb_ohci_bot_recover(uint32_t base);

	usb_ohci_bot_recover(g_pre_base);
        return;
    }
    /* Bulk-Only Mass Storage Reset: bmRequestType=0x21, bRequest=0xFF */
    s[0]=0x21; s[1]=0xFF; s[2]=0; s[3]=0; s[4]=0; s[5]=0; s[6]=0; s[7]=0;
    usb_ehci_ctrl(s, 0, 0, 0, 0);
    /* GET_MAX_LUN: 0xA1 0xFE */
    uint8_t lun = 0;
    s[0]=0xA1; s[1]=0xFE; s[2]=0; s[3]=0; s[4]=0; s[5]=0; s[6]=1; s[7]=0;
    usb_ehci_ctrl(s, 0, 0, &lun, 1);
    /* CLEAR_FEATURE(HALT) на bulk-EP (номера знает usb_msd — берём из dev) */
    extern uint8_t usb_msd_eps(uint8_t* in, uint8_t* out);
    uint8_t in_ep, out_ep;
    if (usb_msd_eps(&in_ep, &out_ep) == 0) {
        s[0]=0x02; s[1]=0x01; s[2]=0; s[3]=0; s[4]=out_ep; s[5]=0; s[6]=0; s[7]=0;
        usb_ehci_ctrl(s, 0, 0, 0, 0);
        s[4]=in_ep | 0x80;
        usb_ehci_ctrl(s, 0, 0, 0, 0);
    }
}

// ---- Порт и enum HS-устройства ----
static int usb_ehci_port_reset(void) {
    uint32_t ps = ehci_rd(0x44);
    if (!(ps & (1u << 0))) return -1;   /* CCS */
    ehci_wr(0x44, (1u << 12));                    /* PP=1 */
    for (uint32_t t = 0; t < 0xFFFFu; t++) { if (ehci_rd(0x44) & (1u << 0)) break; udelay(100); }
    ehci_wr(0x44, (1u << 12) | (1u << 8));        /* PP | PR */
    for (uint32_t t = 0; t < 0xFFFFFu; t++) { if (ehci_rd(0x44) & (1u << 8)) break; udelay(100); }
    udelay(50000);                                /* >= 10 ms */
    ps = ehci_rd(0x44);
    ehci_wr(0x44, (ps & ~(1u << 8)) | (1u << 12)); /* clear PR (manually), keep PP */
    for (uint32_t t = 0; t < 100; t++) {
        ps = ehci_rd(0x44);
        if ((ps & (1u << 2)) || (ps & (1u << 13))) break;   /* PE or PortOwner */
        udelay(1000);
    }
    /* speed: PORTSC 27:26 (0=FS,1=LS,2=HS) — забираем себе, если HS */
    int speed = (ps >> 26) & 3;
    printf("usb-ehci: port reset portsc=0x%08X pe=%d po=%d speed=%d\n",
           (unsigned)ps, (int)((ps >> 2) & 1), (int)((ps >> 13) & 1), speed);
    return (ps & (1u << 2)) ? ((speed == 2) ? 0 : 1) : -1;   /* 0=HS, 1=non-HS */
}

int usb_hc_init(void) {
    /* r856: если HID-скан уже прочитал MSD-конфиг — не трогаем EHCI и не
       энумерируем устройство второй раз (этот путь падал на стенде). */
    if (g_pre_msd) {
        if (usb_ohci_activate_flash_pre() == 0) return 0;
        printf("usb: pre-enum failed, fallback re-enum\n");
        g_pre_msd = 0;
    }

    // Подъём EHCI (PHY1): такты/PHY уже подняты usb_ohci_init (PHY1+PHY2,
    // usb_kbd_init). Здесь только EHCI-контроллер.
    printf("usb-ehci: init\n");

    if (usb_ehci_hc_reset() != 0) { printf("usb-ehci: reset fail\n"); return -1; }

    ehci_wr(0x00, 0);                       /* RS=0 */
    ehci_wr(0x00, 1u << 0);                 /* RS=1 */
    ehci_wr(0x40, 1);                       /* CONFIGFLAG: route ports to HC */
    udelay(10000);

    int hs = usb_ehci_port_reset();
    if (hs < 0) { printf("usb-ehci: no HS device\n"); return -1; }
    if (hs == 1) {
        /* Не-HS устройство (FS/LS) на EHCI-порту — передаём компаньону OHCI
         * (PHY1, 0x01C1B400): полный enum + MSD через OHCI-bulk.
         * Это типичный случай: SanDisk Cruzer Blade определяется как FS. */
        printf("usb-ehci: non-HS on EHCI, using OHCI bulk\n");
        return usb_ohci_enumerate_flash();
    }

    // Enum HS-устройства.
    static uint8_t buf[64] __attribute__((section(".coherent"), aligned(32)));
    static uint8_t cfg9[9] __attribute__((section(".coherent"), aligned(32)));
    static uint8_t cfg_full[256] __attribute__((section(".coherent"), aligned(32)));
    static uint8_t mbr[512] __attribute__((section(".coherent"), aligned(32)));
    static struct usb_msd_dev mdev;
    uint8_t setup[8];

    g_usb_addr = 0;
    /* GET_DESCRIPTOR(DEVICE) 8 байт */
    setup[0]=0x80; setup[1]=0x06; setup[2]=0x00; setup[3]=0x01;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x08; setup[7]=0x00;
    if (usb_ehci_ctrl_mps(setup, 0, 0, buf, 8, 64) != 0) { printf("usb-ehci: dev desc fail\n"); return -1; }
    int mps = buf[7];
    if (mps < 8 || mps > 64) mps = 64;

    /* SET_ADDRESS(1) */
    setup[0]=0x00; setup[1]=0x05; setup[2]=0x01; setup[3]=0x00;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x00; setup[7]=0x00;
    if (usb_ehci_ctrl_mps(setup, 0, 0, 0, 0, (uint16_t)mps) != 0) { printf("usb-ehci: set addr fail\n"); return -1; }
    g_usb_addr = 1;
    udelay(5000);

    /* Полный device-дескриптор */
    setup[0]=0x80; setup[1]=0x06; setup[2]=0x00; setup[3]=0x01;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x12; setup[7]=0x00;
    if (usb_ehci_ctrl_mps(setup, 0, 0, buf, 18, (uint16_t)mps) != 0) { printf("usb-ehci: full desc fail\n"); return -1; }
    printf("usb-ehci: vid=0x%04X pid=0x%04X\n", buf[8] | (buf[9] << 8), buf[10] | (buf[11] << 8));

    /* Config 9 -> полный */
    setup[0]=0x80; setup[1]=0x06; setup[2]=0x00; setup[3]=0x02;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x09; setup[7]=0x00;
    if (usb_ehci_ctrl_mps(setup, 0, 0, cfg9, 9, (uint16_t)mps) != 0) { printf("usb-ehci: cfg desc fail\n"); return -1; }
    uint32_t wlen = cfg9[2] | (cfg9[3] << 8);
    if (wlen > sizeof(cfg_full)) wlen = sizeof(cfg_full);
    setup[6]=(uint8_t)wlen; setup[7]=(uint8_t)(wlen >> 8);
    if (usb_ehci_ctrl_mps(setup, 0, 0, cfg_full, wlen, (uint16_t)mps) != 0) { printf("usb-ehci: cfg full fail\n"); return -1; }

    /* SET_CONFIGURATION(1) */
    setup[0]=0x00; setup[1]=0x09; setup[2]=0x01; setup[3]=0x00;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x00; setup[7]=0x00;
    if (usb_ehci_ctrl_mps(setup, 0, 0, 0, 0, (uint16_t)mps) != 0) { printf("usb-ehci: SET_CONFIGURATION fail\n"); return -1; }
    udelay(5000);

    // MSD-интерфейс + активация.
    if (usb_msd_init(&mdev, cfg_full, wlen) != 0) { printf("usb-ehci: no MSD interface\n"); return -1; }
    g_bulk_via_ohci = 0;
    if (usb_msd_wait_ready(&mdev, 3) != 0) printf("usb-ehci: TUR fail\n");
    uint32_t last = 0, blk = 0;
    if (usb_msd_read_capacity(&mdev, &last, &blk) == 0) {
        printf("usb-ehci: block=%u last_lba=%u\n", (unsigned)blk, (unsigned)last);
    } else {
        printf("usb-ehci: READ CAPACITY fail\n");
    }
    if (usb_msd_activate(&mdev) != 0) { printf("usb-ehci: msd activate fail\n"); return -1; }
    if (usb_msd_block_read(0, mbr) != 0) { printf("usb-ehci: MBR read fail\n"); return -1; }
    printf("usb-ehci: MBR sig=0x%04X — flash OK\n", mbr[510] | (mbr[511] << 8));
    return 0;
}

// Доступ к текущим EP активного накопителя (для BOT-recovery из usb_msd).
uint8_t usb_msd_eps(uint8_t* in, uint8_t* out) {
    extern uint8_t usb_msd_get_eps(uint8_t* in, uint8_t* out);
    return usb_msd_get_eps(in, out);
}

// ---- r788/r793: полный re-enum FS/LS-флешки через OHCI (PHY1) ----
// Повторные control-передачи на уже энумерированном устройстве (адрес 1)
// после enum клавиатуры + запуска её interrupt-IN падали (SET_CONFIGURATION
// fail в r792). Надёжный путь, проверенный на стенде в Bare_linux:
// сброс порта B400 → устройство уходит в Default state (адрес 0) →
// полный enum: GET_DESC(0)/SET_ADDRESS(1)/GET_DESC(full)/
// GET_CONFIG(9/full)/SET_CONFIGURATION → MSD через OHCI-bulk.
int usb_ohci_enumerate_flash(void) {
    extern int usb_ohci_port_reset(uint32_t base, int port);
    extern int usb_ohci_ctrl_transfer(uint32_t base, uint8_t addr, uint8_t ep_in,
                                      const uint8_t* setup, uint8_t setup_len,
                                      uint8_t* data, uint32_t data_len, int dir_in,
                                      uint32_t timeout_ms);
    extern void usb_ohci_set_mps(uint16_t mps);
    extern int usb_ohci_bulk_toggle_reset(uint32_t base);
    extern void usb_ohci_bot_recover(uint32_t base);

    const uint32_t base = 0x01C1B400u;   /* PHY1 OHCI (компаньон EHCI) */
    static uint8_t buf[64] __attribute__((section(".coherent"), aligned(32)));
    static uint8_t cfg9[9] __attribute__((section(".coherent"), aligned(32)));
    static uint8_t cfg[256] __attribute__((section(".coherent"), aligned(32)));
    static uint8_t mbr[512] __attribute__((section(".coherent"), aligned(32)));
    static uint8_t inq[36] __attribute__((section(".coherent"), aligned(32)));
    static struct usb_msd_dev mdev;
    uint8_t setup[8];

    printf("usb-ohci: flash re-enum on 0x%X\n", (unsigned)base);

    /* 0. ОТДАТЬ ПОРТ от EHCI компаньону OHCI: PO=1 (эталон Bare_linux,
     *    usb_hc.c:805). После usb_ehci_hc_reset+CONFIGFLAG=1+port_reset EHCI
     *    остался владельцем PHY-порта (PortOwner=0), поэтому OHCI видел
     *    CCS=0 ("device gone", лог r800). PO=1 снимает владение */
    {
        volatile uint32_t* portsc = (volatile uint32_t*)(USB_EHCI_OP1 + 0x44);
        uint32_t ps = *portsc;
        if (!(ps & (1u << 13))) {
            *portsc = ps | (1u << 13);   /* set PO */
            __asm volatile("dsb" ::: "memory");
            udelay(50000);               /* EHCI снимает владение, OHCI видит CCS */
        }
    }

    /* 1. Сброс порта: устройство в Default state (адрес 0). */
    if (usb_ohci_port_reset(base, 0) != 0) {
        printf("usb-ohci: flash port reset fail\n"); return -1;
    }

    /* 1. GET_DESCRIPTOR(DEVICE) 8 байт, адрес 0 */
    setup[0]=0x80; setup[1]=0x06; setup[2]=0x00; setup[3]=0x01;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x08; setup[7]=0x00;
    if (usb_ohci_ctrl_transfer(base, 0, 0, setup, 8, buf, 8, 1, 1000) < 0) {
        printf("usb-ohci: dev desc fail\n"); return -1;
    }
    int mps = buf[7];
    if (mps < 8) mps = 8;
    if (mps > 64) mps = 64;
    usb_ohci_set_mps((uint16_t)mps);

    /* 2. SET_ADDRESS(1) */
    setup[0]=0x00; setup[1]=0x05; setup[2]=0x01; setup[3]=0x00;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x00; setup[7]=0x00;
    if (usb_ohci_ctrl_transfer(base, 0, 0, setup, 8, 0, 0, 0, 1000) < 0) {
        printf("usb-ohci: set addr fail\n"); return -1;
    }
    g_usb_addr = 1;
    udelay(5000);

    /* 3. Полный device-дескриптор (для лога VID/PID) */
    setup[0]=0x80; setup[1]=0x06; setup[2]=0x00; setup[3]=0x01;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x12; setup[7]=0x00;
    if (usb_ohci_ctrl_transfer(base, 1, 0, setup, 8, buf, 18, 1, 1000) < 0) {
        printf("usb-ohci: full desc fail\n"); return -1;
    }
    printf("usb-ohci: vid=0x%04X pid=0x%04X\n", buf[8] | (buf[9] << 8), buf[10] | (buf[11] << 8));

    /* 4. Config 9 -> полный */
    setup[0]=0x80; setup[1]=0x06; setup[2]=0x00; setup[3]=0x02;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x09; setup[7]=0x00;
    if (usb_ohci_ctrl_transfer(base, 1, 0, setup, 8, cfg9, 9, 1, 1000) < 0) {
        printf("usb-ohci: cfg desc fail\n"); return -1;
    }
    uint32_t wlen = cfg9[2] | (cfg9[3] << 8);
    if (wlen > sizeof(cfg)) wlen = sizeof(cfg);
    if (wlen < 9) { printf("usb-ohci: cfg len bad\n"); return -1; }
    setup[6]=(uint8_t)wlen; setup[7]=(uint8_t)(wlen >> 8);
    if (usb_ohci_ctrl_transfer(base, 1, 0, setup, 8, cfg, wlen, 1, 1000) < 0) {
        printf("usb-ohci: cfg full fail\n"); return -1;
    }

    /* 5. SET_CONFIGURATION(1) — активует bulk-EP MSD */
    setup[0]=0x00; setup[1]=0x09; setup[2]=0x01; setup[3]=0x00;
    setup[4]=0x00; setup[5]=0x00; setup[6]=0x00; setup[7]=0x00;
    if (usb_ohci_ctrl_transfer(base, 1, 0, setup, 8, 0, 0, 0, 1000) < 0) {
        printf("usb-ohci: SET_CONFIGURATION fail\n"); return -1;
    }
    udelay(100000);

    /* MSD-интерфейс + активация. Архиважно: bulk-маршрут → OHCI. */
    if (usb_msd_init(&mdev, cfg, wlen) != 0) {
        printf("usb-ohci: no MSD interface\n"); return -1;
    }
    extern void usb_hc_set_bulk_ohci(void);
    usb_hc_set_bulk_ohci();
    usb_ohci_bulk_toggle_reset(base);
    if (usb_msd_activate(&mdev) != 0) {
        printf("usb-ohci: msd activate fail\n"); return -1;
    }
    usb_ohci_bot_recover(base);
    printf("usb-ohci: BOT recovery done\n");

    if (usb_msd_inquiry(&mdev, inq) == 0) {
        printf("usb-ohci: INQUIRY: ");
        for (int i = 8; i < 32 && inq[i]; i++) {
            char c = (char)inq[i];
            if (c < 0x20 || c > 0x7E) c = '.';
            printf("%c", c);
        }
        printf("\n");
    }

    uint32_t last = 0, blk = 0;
    if (usb_msd_wait_ready(&mdev, 3) != 0) printf("usb-ohci: TUR fail\n");
    if (usb_msd_read_capacity(&mdev, &last, &blk) == 0) {
        printf("usb-ohci: block=%u last_lba=%u\n", (unsigned)blk, (unsigned)last);
    } else {
        printf("usb-ohci: READ CAPACITY fail\n");
    }

    if (usb_msd_block_read(0, mbr) != 0) {
        printf("usb-ohci: MBR read fail\n"); return -1;
    }
    printf("usb-ohci: MBR sig=0x%04X — flash OK\n", mbr[510] | (mbr[511] << 8));
    return 0;
}

// Переключить bulk-маршрут на OHCI (для FS/LS-флешки). Вызывается
// usb_ohci_enumerate_flash ДО активации MSD. Реализация в usb_hc.c.
void usb_hc_set_bulk_ohci(void) {
    g_bulk_via_ohci = 1;
}