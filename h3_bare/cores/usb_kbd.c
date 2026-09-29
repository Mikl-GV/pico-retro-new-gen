// usb_kbd.c — USB HID ввод через OHCI (Full/Low-Speed).
//
// ============================================================================
//   СЛОИ ДЖОЙСТИКА — НЕ ПУТАТЬ (r155, r157):
//     * МЕНЮ (usb_input_poll → usb_pad_update): скан пада не чаще
//       PAD_CACHE_US (12 мс), антидребезг PAD_DEBOUNCE_HITS (3 одинаковых
//       скана), автоповтор D-Pad 400/200 мс. Кэш 12 мс + паузы циклов меню
//       16 мс намеренно снижают частоту I2C — иначе RMW-гонка с TFT-ядром
//       рвала сканы («джой тупит»).
//     * ИГРЫ (host-слои): прямой sega_pad_scan() раз в кадр в build_input —
//       МИМО кэша/антидребезга. Это нормально; g_pad_cache_t при этом.
//   НЕ МЕНЯТЬ PAD_CACHE_US/PAD_DEBOUNCE_HITS/порядок опросов без прохода
//   «Sega 6-button test + движение в меню + крестовина в NES/MD».
//
//   КЛАВИАТУРА И ГЕЙМПАД РАВНОПРАВНЫ: в usb_input_poll джой-фронт имеет
//   приоритет, но клавиатурное событие не теряется (откладывается на
//   следующий вызов). Обратно: удержание клавиши НЕ глушит джой.
//
//   КЛАВИАТУРА — НЕ «СЫРОЙ ОТЧЁТ», А СЛОЙ СОСТОЯНИЯ (r0.32x):
//   prev/cur-дифф boot-отчётов даёт фронты PRESS/RELEASE, удержание хранится
//   в слое и отдаётся в игры непрерывно (распознавание удержания кнопки).
//   Отпускание: код исчез из отчёта ЛИБО поток отчётов замолчал >40 мс
//   (Low-Speed донгл при отпускании не шлёт пустой отчёт — проверено на
//   железе; при удержании отчёты идут, поэтому удержание не рвётся).
//   Автоповтор — только для навигации в меню (по своим порогам); в играх
//   НЕ повторяем и НЕ автофаертим.
//   Наружу: usb_kbd_poll() (меню), usb_kbd_get_raw()/get_last() (состояние),
//   get_mods().
//
//   «ДОЕЗД» ВВОДА: wait-release (usb_pad_wait_release/usb_kbd_wait_release)
//   + armed-предохранитель в emu.c защищают вход/выход эмулятора от
//   «доехавших» кнопок. Лимиты короткие — это спасение, не баг.
// ============================================================================
//
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
#include "usb_kbd.h"   // KBD_EV_* и прототипы слоя

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

// ============================================================================
//  КЛАВИАТУРА: событийный слой (r0.32x)
//  Физика (avatto-i8-pro-hid.md): новый boot-отчёт приходит только при
//  ИЗМЕНЕНИИ (нажатие/отпускание/комбо), при удержании повторов НЕТ,
//  отпускание — отчёт с обнулёнными кодами. Поэтому prev/cur-дифф даёт
//  точные фронты, а таймаут-сброса состояния НЕ нужно (и он вреден:
//  «дёргал» бы удержание на медленных кадрах).
// ============================================================================
#define KBD_Q_LEN 24
static uint8_t  g_q_sc[KBD_Q_LEN], g_q_ev[KBD_Q_LEN];
static int      g_q_r = 0, g_q_w = 0;

static uint8_t  g_held[6];          // текущие нажатые коды (как в отчёте)
static int      g_held_n = 0;
static uint32_t g_down_t[256];      // мкс нажатия (0 = не нажата)
static uint32_t g_rep_t[256];       // мкс последнего REPEAT-события
static uint8_t  g_mods = 0;         // модификаторы последнего отчёта

// Клавиатурный сканкод, отложенный usb_input_poll (в тот же проход сработал
// фронт геймпада). Общий для меню: чистится в wait_release, чтобы «не доехал»
// в подменю/эмулятор.
static int g_kbd_stash = 0;

// Выход по ESC x3 — только для Low-Speed донгла (у него не работает длинное
// удержание, см. usb_kbd.h). Детект в kbd_process_report().
#define ESC3_WINDOW_US 700000
#define ESC3_NEED      3
static int      g_esc_cnt = 0;
static uint32_t g_esc_last_t = 0;
static int      g_esc3_fired = 0;

// Автоповтор клавиатуры (меню/браузер): удержали стрелку — курсор бежит.
// В играх повтор/автофайр НЕ применяется (игра сама обрабатывает удержание).
#define KBD_REPEAT_DELAY_US 350000
#define KBD_REPEAT_RATE_US   90000

// Распознавание отпускания (r0.32x, Low-Speed донгл I8 Pro):
// донгл при ОТПУСКАНИИ не шлёт пустой отчёт — просто замолкает (проверено на
// железе; «пустой отчёт» из avatto-i8-pro-hid.md на этой связке не приходит).
// При удержании отчёты продолжают идти, поэтому: тишина > KBD_IDLE_RELEASE_US
// при зажатых клавишах = их отпустили (или связь потеряна — тоже безопасно
// «отпустить»). Так же делают простые эмуляторы/драйверы, чей стек не видит
// ключевого апдейта устройства; тайминг согласован с прежним рабочим r172 (25 мс).
#define KBD_IDLE_RELEASE_US 40000

// Автоповтор Sega-геймпада — ЗАМЕДЛЕННЫЙ (в меню D-Pad не должен летать)
#define PAD_REPEAT_DELAY_US 400000
#define PAD_REPEAT_RATE_US  200000

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
// r155: кэш 12 мс (было 2 мс). В бесконечном цикле input_wait() главного
// меню без паузы скан каждые 2 мс = 500 сканов/с × ~1,4 мс ≈ 70% времени
// шины — любая RMW-пакость CPU1 (тач/LED на PA_DAT) попадала в идущий I2C,
// срывала бит и «тупил» джой. 12 мс достаточно для меню (60 Гц + автоповтор)
// и меньше окно пересечения с TFT-ядром на порядок.
#define PAD_CACHE_US 12000         // 12 мс: повторные вызовы не дёргают чип
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

// Время последнего свежего отчёта (диагностика) и лимит частоты поллов.
// r0.32x: переарм TD/ED в usb_ohci_intr_in_poll делается ТОЛЬКО при снятии
// пакета (см. usb_ohci.c), поэтому частые поллы больше не рассинхронизируют
// цепочку. Здесь — не чаще одного аппаратного опроса за 1000 мкс.
static uint32_t g_raw_fresh_t = 0;
static uint32_t g_ls_poll_t = 0;
static int      g_ls_poll_res = -1;
static int      g_ls_err = 0;      // подряд ошибок завершения TD → перезапуск

// 0 = есть свежий отчёт в g_kbd.report; -1 = нет/ошибка.
// Таймаут-сброса состояния НЕТ: «тишина» при удержании нормальна (донгл не
// шлёт повторов) — отпускание приходит отдельным отчётом и обрабатывается
// prev/cur-диффом в kbd_process_report().
static int kbd_read_report(void) {
    if (!g_kbd.found || !g_kbd.in_ep) return -1;
    if (kbd_low_speed) {
        uint32_t now = h3_hs_timer_lo_us();
        if (now - g_ls_poll_t >= 1000u) {
            g_ls_poll_t = now;
            int r = usb_ohci_intr_in_poll(g_kbd.base, g_kbd.report, 8, 0);
            if (r > 0) {
                g_ls_poll_res = 0; g_raw_fresh_t = now; g_ls_err = 0;
            } else if (r == 0) {
                g_ls_poll_res = -1;               // пакета нет (норма при удержании)
            } else {
                g_ls_poll_res = -1;               // ошибка TD (CRC/timeout)
                if (++g_ls_err >= 8) { usb_kbd_restart_intr(); g_ls_err = 0; }
            }
        }
        return g_ls_poll_res;
    } else {
        if (get_report_dev(&g_kbd, g_kbd.report, 8) < 0) return -1;
        cache_inv((uint32_t)g_kbd.report, 8);
        g_raw_fresh_t = h3_hs_timer_lo_us();
        return 0;
    }
}

// ---- очередь событий ----
static void q_push(uint8_t sc, uint8_t ev) {
    int nxt = (g_q_w + 1) % KBD_Q_LEN;
    if (nxt == g_q_r) g_q_r = (g_q_r + 1) % KBD_Q_LEN;   // переполнение: теряем старое
    g_q_sc[g_q_w] = sc; g_q_ev[g_q_w] = ev;
    g_q_w = nxt;
}
static int q_pop(uint8_t* sc, uint8_t* ev) {
    if (g_q_r == g_q_w) return 0;
    *sc = g_q_sc[g_q_r]; *ev = g_q_ev[g_q_r];
    g_q_r = (g_q_r + 1) % KBD_Q_LEN;
    return 1;
}

// prev/cur-дифф нового отчёта → фронты (PRESS/RELEASE) + состояние удержания.
static void kbd_process_report(const uint8_t* rep) {
    uint32_t now = h3_hs_timer_lo_us();
    uint8_t codes[6]; int cn = 0;
    for (int i = 2; i < 8; i++) if (rep[i]) codes[cn++] = rep[i];

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
            uint32_t t = now ? now : 1;
            g_down_t[sc] = t; g_rep_t[sc] = t;
            q_push(sc, KBD_EV_PRESS);
            // Выход: ESC x3 подряд в окне (только Low-Speed донгл).
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
    g_mods = rep[0];
}

// Отпускание по «тишине потока отчётов» (Low-Speed донгл молчит при
// отпускании, см. KBD_IDLE_RELEASE_US).
static void kbd_release_all(void) {
    for (int i = 0; i < g_held_n; i++) {
        uint8_t sc = g_held[i];
        g_down_t[sc] = 0; g_rep_t[sc] = 0;
        q_push(sc, KBD_EV_RELEASE);
    }
    g_held_n = 0;
}

// Опросить HW и обновить состояние (очередь фронтов PRESS/RELEASE).
// Автоповтор НЕ кладём в очередь (иначе она растёт во время игры) —
// его выдаёт usb_kbd_poll() по удержанию.
static void kbd_scan(void) {
    if (!g_kbd.found || !g_kbd.in_ep) return;
    if (kbd_read_report() == 0)
        kbd_process_report(g_kbd.report);

    // Распознавание отпускания: пока клавиши удерживаются, отчёты идут;
    // тишина дольше порога = отпущено (или потеря связи) — отпускаем.
    if (g_held_n > 0 && g_raw_fresh_t != 0 &&
        (h3_hs_timer_lo_us() - g_raw_fresh_t > KBD_IDLE_RELEASE_US))
        kbd_release_all();
}

// r0.32x: перезапуск interrupt-IN цепочки ED/TD с нуля (после ошибок TD или
// выхода из эмулятора). Устройство НЕ перечисляется, состояние клавиш НЕ
// сбрасывается — только перевешивается TD.
void usb_kbd_restart_intr(void) {
    kbd_intr_started = 0;
    g_ls_poll_res = -1;
    g_ls_err = 0;
    kbd_intr_start();
}

// ---- публичный API ----
// Меню: сканкод PRESS (из очереди фронтов); если свежих нажатий нет —
// автоповтор удержанной клавиши (свои пороги, без сброса состояния).
int usb_kbd_poll(void) {
    if (!g_kbd.found || !g_kbd.in_ep) return 0;
    kbd_scan();
    uint8_t sc, ev;
    while (q_pop(&sc, &ev))
        if (ev == KBD_EV_PRESS) return sc;   // RELEASE меню не нужно

    uint32_t now = h3_hs_timer_lo_us();
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

// Игры: текущее УДЕРЖИВАЕМОЕ состояние (по слою, а не «сырой» DMA-буфер).
int usb_kbd_get_raw(uint8_t* buf, int max_buf) {
    if (!g_kbd.found || !g_kbd.in_ep) return 0;
    kbd_scan();
    int cnt = 0;
    for (int i = 0; i < g_held_n && cnt < max_buf; i++) buf[cnt++] = g_held[i];
    return cnt;
}

uint8_t usb_kbd_get_mods(void) {
    if (!g_kbd.found) return 0;
    return g_mods;   // модификаторы последнего обработанного отчёта
}

// Состояние без нового опроса USB (emu_esc_hold: не «воровать» отчёт за кадр).
int usb_kbd_get_last(uint8_t* buf, int max_buf) {
    if (!g_kbd.found) return 0;
    int cnt = 0;
    for (int i = 0; i < g_held_n && cnt < max_buf; i++) buf[cnt++] = g_held[i];
    return cnt;
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
// r155: ждём не дольше 500 мс — если пад «залип» (сбой I2C после гонки PA_DAT
// с TFT-ядром), переинициализируем PCF8574 и выходим, иначе меню висло
// на тысячи секунд (старый лимит 1 000 000 итераций × 1 мс).
void usb_pad_wait_release(void) {
    uint32_t guard = 0;
    while (sega_pad_scan() != 0 && ++guard < 500) udelay(1000);
    if (guard >= 500) {
        // Залипший пад: сброс PCF8574 (0xFF → TH=1 idle), как в sega_pad_test_run
        sega_pad_init();
    }
    g_pad_prev = 0;
    g_pad_repeat_start = 0;
    g_pad_was_repeat = 0;
    g_pad_deb = 0;
    g_pad_deb_cnt = 0;
    g_pad_cache_t = 0;
    g_pad_cur = 0;
    g_pad_edge = 0;
    g_kbd_stash = 0;   // отложенный клавиатурный сканкод не должен «доехать»
}

// Дождаться отпускания КЛАВИАТУРЫ: чтобы зажатый Enter/ESC не «доехал» в
// новое подменю и не активировал первый пункт.
// r0.32x: дожидаемся, пока СЛОЙ не покажет ноль удержанных клавиш (или 800 мс
// защитного лимита). Работает и «явный 00», и случай «донгл замолчал, но
// отчёт так и не пришёл» (проверяем состояние, а не тишину по времени).
void usb_kbd_wait_release(void) {
    uint32_t t0 = h3_hs_timer_lo_us();
    for (;;) {
        kbd_scan();
        if (g_held_n == 0) break;
        if (h3_hs_timer_lo_us() - t0 > 800000u) break;
        udelay(1000);
    }
    // Кнопка выхода не должна «доехать»: чистим состояние, очередь и стэш.
    g_q_r = g_q_w = 0;
    g_kbd_stash = 0;
    g_held_n = 0;
    for (int i = 0; i < 256; i++) { g_down_t[i] = 0; g_rep_t[i] = 0; }
}

// Дождаться отпускания ВСЕХ кнопок (клавиатура + Sega-геймпад) при ВЫХОДЕ из
// эмулятора: кнопка выхода держится ~0.9-1 с (ESC или Start+Mode), и без этой
// паузы она «доезжает» в меню — Start→Enter активирует первый пункт, т.е.
// пользователь «автоматически попадает в первую строку». r0.204
void usb_wait_release_all(void) {
    usb_kbd_wait_release();   // клавиатура (тишина 20 мс без новых сканкодов)
    usb_pad_wait_release();   // геймпад (со сбросом кэша/фронтов; залип → re-init)
}

// Джой-фронт → сканкод меню (та же таблица, что была внутри usb_input_poll)
static int pad_pressed_to_key(uint16_t pressed) {
    if (pressed & 0x0001) return 82;   // Up → Up
    if (pressed & 0x0002) return 81;   // Down → Down
    if (pressed & 0x0004) return 80;   // Left → Left
    if (pressed & 0x0008) return 79;   // Right → Right
    if (pressed & 0x0010) return 40;   // A → Enter
    if (pressed & 0x0080) return 40;   // Start → Enter
    if (pressed & 0x0020) return 41;   // B → ESC
    if (pressed & 0x0800) return 22;   // Mode → S (открыть читы в браузере)
    return 0;
}

// ---- Объединённый ввод для меню: клавиатура И Sega-геймпад равноправны ----
// Возвращает HID-сканкод (82=Up, 81=Down, 79=Right, 80=Left, 40=Enter, 41=ESC)
// либо 0, если ничего не нажато. Тач переводится в «клавиши» по зонам экрана.
// Sega-геймпад: использует СВОЙ слой (usb_pad_update/get/edge) — один аппаратный
// скан на кадр, антидребезг 3 скана, удержание D-Pad = автоповтор.
// r155: раньше клавиатура стояла первой и «перебивала» джой (if (k) return k —
// при нажатой клавише или её автоповторе джой не опрашивался вовсе).
// Теперь джой-фронт обрабатывается всегда; клавиатурное событие, пришедшее
// в тот же проход, откладывается и не теряется.
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

    // фронт/спад: обновляем g_pad_prev ДО обработки
    if (pressed && pad) {
        g_pad_was_repeat = 0;
        g_pad_repeat_start = h3_hs_timer_lo_us();
        int j = pad_pressed_to_key(pressed);
        if (j) {
            // клавиатурное событие не теряем — выдадим следующим вызовом
            if (k) g_kbd_stash = k;
            return j;
        }
        // неизвестный бит фронта — не теряем клавиатуру, идём дальше
    }
    if (k) return k;

    if (pad) {
        // удержание СТРЕЛКИ — автоповтор (как клавиатура); кнопки не повторяются
        uint32_t now = h3_hs_timer_lo_us();
        uint32_t elapsed = now - g_pad_repeat_start;
        uint16_t held = pad & 0x000F;   // только D-Pad
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