// usb_kbd.c — USB HID ввод через OHCI (Full/Low-Speed).
// Устройства сидят на двух портах (OHCI1/OHCI2), классификация по интерфейсам:
//   boot keyboard (3,1,1)                 -> клавиатура (HID-сканкоды)
//   generic HID    (3,0,x)                -> тач GT911 (Waveshare, 0eef:0005)
//   boot mouse     (3,1,2)                -> тачпад I8 Pro (0603:0002, intf1)
// Клавиатура: Full-Speed (Logitech) — GET_REPORT; Low-Speed (I8 Pro) —
// Interrupt IN (однослотовый, донгл не отвечает на GET_REPORT).
// Тачпад I8 Pro — Interrupt IN (слот 1, отдельный ED через NextED).
// Ввод наружу: события сканкодов (usb_kbd_poll), состояние (get_raw),
// курсор/клик тачпада (usb_pad_*). Эмуляторы читают только get_raw/get_mods.
#include <string.h>
#include "h3.h"
#include "h3_hs_timer.h"
#include "uart.h"
#include "usb_ohci.h"
#include "sega_pad.h"

#define TOUCH_BUF   16

// ---- Стандартные дескрипторы USB (используются в enum_port по raw-байтам) ----
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;   // 4 = interface
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
    uint8_t  bDescriptorType;   // 5 = endpoint
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} usb_ep_desc_t;

// ---- Стандартные setup-запросы ----
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

#define RT_DEVICE     (0x80 | 0x00)

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
    int      type;          // 1=kbd, 2=touch
} usb_dev_t;

// HID-устройства: структуры с report-буферами лежат в uncached-области
// (.coherent) — HC пишет в report[] через DMA, а write-back D-cache иначе
// давал бы stale-чтения и гонки с toggle. Курсор/кнопки — обычный BSS.
static usb_dev_t g_kbd   __attribute__((section(".coherent"), aligned(8)));
static usb_dev_t g_touch __attribute__((section(".coherent"), aligned(8)));
static usb_dev_t g_pad   __attribute__((section(".coherent"), aligned(8)));   // тачпад/мышь (intf1, boot mouse)

static uint8_t g_pad_report[8] __attribute__((section(".coherent"), aligned(8)));  // DMA-буфер для interrupt-IN мыши (ep 0x82, maxpkt=8)

// Курсор тачпада (накапливается из dx/dy)
static int g_pad_x = 512;
static int g_pad_y = 300;

static uint8_t g_touch_report[TOUCH_BUF] __attribute__((section(".coherent"), aligned(8)));
static int g_repeat_sc = 0;    // последний сканкод
static int g_was_repeat = 0;   // флаг удержания (сброс в wait_release)
static uint32_t g_repeat_start = 0;
static int g_kbd_pending = 0;  // сканкод, прочитанный wait_release и не отданный

// Автоповтор Sega-геймпада — ЗАМЕДЛЕННЫЙ (в меню D-Pad не должен летать)
#define PAD_REPEAT_DELAY_US 400000   // ~0,4 с до первого повтора
#define PAD_REPEAT_RATE_US  200000   // ~5 шагов/с при удержании

// Автоповтор клавиатуры (меню/браузер): GET_REPORT отдаёт удержанную
// клавишу на каждый опрос — без гейта курсор летел бы неконтролируемо.
#define KBD_REPEAT_DELAY_US 350000
#define KBD_REPEAT_RATE_US  90000

// Антидребезг геймпада: состояние принимается только после PAD_DEBOUNCE_HITS
// ОДИНАКОВЫХ сканов подряд. PCF8574 обновляет выходы на STOP корректно, но
// контакты кнопок (особенно на клонах) дребезжат сами по себе — одиночный
// «мусорный» кадр не должен порождать ложный фронт в меню.
#define PAD_DEBOUNCE_HITS 3

// ---- состояние геймпада (вынесено из usb_input_poll, чтобы можно было сбросить) ----
static uint16_t g_pad_prev = 0;
static uint32_t g_pad_repeat_start = 0;
static int      g_pad_was_repeat = 0;
static int      g_t_prev_pressed = 0;
static uint16_t g_pad_deb = 0;    // последний стабильный кандидат
static int      g_pad_deb_cnt = 0; // сколько одинаковых сканов подряд

// ---- СВОЙ слой геймпада: кэш скана + фронт ----
#define PAD_CACHE_US 2000          // 2 мс: повторные вызовы не дёргают чип
static uint32_t g_pad_cache_t = 0; // время последнего реального скана (мкс)
static uint16_t g_pad_cur = 0;     // стабильное состояние (антидребезг применён)
static uint16_t g_pad_edge = 0;    // фронт нажатия (0→1)

// Обновить состояние геймпада (один реальный скан не чаще раза в PAD_CACHE_US).
// Вызывать один раз за кадр перед usb_pad_get()/usb_pad_edge().
void usb_pad_update(void) {
    uint32_t now = h3_hs_timer_lo_us();
    if (now - g_pad_cache_t < PAD_CACHE_US) return;   // кэш свежий
    g_pad_cache_t = now;

    uint16_t pad = sega_pad_scan();

    // антидребезг: состояние принимается после PAD_DEBOUNCE_HITS одинаковых сканов
    if (pad != g_pad_deb) { g_pad_deb = pad; g_pad_deb_cnt = 1; return; }
    if (++g_pad_deb_cnt < PAD_DEBOUNCE_HITS) return;
    g_pad_deb_cnt = 0;

    // фронт: биты, появившиеся сейчас и отсутствовавшие в прошлом стабильном состоянии
    g_pad_edge = pad & ~g_pad_cur;
    g_pad_cur = pad;
}

// Фронт нажатия: биты, появившиеся с прошлого чтения. Edge ПОТРЕБЛЯЕТСЯ
// (сбрасывается) при вызове — иначе фронт «висит» и повторно срабатывает
// на каждом usb_input_poll в цикле меню (прыжки курсора через 3-4 строки).
uint16_t usb_pad_edge(void) {
    uint16_t e = g_pad_edge;
    g_pad_edge = 0;
    return e;
}

uint16_t usb_pad_get(void) { return g_pad_cur; }

// forward
static int usb_touch_poll(int* x, int* y, int* pressed);
static int kbd_low_speed;
static void kbd_intr_start(void);
static int kbd_read_report(void);

// ---- Control-передача для конкретного устройства ----
static int ctrl_req_dev(usb_dev_t* d, const usb_setup_t* req, uint8_t* data,
                        uint16_t data_len, int dir_in, uint32_t timeout_ms) {
    usb_ohci_set_mps(d->mps0 ? d->mps0 : 8);
    return usb_ohci_ctrl_transfer(d->base, d->addr, 0,
                                  (const uint8_t*)req, sizeof(*req),
                                  data, data_len, dir_in, timeout_ms);
}

// ---- GET_REPORT — короткий таймаут, чтобы не блокировать цикл ----
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

static void cache_inv(uint32_t addr, uint32_t size) {
    addr &= ~0x1Fu;
    uint32_t end = addr + size + 32;
    for (; addr < end; addr += 32)
        __asm volatile("mcr p15, 0, %0, c7, c6, 1" :: "r"(addr));
    __asm volatile("dsb" ::: "memory");
}

// ---- Энумерация одного порта. Возвращает тип: 1=kbd, 2=touch, 0=fail ----
// Если в конфиге есть boot-mouse интерфейс (3,1,2) — заполняет mouse_out
// (тачпад I8 Pro/Novatek: клавиатура + мышь в одном устройстве).
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

    // 1. дескриптор на адресе 0 (первые 8 байт, узнаём ep0 mps)
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
    printf("usb: port=0x%X vid=%04X pid=%04X class=%02X mps=%d\n",
           (unsigned)base, (unsigned)vid, (unsigned)pid, (unsigned)buf[5], mps);

    // 4. конфигурация
    r = ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = RT_DEVICE,
                                          .bRequest = GET_DESCRIPTOR,
                                          .wValue = (2 << 8), .wLength = 256 },
                     buf, 256, 1, 1000);
    if (r < 0) { uart_puts("usb: get_cfg fail\n"); return 0; }

    // 5. парсинг: ищем boot keyboard (3,1,1); иначе generic HID (3,0,x) = тач
    int pos = 0;
    int type = 0;
    int in_mouse_intf = 0;   // находимся внутри интерфейса мыши
    dev->in_ep = 0;
    dev->in_maxpkt = 0;
    while (pos < r && pos < 254) {
        uint8_t len = buf[pos], t = buf[pos + 1];
        if (len == 0) break;
        if (t == 4) { // interface
            usb_intf_desc_t* intf = (usb_intf_desc_t*)(buf + pos);
            in_mouse_intf = 0;
            if (intf->bInterfaceClass == 3 && intf->bInterfaceSubClass == 1 &&
                intf->bInterfaceProtocol == 1) {
                type = 1;
                dev->in_ep = 0; dev->in_maxpkt = 0;
            } else if (intf->bInterfaceClass == 3 && intf->bInterfaceSubClass == 1 &&
                       intf->bInterfaceProtocol == 2 && mouse_out) {
                // boot-mouse (тачпад) — второй интерфейс композита
                mouse_out->base = base;
                mouse_out->addr = 1;  // тот же адрес (SET_ADDRESS=1)
                mouse_out->mps0 = dev->mps0;
                mouse_out->in_ep = 0;
                mouse_out->in_maxpkt = 0;
                mouse_out->type = 3;
                in_mouse_intf = 1;
                uart_puts("usb:   -> MOUSE (boot mouse)\n");
            } else if (intf->bInterfaceClass == 3 && !type) {
                type = 2; // generic HID — кандидат в тач
                dev->in_ep = 0; dev->in_maxpkt = 0;
            }
        } else if (t == 5) { // endpoint
            usb_ep_desc_t* ep_desc = (usb_ep_desc_t*)(buf + pos);
            if (ep_desc->bEndpointAddress & 0x80) { // IN
                if (type == 1 && !dev->in_ep) {
                    dev->in_ep = ep_desc->bEndpointAddress;
                    dev->in_maxpkt = ep_desc->wMaxPacketSize;
                } else if (in_mouse_intf && mouse_out && !mouse_out->in_ep) {
                    mouse_out->in_ep = ep_desc->bEndpointAddress;
                    mouse_out->in_maxpkt = ep_desc->wMaxPacketSize;
                    if (mouse_out->in_ep)
                        mouse_out->found = 1;   // тачпад найден
                }
            }
        }
        pos += len;
    }

    if (!dev->in_ep) {
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
        // boot protocol — для generic может не поддержаться, не критично
        ctrl_req_dev(dev, &(usb_setup_t){ .bmRequestType = 0x21,
                                          .bRequest = HID_SET_PROTOCOL, .wValue = 0,
                                          .wIndex = 0 }, 0, 0, 0, 1000);
        uart_puts("usb:   -> KEYBOARD\n");
    } else {
        uart_puts("usb:   -> TOUCH\n");
    }
    return type;
}

int usb_kbd_init(void) {
    usb_ohci_init(0x01C1B400);   // OHCI1
    usb_ohci_init(0x01C1C400);   // OHCI2

    // ждём устройства на любом порту
    for (int trial = 0; trial < 500; trial++) {
        if (usb_ohci_root_port_connected(0x01C1B400, 0) ||
            usb_ohci_root_port_connected(0x01C1C400, 0))
            break;
        udelay(10000);
    }

    // энумерируем оба порта во временные структуры, потом распределяем по типу
    usb_dev_t d1, d2;
    usb_dev_t mouse1, mouse2;  // второй интерфейс (тачпад)
    memset(&d1, 0, sizeof(d1)); memset(&mouse1, 0, sizeof(mouse1));
    memset(&d2, 0, sizeof(d2)); memset(&mouse2, 0, sizeof(mouse2));
    int t1 = 0, t2 = 0;
    if (usb_ohci_root_port_connected(0x01C1B400, 0)) {
        t1 = enum_port(0x01C1B400, &d1, &mouse1);
    }
    if (usb_ohci_root_port_connected(0x01C1C400, 0)) {
        t2 = enum_port(0x01C1C400, &d2, &mouse2);
    }
    // клавиатура (type=1) приоритетна для g_kbd; тач (type=2) — в g_touch
    if (t1 == 1) { memcpy(&g_kbd, &d1, sizeof(g_kbd)); }
    else if (t1 == 2) { memcpy(&g_touch, &d1, sizeof(g_touch)); }
    if (t2 == 1) {
        if (!g_kbd.found) memcpy(&g_kbd, &d2, sizeof(g_kbd));
    } else if (t2 == 2) {
        if (!g_touch.found) memcpy(&g_touch, &d2, sizeof(g_touch));
    }

    // Тачпад (мышь) — захват из d1/d2
    if (mouse1.type == 3) { memcpy(&g_pad, &mouse1, sizeof(g_pad)); }
    else if (mouse2.type == 3) { memcpy(&g_pad, &mouse2, sizeof(g_pad)); }

    // Тачпад (I8 Pro/Novatek): второй интерфейс boot-mouse (ep 0x82).
    // Читаем через interrupt-IN слот 1 — тем же рабочим паттерном, что и
    // клавиатура (Low-Speed донгл не отвечает на GET_REPORT).
    // Стартуем ДО проверки клавиатуры: навигацию можно вести одним тачпадом.
    if (g_pad.found && g_pad.in_ep) {
        // SET_PROTOCOL(boot) на интерфейс мыши (intf 1)
        usb_ohci_set_mps(g_pad.mps0 ? g_pad.mps0 : 8);
        usb_setup_t req = {
            .bmRequestType = 0x21,
            .bRequest      = HID_SET_PROTOCOL,
            .wValue        = 0,
            .wIndex        = 1,   // интерфейс 1 = мышь
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

    // Канал чтения клавиатуры: Low-Speed (mps0<=8, напр. Novatek 0603:0002)
    // не отвечает на GET_REPORT — читаем через Interrupt IN.
    // Full-Speed (mps0>8, Logitech) — GET_REPORT (проверенный путь).
    kbd_low_speed = (g_kbd.mps0 <= 8) ? 1 : 0;
    if (kbd_low_speed) {
        printf("kbd: Low-Speed (mps=%u) -> Interrupt IN\n", g_kbd.mps0);
        kbd_intr_start();
    } else {
        printf("kbd: Full-Speed (mps=%u) -> GET_REPORT\n", g_kbd.mps0);
    }

    uart_puts("usb: keyboard ready\n");
    return 0;
}

// ---- Клавиатура ----
static int kbd_intr_started = 0;
static int kbd_low_speed = 0;

static void kbd_intr_start(void) {
    if (kbd_intr_started) return;
    if (!g_kbd.found || !g_kbd.in_ep) return;
    usb_ohci_intr_in_start(g_kbd.base, g_kbd.addr,
                           g_kbd.in_ep & 0x7F, g_kbd.report, 8, 0);
    kbd_intr_started = 1;
}

static int kbd_read_report(void) {
    if (!g_kbd.found || !g_kbd.in_ep) return -1;
    if (kbd_low_speed) {
        return (usb_ohci_intr_in_poll(g_kbd.base, g_kbd.report, 8, 0) > 0) ? 0 : -1;
    } else {
        if (get_report_dev(&g_kbd, g_kbd.report, 8) < 0) return -1;
        cache_inv((uint32_t)g_kbd.report, 8);
        return 0;
    }
}

int usb_kbd_poll(void) {
    if (!g_kbd.found || !g_kbd.in_ep) return 0;

    if (g_kbd_pending) {
        int p = g_kbd_pending;
        g_kbd_pending = 0;
        return p;
    }

    int r = kbd_read_report();
    if (r < 0)
        return 0;

    int sc = 0;
    for (int i = 2; i < 8; i++) {
        if (g_kbd.report[i]) { sc = g_kbd.report[i]; break; }
    }

    uint32_t now = h3_hs_timer_lo_us();
    if (sc == 0) { g_repeat_sc = 0; g_was_repeat = 0; return 0; }
    if (sc != g_repeat_sc) {
        g_repeat_sc = sc;
        g_repeat_start = now;
        g_was_repeat = 0;
        return sc;
    }
    if (g_was_repeat) {
        if (now - g_repeat_start >= KBD_REPEAT_RATE_US) {
            g_repeat_start = now;
            return sc;
        }
        return 0;
    }
    if (now - g_repeat_start >= KBD_REPEAT_DELAY_US) {
        g_repeat_start = now;
        g_was_repeat = 1;
        return sc;
    }
    return 0;
}

int usb_kbd_get_raw(uint8_t* buf, int max_buf) {
    if (!g_kbd.found || !g_kbd.in_ep) return 0;
    uint8_t cur[8];
    memcpy(cur, g_kbd.report, 8);
    if (kbd_read_report() < 0) {
        memcpy(cur, g_kbd.report, 8);
    }
    int cnt = 0;
    for (int i = 2; i < 8 && cnt < max_buf; i++)
        if (cur[i]) buf[cnt++] = cur[i];
    return cnt;
}

uint8_t usb_kbd_get_mods(void) {
    if (!g_kbd.found) return 0;
    return g_kbd.report[0]; // modifiers: bit0=LCtrl bit1=LShift bit2=LAlt bit3=LGui bit4=RCtrl bit5=RShift
}

// ---- Тач (Waveshare GT911, 0eef:0005) через GET_REPORT ----
// Формат HID-пакета (6 байт на точку, Report ID=0x01):
//   byte[0] = Report ID (0x01)
//   byte[1] = Status: bit0=TipSwitch (нажат/не нажат), bit1=InRange, bits2-7=ContactID
//   byte[2..3] = X (Little-Endian): X = byte[2] | (byte[3] << 8)
//   byte[4..5] = Y (Little-Endian): Y = byte[4] | (byte[5] << 8)
//   byte[6] (опц.) = Contact Width/Height
// Координаты: 0..4095 (12-bit матрица GT911), масштабируются под разрешение дисплея.
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

    // Report ID должен быть 0x01 (тач)
    if (g_touch_report[0] != 0x01) return 0;

    uint8_t status = g_touch_report[1];
    *pressed = (status & 0x01) != 0;  // Tip Switch

    if (*pressed) {
        // X = LE: byte[2] | byte[3]<<8
        *x = (int)g_touch_report[2] | ((int)g_touch_report[3] << 8);
        // Y = LE: byte[4] | byte[5]<<8
        *y = (int)g_touch_report[4] | ((int)g_touch_report[5] << 8);
        // GT911 выдает 0..4095; экран 1024x600 — масштабировать будет
        // вызывающая сторона (usb_touch_joy).
    }
    return 1;
}

// Фронт нажатия Sega-геймпада: возвращает биты, нажатые ТОЛЬКО что (0→1).
// Через СВОЙ слой (без лишних аппаратных сканов — кэш в usb_pad_update).
uint16_t usb_pad_just_pressed(void) {
    usb_pad_update();
    return usb_pad_edge();
}

// Дождаться, пока ВСЕ кнопки геймпада будут отпущены (и не было повторного
// нажатия), чтобы зажатая кнопка не «доехала» в новое подменю.
void usb_pad_wait_release(void) {
    uint32_t guard = 0;
    while (sega_pad_scan() != 0 && ++guard < 1000000) udelay(1000);
    g_pad_prev = 0;
    g_pad_repeat_start = 0;
    g_pad_was_repeat = 0;
    g_pad_deb = 0;
    g_pad_deb_cnt = 0;
    g_pad_cache_t = 0;
    g_pad_cur = 0;
    g_pad_edge = 0;
}

// Дождаться отпускания КЛАВИАТУРЫ (всех клавиш, кроме модификаторов):
// чтобы зажатый Enter не «доехал» в новое подменю и не активировал первый пункт.
void usb_kbd_wait_release(void) {
    // Донгл не шлёт 00 при отжатии — просто замолкает.
    // 20 мс тишины без нового сканкода = отжатие (r126: было 5 мс — Enter/ESC
    // терялись: донгл молчит 2-10 мс между отчётами). Пришёл сканкод — сброс.
    // Любой сканкод, прочитанный здесь (кроме Enter/ESC), сохраняем в
    // g_kbd_pending — он не должен теряться для следующего usb_kbd_poll.
    g_kbd_pending = 0;
    uint32_t t0 = h3_hs_timer_lo_us();
    for (;;) {
        if (kbd_read_report() == 0) {
            int any = 0;
            int sc = 0;
            for (int i = 2; i < 8; i++) {
                if (g_kbd.report[i]) { sc = g_kbd.report[i]; any = 1; break; }
            }
            if (any) {
                // Другой сканкод (не Enter 28/40 и не ESC 41) — новое нажатие,
                // сохраняем в pending, чтобы не потерялось при выходе.
                if (sc != 28 && sc != 40 && sc != 41) {
                    g_kbd_pending = sc;
                }
                t0 = h3_hs_timer_lo_us();   // перезапуск таймера (ещё не отжато)
            } else {
                break;                       // явный 00 — отжато сразу
            }
        }
        if (h3_hs_timer_lo_us() - t0 > 20000) break;   // 20 мс тишины = отжато
        udelay(1000);
    }
    g_repeat_sc = 0;
    g_was_repeat = 0;
}

// ---- Объединённый ввод для меню: клавиатура, при отсутствии — Sega-геймпад, тач ----
// Возвращает HID-сканкод (82=Up, 81=Down, 79=Right, 80=Left, 40=Enter, 41=ESC)
// либо 0, если ничего не нажато. Тач переводится в «клавиши» по зонам экрана.
// Sega-геймпад: использует СВОЙ слой (usb_pad_update/get/edge) — один аппаратный
// скан на кадр, антидребезг 3 скана, удержание D-Pad = автоповтор.
int usb_input_poll(void) {
    int k = usb_kbd_poll();
    if (k) return k;

    usb_pad_update();
    uint16_t pad = usb_pad_get();
    uint16_t pressed = usb_pad_edge();

    // фронт/спад: обновляем g_pad_prev ДО обработки
    if (pressed && pad) {
        g_pad_was_repeat = 0;
        g_pad_repeat_start = h3_hs_timer_lo_us();
        if (pressed & 0x0001) return 82;   // Up → Up
        if (pressed & 0x0002) return 81;   // Down → Down
        if (pressed & 0x0004) return 80;   // Left → Left
        if (pressed & 0x0008) return 79;   // Right → Right
        if (pressed & 0x0010) return 40;   // A → Enter
        if (pressed & 0x0080) return 40;   // Start → Enter
        if (pressed & 0x0020) return 41;   // B → ESC
        if (pressed & 0x0800) return 22;   // Mode → S (открыть читы в браузере)
    } else if (pad) {
        // удержание СТРЕЛКИ — автоповтор (как клавиатура); кнопки не повторяются
        uint32_t now = h3_hs_timer_lo_us();
        uint32_t elapsed = now - g_pad_repeat_start;
        uint16_t held = pad & 0x000F;   // только D-Pad
        if (!held) return 0;
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

    // Тач: только фронт нажатия (0→1), чтобы палец не «повторял» клавишу
    int x = 0, y = 0, p = 0;
    if (!usb_touch_poll(&x, &y, &p)) { g_t_prev_pressed = 0; return 0; }
    if (!p) { g_t_prev_pressed = 0; return 0; }
    if (g_t_prev_pressed) return 0;   // уже обработали это касание
    g_t_prev_pressed = 1;

    // Экран 1024x600; матрица GT911 0..4095 — нормируем
    int sx = (x * 1024) / 4096;
    int sy = (y * 600)  / 4096;

    if (sy < 200) return 82;                       // верх — Up
    if (sy > 400) return 81;                       // низ — Down
    if (sx < 512) return 41;                       // середина слева — ESC/назад
    return 40;                                     // середина справа — Enter
}

// ---- Тач как джойстик для эмулятора ----
// Зоны: верх 30% = up, низ 30% = down, иначе левая/правая половина = left/right.
// Касание в любом месте = fire.
void usb_touch_joy(uint8_t* dir, uint8_t* fire) {
    int x = 0, y = 0, p = 0;
    if (!usb_touch_poll(&x, &y, &p)) return;
    if (!p) return;
    *fire = 1;
    // диапазон неизвестен (0..1023 или 0..4095) — используем доли
    uint32_t yfrac = (uint32_t)y * 10u / 4096u;
    if (yfrac < 3u) { *dir |= 1; return; }          // up
    if (yfrac > 7u) { *dir |= 2; return; }          // down
    if ((uint32_t)x * 10u / 4096u < 5u) *dir |= 4;  // left
    else                               *dir |= 8;  // right
}

// ---- Тачпад (I8 Pro boot mouse): накопление курсора из dx/dy ----
// Формат boot-mouse отчёта: byte[0]=кнопки(bit0=ЛКМ), byte[1]=dx, byte[2]=dy
// (знаковые), byte[3]=wheel. Читается через interrupt-IN слот 1.
void usb_pad_poll(void) {
    if (!g_pad.found || !g_pad.in_ep) return;

    // Вычитываем ВСЕ накопленные свежие пакеты (донгл шлёт пустые
    // keepalive каждые ~10 мс, они перезатирают буфер — пакет движения
    // иначе теряется).
    while (usb_ohci_intr_in_poll(g_pad.base, g_pad_report, 8, 1) > 0) {
        int8_t dx = (int8_t)g_pad_report[1];
        int8_t dy = (int8_t)g_pad_report[2];

        // Мёртвая зона: микро-движения (джиттер/инерция донгла ±1..3)
        // не двигают крестик — иначе он "уезжает" сам после езды по тачу.
        if (dx > 1 || dx < -1) g_pad_x += dx * 4;
        if (dy > 1 || dy < -1) g_pad_y += dy * 4;
        if (g_pad_x < 0) g_pad_x = 0;
        if (g_pad_x > 1023) g_pad_x = 1023;
        if (g_pad_y < 0) g_pad_y = 0;
        if (g_pad_y > 599) g_pad_y = 599;
    }
}

// Позиция курсора тачпада (0..1023 / 0..599). 0 = тачпада нет.
int usb_pad_get_pos(int* x, int* y) {
    if (!g_pad.found) return 0;
    *x = g_pad_x; *y = g_pad_y;
    return 1;
}