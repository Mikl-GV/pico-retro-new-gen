// usb_kbd.c — USB HID ввод через OHCI (Full/Low-Speed).
//
// Единый слой состояния клавиатуры + очередь событий PRESS/RELEASE.
//
// Потребители читают только актуальное СОСТОЯНИЕ:
//   * меню/браузер/настройки:   usb_kbd_poll()     — сканкод нажатия или 0;
//   * эмуляторы (host-слои):    usb_kbd_get_raw()  — удерживаемые коды;
//                               usb_kbd_get_mods() — биты модификаторов.
//
// Слой опрашивает железо ровно раз в кадр: всё чтение OHCI и применение
// отчётов происходит в kbd_scan(), который вызывается не чаще KBD_FRAME_US.
// Повторные вызовы публичных функций в tight-loop меню дешёвые (очередь и
// автоповтор), аппаратный опрос не размножается.
//
// Физика донглов (см. docs/avatto-i8-pro-*.md, проверено на железе):
//   * отчёт (8 байт boot keyboard) приходит ТОЛЬКО на изменение состояния;
//   * удержание клавиши = молчание (замер: 'G' 5.1 с без пакетов);
//   * отпускание = пустой отчёт 00 00 00 00 00 00 00 00;
//   * автоповтор делает потребитель (меню/BIOS), не донгл.
// Поэтому: фронты определяются prev/cur-диффом отчётов (пустой отчёт =
// отпускание), автоповтор — в usb_kbd_poll()/host-слоях, а таймаут по
// тишине — только страховка от потери связи (обрыв RF).
//
// Каналы чтения:
//   * Low-Speed  (mps0<=8, напр. Novatek 0603:0002) — Interrupt IN (слот 0),
//     донгл не отвечает на GET_REPORT;
//   * Full-Speed (mps0>8,  напр. Logitech)          — GET_REPORT (control).
#include <string.h>
#include "h3.h"
#include "h3_hs_timer.h"
#include "uart.h"
#include "usb_ohci.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "usb_kbd.h"
#include "usb_msd.h"
#include "i2s.h"   // r1141: кнопки громкости клавиатуры

#define TOUCH_BUF 16

// ---- Стандартные дескрипторы USB (enum_port) ----
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} usb_intf_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_ep_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} usb_setup_t;

#define GET_DESCRIPTOR    6
#define SET_ADDRESS       5
#define SET_CONFIGURATION 9
#define HID_SET_PROTOCOL  0x0B
#define HID_GET_REPORT    0x01
#define RT_DEVICE         (0x80 | 0x00)

extern int printf(const char* fmt, ...);

// ---- Одно HID-устройство ----
typedef struct {
    uint32_t base;
    uint8_t  addr;
    uint8_t  in_ep;
    uint16_t in_maxpkt;
    uint8_t  mps0;          // ep0 max packet size
    uint8_t  report[8];     // клавиатурный отчёт
    int      found;
    int      type;          // 1=kbd, 2=touch, 3=mouse
} usb_dev_t;

// Секция .coherent: HC пишет report[] через DMA, write-back D-cache давал
// бы stale-чтения (гонка с toggle).
static usb_dev_t g_kbd   __attribute__((section(".coherent"), aligned(8)));
static usb_dev_t g_touch __attribute__((section(".coherent"), aligned(8)));
static usb_dev_t g_pad   __attribute__((section(".coherent"), aligned(8)));

static uint8_t g_pad_report[8] __attribute__((section(".coherent"), aligned(8)));

static int g_pad_x = 512;
static int g_pad_y = 300;

static uint8_t g_touch_report[TOUCH_BUF] __attribute__((section(".coherent"), aligned(8)));

// ============================================================================
//  КЛАВИАТУРА: состояние + события
// ============================================================================
#define KBD_Q_LEN 24
static uint8_t  g_q_sc[KBD_Q_LEN], g_q_ev[KBD_Q_LEN];
static int      g_q_r = 0, g_q_w = 0;

static uint8_t  g_held[6];       // удерживаемые коды (как в последнем отчёте)
static int      g_held_n = 0;
static uint8_t  g_mods = 0;      // модификаторы последнего отчёта
static uint32_t g_down_t[256];   // мкс первого нажатия (0 = не нажата)
static uint32_t g_rep_t[256];    // мкс последнего автоповтора (меню)

// Кадровый гейт аппаратного опроса (общий для меню и эмуляторов).
#define KBD_FRAME_US 16000u
static uint32_t g_scan_t = 0;

static int g_esc_cnt = 0;        // ESC x3 (Low-Speed донгл): выход из эмулятора
static uint32_t g_esc_last_t = 0;
static int g_esc3_fired = 0;
#define ESC3_WINDOW_US 700000
#define ESC3_NEED      3

// Автоповтор в меню (навигация стрелками); в играх повтор не делаем.
#define KBD_REPEAT_DELAY_US 350000
#define KBD_REPEAT_RATE_US   90000

// Страховка потери связи: донгл отвалился — отпустить всё, что было
// зажато. r1065: пустой отчёт отпускания приходит САМ (см. doc: пустой =
// отпускание; удержание = молчание НЕ требует таймаутов). Оставшиеся
// зажатые клавиши при КОРРЕКТНОМ потоке пакетов отпускаются через
// kbd_process_report (diff prev/cur). Таймаут оставлен ТОЛЬКО как крайняя
// страховка при физическом обрыве RF/донгла (нет НИ ОДНОГО пакета вообще).
#define KBD_IDLE_RELEASE_US 5000000u

static int kbd_low_speed = 0;
static int kbd_found = 0;

// forward (определены ниже, в секции control/cache)
static int get_report_dev(usb_dev_t* d, uint8_t* buf, int len);
static void cache_inv(uint32_t addr, uint32_t size);

// ---- очередь событий ----
static void q_push(uint8_t sc, uint8_t ev) {
    int nxt = (g_q_w + 1) % KBD_Q_LEN;
    if (nxt == g_q_r) g_q_r = (g_q_r + 1) % KBD_Q_LEN;   // переполнение: старое
    g_q_sc[g_q_w] = sc; g_q_ev[g_q_w] = ev;
    g_q_w = nxt;
}
static int q_pop(uint8_t* sc, uint8_t* ev) {
    if (g_q_r == g_q_w) return 0;
    *sc = g_q_sc[g_q_r]; *ev = g_q_ev[g_q_r];
    g_q_r = (g_q_r + 1) % KBD_Q_LEN;
    return 1;
}

// prev/cur-дифф отчёта → фронты PRESS/RELEASE и новое состояние удержания.
static void kbd_process_report(const uint8_t* rep) {
    uint32_t now = h3_hs_timer_lo_us();
    g_mods = rep[0];

    // HID rollover: >6 клавиш одновременно — в поле кодов 0x01 (мусор),
    // реальные коды отсутствуют. Состояние удержания НЕ трогаем (иначе
    // diff ложно «отпустит» все зажатые), модификаторы уже обновлены выше.
    int rollover = 0;
    for (int i = 2; i < 8; i++)
        if (rep[i] == 0x01) { rollover = 1; break; }
    if (rollover) return;

    uint8_t codes[6]; int cn = 0;
    for (int i = 2; i < 8; i++) if (rep[i]) codes[cn++] = rep[i];

    // r1141: аппаратные кнопки громкости (HID Consumer Page: Volume Up=0xE9,
    // Volume Down=0xEA, часто в верхнем мультимедиа-ряду). По фронту нажатия
    // меняем громкость и НЕ передаём сканкод в очередь/удержание (иначе
    // «доедет» в эмулятор как игровая кнопка). Отчёты приходят только на
    // изменение, поэтому каждое появление кода = одно нажатие (автоповтор
    // делает потребитель, здесь не нужен). Работает и в меню, и в игре.
    // core0-only: g_volume_pct живёт в обычном BSS, гонки с CPU2 нет.
    {
        extern void i2s_volume(int);
        extern int  i2s_volume_pct(void);
        int vol_delta = 0;
        for (int j = 0; j < cn; j++) {
            if (codes[j] == 0xE9) vol_delta += 5;    // Volume Up
            else if (codes[j] == 0xEA) vol_delta -= 5; // Volume Down
        }
        if (vol_delta != 0) {
            int v = i2s_volume_pct() + vol_delta;
            if (v > 100) v = 100; if (v < 0) v = 0;
            i2s_volume(v);
        }
        // отфильтровать volume-коды из списка — не для очереди/удержания
        int dst = 0;
        for (int j = 0; j < cn; j++)
            if (codes[j] != 0xE9 && codes[j] != 0xEA) codes[dst++] = codes[j];
        cn = dst;
    }

    // отпускания: были нажаты, в новом отчёте их нет (в т.ч. пустой отчёт)
    for (int i = 0; i < g_held_n; i++) {
        uint8_t sc = g_held[i]; int still = 0;
        for (int j = 0; j < cn; j++) if (codes[j] == sc) { still = 1; break; }
        if (!still) { g_down_t[sc] = 0; g_rep_t[sc] = 0; q_push(sc, KBD_EV_RELEASE); }
    }
    // нажатия: есть в новом отчёте, не было в прошлом
    for (int j = 0; j < cn; j++) {
        uint8_t sc = codes[j]; int had = 0;
        for (int i = 0; i < g_held_n; i++) if (g_held[i] == sc) { had = 1; break; }
        if (!had) {
            g_down_t[sc] = now ? now : 1;
            g_rep_t[sc] = g_down_t[sc];
            q_push(sc, KBD_EV_PRESS);
            // Выход из эмулятора: ESC x3 подряд в окне (Low-Speed донгл,
            // у него ненадёжно длинное удержание).
            if (kbd_low_speed && sc == 41) {
                if (now - g_esc_last_t <= ESC3_WINDOW_US) g_esc_cnt++;
                else g_esc_cnt = 1;
                g_esc_last_t = now;
                if (g_esc_cnt >= ESC3_NEED) { g_esc3_fired = 1; g_esc_cnt = 0; }
            }
        }
    }
    g_held_n = cn;
    for (int i = 0; i < cn; i++) g_held[i] = codes[i];
}

// Отпустить всё (страховка потери связи / принудительный сброс).
static void kbd_release_all(void) {
    for (int i = 0; i < g_held_n; i++) {
        uint8_t sc = g_held[i];
        g_down_t[sc] = 0; g_rep_t[sc] = 0;
        q_push(sc, KBD_EV_RELEASE);
    }
    g_held_n = 0;
}

// ---- Чтение железа ----
static uint32_t g_raw_fresh_t = 0;   // мкс последнего снятого отчёта

// 0 = есть свежий отчёт в g_kbd.report; -1 = нет пакета / ошибка.
// r1065: рестарт цепочки при ошибке TD делает usb_ohci_intr_in_poll сам
// (Halted-детект + intr_in_restart). Здесь -1 означает «кольцо пересобрано»,
// удержание сбрасываем — после рестарта состояние клавиш неизвестно, а
// пустой отчёт RELEASE уже не придёт (он ушёл в умерший TD).
static int kbd_read_report(void) {
    if (kbd_low_speed) {
        int r = usb_ohci_intr_in_poll(g_kbd.base, g_kbd.report, 8, 0);
        if (r > 0) { g_raw_fresh_t = h3_hs_timer_lo_us(); return 0; }
        if (r == 0) return -1;          // пакета нет (удержание = молчание)
        kbd_release_all();              // ошибка TD: Halted -> кольцо пересобрано
        return -1;
    }
    if (get_report_dev(&g_kbd, g_kbd.report, 8) < 0) return -1;
    cache_inv((uint32_t)g_kbd.report, 8);
    g_raw_fresh_t = h3_hs_timer_lo_us();
    return 0;
}

// Один кадр опроса железа: снимаем ВСЕ накопленные отчёты подряд (быстрая
// печать шлёт пачку — нажатие+отпускание на одном кадре), каждый применяем
// к состоянию. Затем страховка потери связи.
#define KBD_DRAIN_MAX 16
static void kbd_scan(void) {
    for (int d = 0; d < KBD_DRAIN_MAX; d++) {
        if (kbd_read_report() != 0) break;
        kbd_process_report(g_kbd.report);
    }
    if (g_held_n > 0 && g_raw_fresh_t != 0 &&
        (h3_hs_timer_lo_us() - g_raw_fresh_t > KBD_IDLE_RELEASE_US))
        kbd_release_all();
}

// Публичный опрос не чаще кадра (общий кадровый гейт).
static void kbd_poll_frame(void) {
    uint32_t now = h3_hs_timer_lo_us();
    if (now - g_scan_t < KBD_FRAME_US) return;
    g_scan_t = now;
    kbd_scan();
}

// ============================================================================
//  Control-передача + cache + энумерация
// ============================================================================
static int ctrl_req_dev(usb_dev_t* d, const usb_setup_t* req, uint8_t* data,
                        uint16_t data_len, int dir_in, uint32_t timeout_ms) {
    usb_ohci_set_mps(d->mps0 ? d->mps0 : 8);
    return usb_ohci_ctrl_transfer(d->base, d->addr, 0,
                                  (const uint8_t*)req, sizeof(*req),
                                  data, data_len, dir_in, timeout_ms);
}

static int get_report_dev(usb_dev_t* d, uint8_t* buf, int len) {
    usb_setup_t req = {
        .bmRequestType = 0xA1,
        .bRequest      = HID_GET_REPORT,
        .wValue        = 0x0100,
        .wIndex        = 0,
        .wLength       = (uint16_t)len,
    };
    return ctrl_req_dev(d, &req, buf, len, 1, 200);
}

// L1 by MVA + L2/PL310 by PA: DMA пишет DRAM, во write-back кэше иначе stale.
static void cache_inv(uint32_t addr, uint32_t size) {
    uint32_t start = addr & ~0x1Fu;
    uint32_t end = (addr + size + 0x1Fu) & ~0x1Fu;
    volatile uint32_t* l2 = (volatile uint32_t*)0x01C43000u;   /* PL310 */
    l2[0x730u / 4] = 0;                                        /* CACHE_SYNC */
    for (volatile int i = 0; i < 1000 && (l2[0x730u / 4] & 1u); i++) ;
    for (uint32_t a = start; a < end; a += 32)
        l2[0x7B0u / 4] = a;                                    /* CLEAN_INV_PA */
    __asm volatile("dsb" ::: "memory");
    for (uint32_t a = start; a < end; a += 32)
        __asm volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(a)); /* DCIMVAC */
    __asm volatile("dsb" ::: "memory");
}

// Энумерация одного порта. Второй интерфейс boot-mouse → mouse_out.
static int enum_port(uint32_t base, usb_dev_t* dev, usb_dev_t* mouse_out) {
    uint8_t buf[256];
    int r;

    if (mouse_out) memset(mouse_out, 0, sizeof(*mouse_out));
    memset(dev, 0, sizeof(*dev));
    dev->base = base;

    if (usb_ohci_port_reset(base, 0) < 0) {
        uart_puts("usb: port reset fail\n");
        return 0;
    }

    // 1. дескриптор на адресе 0 (первые 8 байт — узнаём ep0 mps)
    dev->addr = 0;
    r = ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = RT_DEVICE,
                                          .bRequest = GET_DESCRIPTOR,
                                          .wValue = (1 << 8), .wLength = 8 },
                     buf, 8, 1, 1000);
    if (r < 0) { uart_puts("usb: get_dev_desc @0 fail\n"); return 0; }
    int mps = buf[7];
    if (mps < 8) mps = 8;
    if (mps > 64) mps = 64;
    dev->mps0 = (uint8_t)mps;
    usb_ohci_set_mps((uint16_t)mps);

    // 2. SET_ADDRESS(1)
    r = ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = 0x00,
                                          .bRequest = SET_ADDRESS, .wValue = 1 },
                     0, 0, 0, 1000);
    if (r < 0) { uart_puts("usb: set_addr fail\n"); return 0; }
    dev->addr = 1;
    udelay(5000);

    // 3. полный дескриптор устройства (VID/PID)
    r = ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = RT_DEVICE,
                                          .bRequest = GET_DESCRIPTOR,
                                          .wValue = (1 << 8),
                                          .wLength = (uint16_t)(mps > 18 ? mps : 18) },
                     buf, mps > 18 ? mps : 18, 1, 1000);
    if (r < 0) { uart_puts("usb: get_dev_full fail\n"); return 0; }
    uint16_t vid = (uint16_t)(buf[8] | (buf[9] << 8));
    uint16_t pid = (uint16_t)(buf[10] | (buf[11] << 8));
    const char* dname = "?";
    switch (vid) {
    case 0x0603: dname = "Avatto/NOVATEK (i8 Pro донгл)"; break;
    case 0x046D: dname = "Logitech"; break;
    case 0x05AC: dname = "Apple"; break;
    case 0x413C: dname = "Dell"; break;
    case 0x03F0: dname = "HP"; break;
    case 0x04F2: dname = "Chicony"; break;
    case 0x1C4F: dname = "SiGma"; break;
    default: break;
    }
    printf("usb: port=0x%X device: %s (vid=%04X pid=%04X class=%02X mps=%d)\n",
           (unsigned)base, dname, (unsigned)vid, (unsigned)pid,
           (unsigned)buf[5], mps);

    // строковые дескрипторы iManufacturer/iProduct
    {
        uint8_t si[66];
        uint16_t lang = 0x0409;
        int rr = ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = RT_DEVICE,
                                .bRequest = GET_DESCRIPTOR,
                                .wValue = (3 << 8), .wIndex = 0, .wLength = 4 },
                              si, 4, 1, 1000);
        if (rr >= 4 && si[0] >= 4 && si[1] == 3 && rr >= 4)
            if (si[2] | si[3])
                lang = (uint16_t)(si[2] | (si[3] << 8));
        for (int sidx = 1; sidx <= 2; sidx++) {
            rr = ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = RT_DEVICE,
                                    .bRequest = GET_DESCRIPTOR,
                                    .wValue = (uint16_t)((3 << 8) | sidx),
                                    .wIndex = lang, .wLength = 64 },
                              si, 64, 1, 1000);
            if (rr >= 2 && si[1] == 3 && si[0] >= 2) {
                char lbuf[40];
                int n = 0, lim = si[0];
                if (lim > rr) lim = rr;
                for (int k = 2; k + 1 < lim && n < (int)sizeof(lbuf) - 1; k += 2)
                    lbuf[n++] = (char)si[k];
                lbuf[n] = 0;
                if (lbuf[0])
                    printf("usb:   %s: \"%s\"\n", sidx == 1 ? "manuf" : "prod ", lbuf);
            }
        }
    }

    // 4. конфигурация
    r = ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = RT_DEVICE,
                                          .bRequest = GET_DESCRIPTOR,
                                          .wValue = (2 << 8), .wLength = 256 },
                     buf, 256, 1, 1000);
    if (r < 0) { uart_puts("usb: get_cfg fail\n"); return 0; }

    // 5. парсинг: boot keyboard (3,1,1); generic HID (3,0,x) = тач; boot mouse (3,1,2)
    int pos = 0;
    int type = 0;
    int in_mouse_intf = 0;
    dev->in_ep = 0;
    dev->in_maxpkt = 0;
    while (pos + 1 < r && pos + 1 < 255) {
        uint8_t len = buf[pos], t = buf[pos + 1];
        if (len == 0 || (int)pos + len > r || (int)pos + len > 256) break;
        if (t == 4 && len >= 9) {
            usb_intf_desc_t* intf = (usb_intf_desc_t*)(buf + pos);
            in_mouse_intf = 0;
            if (intf->bInterfaceClass == 3 && intf->bInterfaceSubClass == 1 &&
                intf->bInterfaceProtocol == 1) {
                type = 1;
                dev->in_ep = 0; dev->in_maxpkt = 0;
            } else if (intf->bInterfaceClass == 3 && intf->bInterfaceSubClass == 1 &&
                       intf->bInterfaceProtocol == 2 && mouse_out) {
                mouse_out->base = base;
                mouse_out->addr = 1;
                mouse_out->mps0 = dev->mps0;
                mouse_out->in_ep = 0;
                mouse_out->in_maxpkt = 0;
                mouse_out->type = 3;
                in_mouse_intf = 1;
                uart_puts("usb:   -> MOUSE (boot mouse)\n");
            } else if (intf->bInterfaceClass == 3 && !type) {
                type = 2;
                dev->in_ep = 0; dev->in_maxpkt = 0;
            }
        } else if (t == 5 && len >= 7) {
            usb_ep_desc_t* ep_desc = (usb_ep_desc_t*)(buf + pos);
            if (ep_desc->bEndpointAddress & 0x80) {
                if (type == 1 && !dev->in_ep) {
                    dev->in_ep = ep_desc->bEndpointAddress;
                    dev->in_maxpkt = ep_desc->wMaxPacketSize;
                } else if (in_mouse_intf && mouse_out && !mouse_out->in_ep) {
                    mouse_out->in_ep = ep_desc->bEndpointAddress;
                    mouse_out->in_maxpkt = ep_desc->wMaxPacketSize;
                    if (mouse_out->in_ep) mouse_out->found = 1;
                }
            }
        }
        pos += len;
    }

    if (!dev->in_ep) {
        extern void usb_hc_set_preenum_msd(const uint8_t* cfg, uint32_t len,
                                           uint16_t mps, uint32_t base);
        struct usb_msd_dev md;
        if (usb_msd_init(&md, buf, (uint32_t)r) == 0)
            usb_hc_set_preenum_msd(buf, (uint32_t)r, dev->mps0, base);
        printf("usb: port=0x%X no HID IN ep, type=%d\n", (unsigned)base, type);
        return 0;
    }

    // 6. Set config
    r = ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = 0x00,
                                          .bRequest = SET_CONFIGURATION, .wValue = 1 },
                     0, 0, 0, 1000);
    if (r < 0) { uart_puts("usb: set_config fail\n"); return 0; }

    dev->found = 1;
    dev->type = type;

    if (type == 1) {
        ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = 0x21,
                                          .bRequest = HID_SET_PROTOCOL, .wValue = 0,
                                          .wIndex = 0 }, 0, 0, 0, 1000);
        uart_puts("usb:   -> KEYBOARD\n");
    } else {
        uart_puts("usb:   -> TOUCH\n");
    }
    return type;
}

// ============================================================================
//  Публичный API
// ============================================================================
int usb_kbd_init(void) {
    usb_ohci_init(0x01C1B400);   // OHCI1
    usb_ohci_init(0x01C1C400);   // OHCI2

    for (int trial = 0; trial < 500; trial++) {
        if (usb_ohci_root_port_connected(0x01C1B400, 0) ||
            usb_ohci_root_port_connected(0x01C1C400, 0))
            break;
        udelay(10000);
    }

    usb_dev_t d1, d2;
    usb_dev_t mouse1, mouse2;
    memset(&d1, 0, sizeof(d1)); memset(&mouse1, 0, sizeof(mouse1));
    memset(&d2, 0, sizeof(d2)); memset(&mouse2, 0, sizeof(mouse2));
    int t1 = 0, t2 = 0;
    if (usb_ohci_root_port_connected(0x01C1B400, 0))
        t1 = enum_port(0x01C1B400, &d1, &mouse1);
    if (usb_ohci_root_port_connected(0x01C1C400, 0))
        t2 = enum_port(0x01C1C400, &d2, &mouse2);

    if (t1 == 1) { memcpy(&g_kbd, &d1, sizeof(g_kbd)); }
    else if (t1 == 2) { memcpy(&g_touch, &d1, sizeof(g_touch)); }
    if (t2 == 1) { if (!g_kbd.found) memcpy(&g_kbd, &d2, sizeof(g_kbd)); }
    else if (t2 == 2) { if (!g_touch.found) memcpy(&g_touch, &d2, sizeof(g_touch)); }

    if (mouse1.type == 3) { memcpy(&g_pad, &mouse1, sizeof(g_pad)); }
    else if (mouse2.type == 3) { memcpy(&g_pad, &mouse2, sizeof(g_pad)); }

    // Тачпад (I8 Pro/Novatek boot mouse): interrupt-IN слот 1.
    if (g_pad.found && g_pad.in_ep) {
        usb_ohci_set_mps(g_pad.mps0 ? g_pad.mps0 : 8);
        usb_setup_t req = {
            .bmRequestType = 0x21,
            .bRequest      = HID_SET_PROTOCOL,
            .wValue        = 0,
            .wIndex        = 1,
        };
        ctrl_req_dev(&g_pad, &req, 0, 0, 0, 1000);
        usb_ohci_intr_in_start(g_pad.base, g_pad.addr,
                               g_pad.in_ep & 0x7F, g_pad_report, 8, 1);
        printf("pad: touchpad ep=0x%02X maxpkt=%u -> Interrupt IN (slot 1)\n",
               g_pad.in_ep, g_pad.in_maxpkt);
    }

    if (!g_kbd.found) {
        uart_puts("usb: no keyboard found\n");
        return -1;
    }

    kbd_found = 1;
    kbd_low_speed = (g_kbd.mps0 <= 8) ? 1 : 0;
    if (kbd_low_speed) {
        printf("kbd: Low-Speed (mps=%u) -> Interrupt IN\n", g_kbd.mps0);
        usb_ohci_intr_in_start(g_kbd.base, g_kbd.addr,
                               g_kbd.in_ep & 0x7F, g_kbd.report, 8, 0);
    } else {
        printf("kbd: Full-Speed (mps=%u) -> GET_REPORT\n", g_kbd.mps0);
    }

    uart_puts("usb: keyboard ready\n");
    return 0;
}

// Меню: сканкод PRESS (очередь) либо автоповтор удержания; 0 = ничего.
int usb_kbd_poll(void) {
    if (!kbd_found) return 0;
    kbd_poll_frame();
    uint32_t now = h3_hs_timer_lo_us();
    uint8_t sc, ev;
    while (q_pop(&sc, &ev))
        if (ev == KBD_EV_PRESS) return sc;   // RELEASE меню не нужно

    for (int i = 0; i < g_held_n; i++) {
        uint8_t c = g_held[i]; uint32_t t = g_down_t[c];
        if (!t) continue;
        if (now - t >= KBD_REPEAT_DELAY_US && now - g_rep_t[c] >= KBD_REPEAT_RATE_US) {
            g_rep_t[c] = now;
            return c;
        }
    }
    return 0;
}

// Игры: текущее удерживаемое состояние (коды).
int usb_kbd_get_raw(uint8_t* buf, int max_buf) {
    if (!kbd_found) return 0;
    kbd_poll_frame();
    int cnt = 0;
    for (int i = 0; i < g_held_n && cnt < max_buf; i++) buf[cnt++] = g_held[i];
    return cnt;
}

// Модификаторы последнего обработанного отчёта.
uint8_t usb_kbd_get_mods(void) {
    return kbd_found ? g_mods : 0;
}

// Состояние без опроса железа (emu_esc_hold: не «воровать» второй отчёт/кадр).
int usb_kbd_get_last(uint8_t* buf, int max_buf) {
    if (!kbd_found) return 0;
    int cnt = 0;
    for (int i = 0; i < g_held_n && cnt < max_buf; i++) buf[cnt++] = g_held[i];
    return cnt;
}

// Обслуживание клавиатуры ИЗ длинных ожиданий OHCI (bulk/control): работает
// только для стартованного interrupt-IN (low-speed) — не входит в
// GET_REPORT-рекурсию. Не чаще кадра (общий g_scan_t).
void usb_kbd_poll_block(void) {
    if (!kbd_found || !kbd_low_speed) return;
    kbd_poll_frame();
}

// Выход по ESC x3 (Low-Speed донгл). Флаг потребляется одним вызовом.
int usb_kbd_esc3_pressed(void) {
    if (!kbd_low_speed) return 0;
    int f = g_esc3_fired;
    g_esc3_fired = 0;
    return f;
}

void usb_kbd_esc3_reset(void) {
    g_esc_cnt = 0;
    g_esc_last_t = 0;
    g_esc3_fired = 0;
}

// Перезапуск interrupt-IN цепочки (после ошибок TD / выхода из эмулятора).
// ED строится заново с toggle=DATA0 — рассинхрон чётности снимается сам.
void usb_kbd_restart_intr(void) {
    if (!kbd_found || !kbd_low_speed) return;
    usb_ohci_intr_in_start(g_kbd.base, g_kbd.addr,
                           g_kbd.in_ep & 0x7F, g_kbd.report, 8, 0);
}

// Дождаться, пока СЛОЙ не покажет ноль удержанных клавиш (или 800 мс).
// Чистим состояние/очередь/модификаторы, чтобы зажатые клавиши и «доезд»
// фронтов не попали в новое подменю/эмулятор.
void usb_kbd_wait_release(void) {
    uint32_t t0 = h3_hs_timer_lo_us();
    for (;;) {
        kbd_poll_frame();
        if (g_held_n == 0) break;
        if (h3_hs_timer_lo_us() - t0 > 800000u) break;
        udelay(1000);
    }
    g_q_r = g_q_w = 0;
    g_held_n = 0;
    g_mods = 0;
    g_esc_cnt = 0;
    g_esc_last_t = 0;
    g_esc3_fired = 0;
    for (int i = 0; i < 256; i++) { g_down_t[i] = 0; g_rep_t[i] = 0; }
}

// Дождаться отпускания клавиатуры и Sega-геймпада при ВЫХОДЕ из эмулятора
// (зажатая кнопка не должна «доехать» в меню).
void usb_wait_release_all(void) {
    usb_kbd_wait_release();
    usb_pad_wait_release();
}

// ---- Тач (Waveshare GT911) ----
int usb_touch_poll(int* x, int* y, int* pressed) {
    if (!g_touch.found || !g_touch.in_ep) return 0;
    usb_setup_t req = {
        .bmRequestType = 0xA1,
        .bRequest      = HID_GET_REPORT,
        .wValue        = 0x0100,
        .wIndex        = 0,
        .wLength       = (uint16_t)16,
    };
    int r = ctrl_req_dev(&g_touch, &req, g_touch_report, 16, 1, 50);
    if (r < 0) return 0;
    cache_inv((uint32_t)g_touch_report, TOUCH_BUF);
    if (g_touch_report[0] != 0x01) return 0;

    uint8_t status = g_touch_report[1];
    *pressed = (status & 0x01) != 0;
    if (*pressed) {
        *x = (int)g_touch_report[2] | ((int)g_touch_report[3] << 8);
        *y = (int)g_touch_report[4] | ((int)g_touch_report[5] << 8);
    }
    return 1;
}

// ---- Тачпад (I8 Pro boot mouse): накопление курсора из dx/dy ----
void usb_pad_poll(void) {
    if (!g_pad.found || !g_pad.in_ep) return;
    int drained = 0;
    while (usb_ohci_intr_in_poll(g_pad.base, g_pad_report, 8, 1) > 0 && ++drained < 64) {
        int8_t dx = (int8_t)g_pad_report[1];
        int8_t dy = (int8_t)g_pad_report[2];
        if (dx > 1 || dx < -1) g_pad_x += dx * 4;
        if (dy > 1 || dy < -1) g_pad_y += dy * 4;
        if (g_pad_x < 0) g_pad_x = 0;
        if (g_pad_x > 1023) g_pad_x = 1023;
        if (g_pad_y < 0) g_pad_y = 0;
        if (g_pad_y > 599) g_pad_y = 599;
    }
}

int usb_pad_get_pos(int* x, int* y) {
    if (!g_pad.found) return 0;
    *x = g_pad_x; *y = g_pad_y;
    return 1;
}

// ============================================================================
//  Геймпад (Sega) + объединённый ввод меню
// ============================================================================
#define PAD_CACHE_US 12000
#define PAD_DEBOUNCE_HITS 3
static uint32_t g_pad_cache_t = 0;
static uint16_t g_pad_cur = 0;
static uint16_t g_pad_edge = 0;
static uint16_t g_pad_deb = 0;
static int      g_pad_deb_cnt = 0;

void usb_pad_update(void) {
    uint32_t now = h3_hs_timer_lo_us();
    if (now - g_pad_cache_t < PAD_CACHE_US) return;
    g_pad_cache_t = now;

    uint16_t pad = pad_scan_combined();
    if (pad != g_pad_deb) { g_pad_deb = pad; g_pad_deb_cnt = 1; return; }
    if (++g_pad_deb_cnt < PAD_DEBOUNCE_HITS) return;
    g_pad_deb_cnt = 0;

    g_pad_edge = pad & ~g_pad_cur;
    g_pad_cur = pad;
}

uint16_t usb_pad_edge(void) {
    uint16_t e = g_pad_edge;
    g_pad_edge = 0;
    return e;
}

uint16_t usb_pad_get(void) { return g_pad_cur; }

static uint32_t g_pad_repeat_start = 0;
static int      g_pad_was_repeat = 0;
static int      g_t_prev_pressed = 0;
static int      g_kbd_stash = 0;

void usb_pad_wait_release(void) {
    uint32_t guard = 0;
    while (pad_scan_combined() != 0 && ++guard < 500) udelay(1000);
    if (guard >= 500) sega_pad_init();
    g_pad_repeat_start = 0;
    g_pad_was_repeat = 0;
    g_pad_deb = 0;
    g_pad_deb_cnt = 0;
    g_pad_cache_t = 0;
    g_pad_cur = 0;
    g_pad_edge = 0;
    g_kbd_stash = 0;
}

static int pad_pressed_to_key(uint16_t pressed) {
    if (pressed & 0x0001) return 82;
    if (pressed & 0x0002) return 81;
    if (pressed & 0x0004) return 80;
    if (pressed & 0x0008) return 79;
    if (pressed & 0x0010) return 40;
    if (pressed & 0x0080) return 40;
    if (pressed & 0x0020) return 41;
    if (pressed & 0x0800) return 22;
    return 0;
}

// Объединённый ввод меню: клавиатура И геймпад; возврат HID-сканкода.
int usb_input_poll(void) {
    if (g_kbd_stash) {
        int p = g_kbd_stash;
        g_kbd_stash = 0;
        return p;
    }

    int k = usb_kbd_poll();

    usb_pad_update();
    uint16_t pad = usb_pad_get();
    uint16_t pressed = usb_pad_edge();

    if (pressed && pad) {
        g_pad_was_repeat = 0;
        g_pad_repeat_start = h3_hs_timer_lo_us();
        int j = pad_pressed_to_key(pressed);
        if (j) {
            if (k) g_kbd_stash = k;
            return j;
        }
    }
    if (k) return k;

    if (pad) {
        uint32_t now = h3_hs_timer_lo_us();
        uint32_t elapsed = now - g_pad_repeat_start;
        uint16_t held = pad & 0x000F;
        if (held) {
            if (g_pad_was_repeat) {
                if (elapsed >= PAD_REPEAT_RATE_US) {
                    g_pad_repeat_start = now;
                    if (held & 0x0001) return 82;
                    if (held & 0x0002) return 81;
                    if (held & 0x0004) return 80;
                    if (held & 0x0008) return 79;
                }
            } else {
                if (elapsed >= PAD_REPEAT_DELAY_US) {
                    g_pad_repeat_start = now;
                    g_pad_was_repeat = 1;
                    if (held & 0x0001) return 82;
                    if (held & 0x0002) return 81;
                    if (held & 0x0004) return 80;
                    if (held & 0x0008) return 79;
                }
            }
        }
    }

    int x = 0, y = 0, p = 0;
    if (!usb_touch_poll(&x, &y, &p)) { g_t_prev_pressed = 0; return 0; }
    if (!p) { g_t_prev_pressed = 0; return 0; }
    if (g_t_prev_pressed) return 0;
    g_t_prev_pressed = 1;

    int sx = (x * 1024) / 4096;
    int sy = (y * 600)  / 4096;

    if (sy < 200) return 82;
    if (sy > 400) return 81;
    if (sx < 512) return 41;
    return 40;
}