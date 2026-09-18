// usb_kbd.c — USB HID клавиатура + тач (GT911) через OHCI (Full/Low-Speed).
// Два устройства могут сидеть на двух разных портах (OHCI1/OHCI2).
// Классификация по HID-интерфейсу:
//   boot keyboard (class=3, subclass=1, protocol=1) -> клавиатура
//   generic HID  (class=3, subclass=0)               -> тач
// Энумерация обоих портов последовательно (общие ED/TD, по одному за раз).
#include <string.h>
#include "h3.h"
#include "h3_hs_timer.h"
#include "uart.h"
#include "usb_ohci.h"
#include "sega_pad.h"

#define OHCI1_BASE  0x01C1B400
#define OHCI2_BASE  0x01C1C400
#define TIMEOUT_MS  5000
#define TOUCH_BUF   16

// ---- Стандартные дескрипторы USB ----
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;    // 1 = device
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} usb_dev_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;   // 2 = config
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;
    uint8_t  bConfigurationValue;
    uint8_t  iConfiguration;
    uint8_t  bmAttributes;
    uint8_t  bMaxPower;
} usb_cfg_desc_t;

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

static usb_dev_t g_kbd;
static usb_dev_t g_touch;

static uint8_t g_touch_report[TOUCH_BUF];
static uint32_t g_repeat_start = 0;
static int g_repeat_sc = 0;
static int g_was_repeat = 0;

#define KBD_REPEAT_DELAY_US 200000
#define KBD_REPEAT_RATE_US  50000

// Автоповтор Sega-геймпада — ЗАМЕДЛЕННЫЙ (в меню D-Pad не должен летать)
#define PAD_REPEAT_DELAY_US 400000   // ~0,4 с до первого повтора
#define PAD_REPEAT_RATE_US  200000   // ~5 шагов/с при удержании

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
static void dump_hid_report_desc(usb_dev_t* d);
static int usb_touch_poll(int* x, int* y, int* pressed);

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
static int enum_port(uint32_t base, usb_dev_t* dev) {
    uint8_t buf[256];
    int r;

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
    int cur_boot = 0;
    dev->in_ep = 0;
    dev->in_maxpkt = 0;
    while (pos < r && pos < 254) {
        uint8_t len = buf[pos], t = buf[pos + 1];
        if (len == 0) break;
        if (t == 4) { // interface
            usb_intf_desc_t* intf = (usb_intf_desc_t*)(buf + pos);
            if (intf->bInterfaceClass == 3 && intf->bInterfaceSubClass == 1 &&
                intf->bInterfaceProtocol == 1) {
                cur_boot = 1;
                type = 1;
                dev->in_ep = 0; dev->in_maxpkt = 0;
            } else if (intf->bInterfaceClass == 3 && !type) {
                cur_boot = 0;
                type = 2; // generic HID — кандидат в тач
                dev->in_ep = 0; dev->in_maxpkt = 0;
            } else {
                cur_boot = 0;
            }
        } else if (t == 5 && type) { // endpoint внутри HID-интерфейса
            usb_ep_desc_t* ep = (usb_ep_desc_t*)(buf + pos);
            if (ep->bEndpointAddress & 0x80) { // IN
                if (!dev->in_ep) {
                    dev->in_ep = ep->bEndpointAddress;
                    dev->in_maxpkt = ep->wMaxPacketSize;
                }
            }
        }
        pos += len;
    }
    (void)cur_boot;

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
    memset(&d1, 0, sizeof(d1));
    memset(&d2, 0, sizeof(d2));
    int t1 = 0, t2 = 0;
    if (usb_ohci_root_port_connected(0x01C1B400, 0)) {
        t1 = enum_port(0x01C1B400, &d1);
    }
    if (usb_ohci_root_port_connected(0x01C1C400, 0)) {
        t2 = enum_port(0x01C1C400, &d2);
    }
    // клавиатура (type=1) приоритетна для g_kbd; тач (type=2) — в g_touch
    if (t1 == 1) { memcpy(&g_kbd, &d1, sizeof(g_kbd)); }
    else if (t1 == 2) { memcpy(&g_touch, &d1, sizeof(g_touch)); }
    if (t2 == 1) {
        if (!g_kbd.found) memcpy(&g_kbd, &d2, sizeof(g_kbd));
    } else if (t2 == 2) {
        if (!g_touch.found) memcpy(&g_touch, &d2, sizeof(g_touch));
    }

    if (!g_kbd.found) {
        uart_puts("usb: no keyboard found\n");
        return -1;
    }

    // Диагностика тача: HID Report Descriptor (только один раз)
    if (g_touch.found && g_touch.in_ep) {
        dump_hid_report_desc(&g_touch);
    }

    uart_puts("usb: keyboard ready\n");
    return 0;
}

// ---- Клавиатура ----
static int kbd_read_report(void) {
    if (!g_kbd.found || !g_kbd.in_ep) return -1;
    if (get_report_dev(&g_kbd, g_kbd.report, 8) < 0) return -1;
    cache_inv((uint32_t)g_kbd.report, 8);
    return 0;
}

int usb_kbd_poll(void) {
    if (!g_kbd.found || !g_kbd.in_ep) return 0;
    uint8_t cur[8];
    memcpy(cur, g_kbd.report, 8);
    if (kbd_read_report() < 0) return 0;

    // первая нажатая клавиша (приоритет по порядку в отчёте)
    int sc = 0;
    for (int i = 2; i < 8; i++) {
        if (g_kbd.report[i]) { sc = g_kbd.report[i]; break; }
    }

    uint32_t now = h3_hs_timer_lo_us();

    if (!sc) {
        g_repeat_sc = 0;
        g_was_repeat = 0;
        return 0;
    }

    // была ли эта клавиша в предыдущем отчёте (удержание)?
    int in_prev = 0;
    for (int j = 2; j < 8; j++)
        if (cur[j] == sc) { in_prev = 1; break; }

    if (!in_prev) {
        // новое нажатие
        g_repeat_sc = sc;
        g_repeat_start = now;
        g_was_repeat = 0;
        return sc;
    }

    // удержание той же клавиши — автоповтор
    if (sc == g_repeat_sc) {
        uint32_t elapsed = now - g_repeat_start;
        if (g_was_repeat) {
            if (elapsed >= KBD_REPEAT_RATE_US) {
                g_repeat_start = now;
                return sc;
            }
        } else {
            if (elapsed >= KBD_REPEAT_DELAY_US) {
                g_repeat_start = now;
                g_was_repeat = 1;
                return sc;
            }
        }
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

// Чтение HID Report Descriptor (type 0x22) — точный формат отчёта тача
static void dump_hid_report_desc(usb_dev_t* d) {
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    usb_setup_t req = {
        .bmRequestType = 0x81,      // host->dev, standard, device
        .bRequest      = GET_DESCRIPTOR,
        .wValue        = (0x22 << 8),  // HID report descriptor
        .wIndex        = 0,
        .wLength       = 64,
    };
    int r = ctrl_req_dev(d, &req, buf, 64, 1, 1000);
    printf("hid_report_desc r=%d:", r);
    if (r > 0) {
        for (int i = 0; i < r; i++) printf(" %02X", buf[i]);
    }
    printf("\n");
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

// Сбросить состояние геймпада/тача (вызывается при входе в меню/подменю).
void usb_input_clear(void) {
    g_pad_prev = 0;
    g_pad_repeat_start = 0;
    g_pad_was_repeat = 0;
    g_t_prev_pressed = 0;
    g_pad_deb = 0;
    g_pad_deb_cnt = 0;
    g_pad_cache_t = 0;   // принудительно свежий скан на следующем usb_pad_update
    g_pad_cur = 0;
    g_pad_edge = 0;
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
    // Ждём, пока в отчёте не останется ни одной зажатой клавиши
    for (uint32_t guard = 0; guard < 1000000; guard++) {
        if (kbd_read_report() < 0) break;
        int any = 0;
        for (int i = 2; i < 8; i++)
            if (g_kbd.report[i]) { any = 1; break; }
        if (!any) break;
        udelay(5000);
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