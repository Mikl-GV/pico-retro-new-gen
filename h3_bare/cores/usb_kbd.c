// usb_kbd.c — USB HID Boot Protocol клавиатура через OHCI (Full/Low-Speed).
// Энумерация: GetDeviceDesc, SetAddress, GetConfigDesc, SetConfig, SetProtocol.
// Опрос: GET_REPORT через intr-передачу (либо GET_REPORT control если intr не реализован).
// Макет: просто периодически читаем 8 байт отчета (modifiers + reserved + 6 keycodes).
#include <string.h>
#include "h3.h"
#include "uart.h"
#include "usb_ohci.h"

#define OHCI_BASE  OHCI1_BASE   // USB-A порт 1 (первый физический разъём)
#define TIMEOUT_MS 5000

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

// ---- HID дескриптор ----
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;   // 33 = HID
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bDescriptorTypeExtra;
    uint16_t wDescriptorLength;
} usb_hid_desc_t;

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
#define HID_SET_PROTOCOL  0x0B   // request is class-specific to HID
#define HID_GET_REPORT    0x01

#define RT_DEVICE     (0x80 | 0x00)
#define RT_INTERFACE  (0x80 | 0x01)

static int g_addr = 0;
static uint8_t g_in_ep = 0;
static uint16_t g_in_maxpkt = 0;
static int g_device_found = 0;
static uint32_t g_ohci_base = OHCI1_BASE;

// ---- USB HID report (8 байт) ----
static uint8_t g_report[8];

// Динамический выбор порта (выбирается при enum)
static uint32_t ohci_active(void) { return g_ohci_base; }

static int ctrl_req(const usb_setup_t* req, uint8_t* data, uint16_t data_len, int dir_in) {
    return usb_ohci_ctrl_transfer(ohci_active(), g_addr, 0,
                                   (const uint8_t*)req, sizeof(*req),
                                   data, data_len, dir_in, TIMEOUT_MS);
}

// Получить дескриптор устройства
static int get_dev_desc(uint8_t* buf) {
    usb_setup_t req = {
        .bmRequestType = RT_DEVICE,
        .bRequest      = GET_DESCRIPTOR,
        .wValue        = (1 << 8),  // descriptor type 1 = device
        .wLength       = 8,         // первый фрагмент (min, чтобы узнать bMaxPacketSize0)
    };
    return ctrl_req(&req, buf, 8, 1);
}

// Полный дескриптор устройства
static int get_dev_desc_full(uint8_t* buf, int len) {
    usb_setup_t req = {
        .bmRequestType = RT_DEVICE,
        .bRequest      = GET_DESCRIPTOR,
        .wValue        = (1 << 8),
        .wLength       = (uint16_t)len,
    };
    return ctrl_req(&req, buf, len, 1);
}

// Получить конфигурационный дескриптор
static int get_cfg_desc_full(uint8_t* buf, int len) {
    usb_setup_t req = {
        .bmRequestType = RT_DEVICE,
        .bRequest      = GET_DESCRIPTOR,
        .wValue        = (2 << 8),  // type 2 = configuration
        .wIndex        = 0,
        .wLength       = (uint16_t)len,
    };
    return ctrl_req(&req, buf, len, 1);
}

// Установить адрес
static int set_addr(int addr) {
    usb_setup_t req = {
        .bmRequestType = 0x00,
        .bRequest      = SET_ADDRESS,
        .wValue        = (uint16_t)addr,
    };
    int r = ctrl_req(&req, 0, 0, 0);
    if (r >= 0) g_addr = addr;
    return r;
}

// Установить конфигурацию
static int set_config(int val) {
    usb_setup_t req = {
        .bmRequestType = 0x00,
        .bRequest      = SET_CONFIGURATION,
        .wValue        = (uint16_t)val,
    };
    return ctrl_req(&req, 0, 0, 0);
}

// Установить HID Boot Protocol
static int set_boot_protocol(void) {
    usb_setup_t req = {
        .bmRequestType = 0x01, // class, interface
        .bRequest      = HID_SET_PROTOCOL,
        .wValue        = 0,    // boot protocol
        .wIndex        = 0,
    };
    return ctrl_req(&req, 0, 0, 0);
}

// GET_REPORT (аварийный опрос, если intr не работает)
static int get_report(uint8_t* buf, int len) {
    usb_setup_t req = {
        .bmRequestType = RT_INTERFACE | 0x01, // class, interface IN
        .bRequest      = HID_GET_REPORT,
        .wValue        = 0x0100, // report type INPUT, id 0
        .wIndex        = 0,
        .wLength       = (uint16_t)len,
    };
    return ctrl_req(&req, buf, len, 1);
}

int usb_kbd_init(void) {
    uint8_t buf[256];
    int r;
    int port_tried = 0;

    // Инициализация обоих OHCI-портов (1 и 2)
    uart_puts("usb: init ohci1...\n");
    usb_ohci_init(OHCI1_BASE);
    port_tried++;
    uart_puts("usb: init ohci2...\n");
    usb_ohci_init(OHCI2_BASE);
    port_tried++;

    // Ищем устройство на любом порту
    int found_port = -1;
    for (int trial = 0; trial < 100; trial++) {
        if (usb_ohci_root_port_connected(OHCI1_BASE, 0)) { found_port = 1; break; }
        if (usb_ohci_root_port_connected(OHCI2_BASE, 0)) { found_port = 2; break; }
        udelay(2000);
    }
    if (found_port < 0) {
        uart_puts("usb: no device on any port\n");
        return -1;
    }
    uart_puts("usb: device on port "); uart_putc('0' + found_port); uart_puts("\n");
    g_ohci_base = (found_port == 1) ? OHCI1_BASE : OHCI2_BASE;

    // --- Энумерация ---
    // 1. Получить дескриптор устройства (первые 8 байт для bMaxPacketSize0)
    memset(buf, 0, 64);
    r = get_dev_desc(buf);
    if (r < 0) { uart_puts("usb: get_dev_desc fail\n"); return -1; }
    int mps = buf[7];  // bMaxPacketSize0

    // 2. Установить адрес (адрес 1)
    if (set_addr(1) < 0) { uart_puts("usb: set_addr fail\n"); return -1; }
    udelay(5000);

    // 3. Получить полный дескриптор
    r = get_dev_desc_full(buf, mps > 18 ? mps : 18);
    if (r < 0) { uart_puts("usb: get_dev_full fail\n"); return -1; }

    // 4. Получить конфигурацию (выделяем 256 байт)
    r = get_cfg_desc_full(buf, 256);
    if (r < 0) { uart_puts("usb: get_cfg fail\n"); return -1; }

    // Парсинг интерфейсов
    int pos = 0;
    int found_hid_kbd = 0;
    g_in_ep = 0;
    while (pos < r && pos < 254) {
        uint8_t len = buf[pos];
        uint8_t type = buf[pos+1];
        if (len == 0) break;
        if (type == 4 && pos + 9 < r) { // interface
            usb_intf_desc_t* intf = (usb_intf_desc_t*)(buf + pos);
            if (intf->bInterfaceClass == 3 &&   // HID
                intf->bInterfaceSubClass == 1 && // Boot
                intf->bInterfaceProtocol == 1) { // Keyboard
                found_hid_kbd = 1;
            }
            pos += len;
        } else if (type == 5 && pos + 7 < r && found_hid_kbd) { // endpoint
            usb_ep_desc_t* ep = (usb_ep_desc_t*)(buf + pos);
            if (ep->bEndpointAddress & 0x80) { // IN endpoint
                g_in_ep = ep->bEndpointAddress;
                g_in_maxpkt = ep->wMaxPacketSize;
            }
            pos += len;
        } else {
            pos += len;
        }
    }

    if (!found_hid_kbd) {
        uart_puts("usb: not a HID keyboard\n");
        return -1;
    }

    // 5. Set config (value 1)
    if (set_config(1) < 0) { uart_puts("usb: set_config fail\n"); return -1; }

    // 6. Set boot protocol
    if (set_boot_protocol() < 0) { uart_puts("usb: boot protocol fail\n"); return -1; }

    g_device_found = 1;
    uart_puts("usb: keyboard ready\n");
    return 0;
}

// Сканкод -> ASCII (EN)
static uint8_t scancode_to_ascii(uint8_t sc, int shift) {
    // Только основные, остальные mapping расширится
    static const uint8_t base[128] = {
        [0]    = 0,    [4]  = 'a', [5]  = 'b', [6]  = 'c', [7]  = 'd',
        [8]    = 'e',  [9]  = 'f', [10] = 'g', [11] = 'h', [12] = 'i',
        [13]   = 'j',  [14] = 'k', [15] = 'l', [16] = 'm', [17] = 'n',
        [18]   = 'o',  [19] = 'p', [20] = 'q', [21] = 'r', [22] = 's',
        [23]   = 't',  [24] = 'u', [25] = 'v', [26] = 'w', [27] = 'x',
        [28]   = 'y',  [29] = 'z', [30] = '1', [31] = '2', [32] = '3',
        [33]   = '4',  [34] = '5', [35] = '6', [36] = '7', [37] = '8',
        [38]   = '9',  [39] = '0', [40] = '\n', [42] = '\x08', // Backspace
        [44]   = ' ',  [43] = '\t',
    };
    static const uint8_t shifted[128] = {
        [4]  = 'A', [5]  = 'B', [6]  = 'C', [7]  = 'D', [8]  = 'E',
        [9]  = 'F', [10] = 'G', [11] = 'H', [12] = 'I', [13] = 'J',
        [14] = 'K', [15] = 'L', [16] = 'M', [17] = 'N', [18] = 'O',
        [19] = 'P', [20] = 'Q', [21] = 'R', [22] = 'S', [23] = 'T',
        [24] = 'U', [25] = 'V', [26] = 'W', [27] = 'X', [28] = 'Y',
        [29] = 'Z', [30] = '!', [31] = '@', [32] = '#', [33] = '$',
        [34] = '%', [35] = '^', [36] = '&', [37] = '*', [38] = '(',
        [39] = ')', [40] = '\n', [42] = '\x08', [44] = ' ',
    };
    if (sc >= 128) return 0;
    return shift ? shifted[sc] : base[sc];
}

int usb_kbd_poll(void) {
    if (!g_device_found || !g_in_ep) return 0;

    // Пробуем GET_REPORT (сплинк-дата)
    uint8_t prev[8];
    memset(prev, 0, 8);
    if (get_report(g_report, 8) < 0) return 0;

    // Смотрим modifiers
    // Используем первый нажатый non-zero keycode из 6
    for (int i = 2; i < 8; i++) {
        if (g_report[i]) {
            uint8_t sc = g_report[i];
            int shift = (g_report[0] & 0x02) ? 1 : 0;
            return sc;  // возвращаем сканкод (ASCII-преобразование в main)
        }
    }
    return 0;
}