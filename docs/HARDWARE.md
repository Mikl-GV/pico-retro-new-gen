# Железо (Orange Pi Lite + 7" HDMI LCD + XPT2046)

## Плата

**Orange Pi Lite** — SoC Allwinner H3 (4× Cortex-A7 @ 1.2 ГГц, Mali-400),
512 МБ DDR3. Питание — DC 5V (включается кнопкой). microUSB — только OTG
(не питание, не FEL-подключение в обычном режиме).

Ключевое отличие от RPi: **40-pin разъём совместим по физическим номерам
пинов с Raspberry Pi 3/4**, но GPIO H3 на этих пинах — другие.

## Распиновка 40-pin (проверено, из документации платы)

| Пин | OPI Lite (H3) | Пин | OPI Lite (H3) |
|-----|---------------|-----|---------------|
| 1  | 3,3 В | 2  | 5 В |
| 3  | PA12 (TWI0_SDA/DI_RX) | 4  | 5 В |
| 5  | PA11 (TWI0_SCK/DI_TX) | 6  | GND |
| 7  | PA6 (SIM_PWREN/PWM1) | 8  | PA13 (SPI1_CS/UART3_TX) |
| 9  | GND | 10 | PA14 (SPI1_CLK/UART3_RX) |
| 11 | PA1 (UART2_RX/JTAG_CK) | 12 | PD14 |
| 13 | PA0 (UART2_TX/JTAG_MS) | 14 | GND |
| 15 | PA3 (UART2_CTS/JTAG_DI) | 16 | PC4 |
| 17 | 3,3 В | 18 | PC7 |
| **19** | **PC0 (SPI0_MOSI)** | 20 | GND |
| **21** | **PC1 (SPI0_MISO)** | **22** | **PA2 (UART2_RTS/PA_EINT2)** |
| **23** | **PC2 (SPI0_CLK)** | 24 | **PC3 (SPI0_CS)** |
| 25 | GND | **26** | **PA21 (PCM0_DIN/PA_EINT21)** |
| 27 | PA19 (PCM0_CLK/TWI1_SDA) | 28 | PA18 (PCM0_SYNC/TWI1_SCK) |
| 29 | PA7 (SIM_CLK) | 30 | GND |
| 31 | PA8 (SIM_DATA) | 32 | PG8 (UART1_RTS) |
| 33 | PA9 (SIM_RST) | 34 | GND |
| 35 | PA10 (SIM_DET) | 36 | PG9 (UART1_CTS) |
| 37 | PA20 (PCM0_DOUT/SIM_VPPEN) | 38 | PG6 (UART1_TX) |
| 39 | GND | 40 | PG7 (UART1_RX) |

## Дисплей

7″ HDMI LCD 1024×600, резистивный тач XPT2046. Дисплей надет на **первые 26
пинов** 40-pin разъёма (на физические номера 1–26, 1:1 с RPi 3/4).

### HDMI

- Кабель HDMI → разъём HDMI платы
- Режим: **1024×600 @ 60 Гц**, pixel clock 51.2 МГц
- Панель **без сигнала HPD** — в прошивке включён force-hotplug
  (инициализация идёт, даже если HPD нет)

### Сенсор XPT2046

Соединение дисплей ↔ плата (26-пин шлейф/гребёнка):

| Дисплей | OPI Lite пин | H3 GPIO | Режим |
|---------|--------------|---------|-------|
| 19 TP_SI  | 19 | PC0  (SPI0_MOSI) | выход |
| 21 TP_SO  | 21 | PC1  (SPI0_MISO) | вход  |
| 22 TP_IRQ | 22 | PA2  | вход, низкий = касание |
| 23 TP_SCK | 23 | PC2  (SPI0_CLK)  | выход |
| 26 TP_CS  | 26 | PA21 | выход, низкий = активно |

Интерфейс — **bit-bang SPI** (реализован в `h3_bare/cores/touch_xpt2046.c`)
на пинах PC0/PC1/PC2, CS — PA21, IRQ — PA2.

> Примечание: на некоторых платах TP_CS дисплея подключается к пину 24
> (SPI0_CS = PC3). Здесь — фактически пин 26 (PA21), как в таблице
> разъёма. Если сенсор не отвечает — проверьте мультиметром уровень PA21
> и при необходимости поправьте `PIN_CS` в драйвере.

## UART (отладка)

- **TX — пин 14 (PA4)**
- **RX — пин 15 (PA5)**
- 115200 8N1
- Подключение: USB-UART (FTDI/CP2102), TX платы → RX адаптера, GND → GND

## Питание

- Плата — **только DC 5V** через круглый/Type-C разъём (включается кнопкой)
- microUSB — **только OTG**, для питания/fel не используется
- Дисплей питается от платы: 5V (пин 2/4), 3.3V (пин 1), GND (пин 6)

## Память (карта адресов)

| Адрес | Назначение |
|-------|------------|
| 0x40000000 | Код (загрузка U-Boot `go`) |
| 0x41000000 | Uncached section |
| 0x7C000000 | ROM-буфер с SD (до 24 МБ) |
| 0x7E000000 | Emu framebuffer 320×240 RGB565 |
| 0x7FC00000 | HDMI framebuffer 1024×600 XRGB8888 |
| 0x80000000 | Стек (конец DRAM) |

## SD-карта (SDMMC0)

- Контроллер **MMC0** @ `0x01C0F000` (регистры как в `sunxi_mmc` u-boot)
- FIFO на **0x200** (НЕ 0x44, как в h3.h!)
- CCU: gate `BUS_CLK_GATING0` (0x060) bit 8, reset `BUS_SOFT_RST0` (0x2C0) bit 8, такт `SDMMC0_CLK` (0x088, source osc24M, M=0 → 24МГц)
- Пины: **PF0=D1, PF1=D0, PF2=CLK, PF3=CMD, PF4=D3, PF5=D2** — все alt 2, pull-up на CMD/DAT
- Поддерживается только SDHC/SDXC (ACMD41, bit 30 = 1), блок 512 байт
- **На железе драйвер `sd.c`/`fat.c` ещё НЕ тестирован**

### Готовый SD-образ (`build/h3_bare.img`, 64 МБ)

| Компонент | Позиция |
|-----------|---------|
| MBR, FAT32 LBA (0x0C) | сектор 0 |
| U-Boot SPL (eGON.BT0) | 8K |
| FAT32 `H3_RETRO` | 16M |
| `h3_bare.bin` | FAT |
| `boot.scr` (fatload → go) | FAT |
| `/roms/a2600/` /a5200/ /a7800/ | FAT |

Запись: `sudo dd if=build/h3_bare.img of=/dev/sdX bs=1M conv=fsync`

## WiFi (RTL8189FTV)

Orange Pi Lite несёт чип **Realtek RTL8189FTV** (SDIO, MMC1, порт G: PG0=CLK, PG1=CMD, PG2=D0, PG3=D1, alt 2).

**В bare-metal пока НЕ реализовано.** Требуется:
1. SDIO-стек (MMC1 + CMD52/CMD53)
2. Загрузка firmware чипа
3. 802.11 (b/g/n) + TCP/IP стек

— отдельная большая задача.

## Дисплейные тайминги HDMI (для справки)

- Актива: 1024×600 @ 60 Гц, pixel clock 51.2 МГц
- H: hactive 1024, hfp 160, hbp 88, hsync 40
- V: vactive 600, vfp 12, vbp 20, vsync 3
- Флаги: HSYNC_LOW | VSYNC_LOW
- Панель без HPD → force-hotplug в прошивке