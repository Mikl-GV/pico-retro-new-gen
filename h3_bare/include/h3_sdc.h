#ifndef H3_SDC_H
#define H3_SDC_H

#include <stdint.h>

/* SD/MMC хост-контроллер Allwinner H3.
 *
 * Используется контроллер SDC1 (база 0x01C10000) для связи с WiFi-чипом
 * RTL8189FTV по шине SDIO. PG0..PG5 = SDC1 CLK/CMD/D0-D3.
 *
 * Низкоуровневый доступ: инициализация тактов/пинов/сброса, отправка команд
 * и чтение/запись данных через FIFO. Этого достаточно, чтобы реализовать
 * SDIO CMD52/CMD53 (байтовый/блочный доступ к функциям чипа).
 */

/* Расширенный диагностический лог WiFi (verbose). Включает power-seq trace,
   SDC1-диагностику, rf_dump, macchk, txpre-дамп и т.п. — детальные трассы,
   не нужные для обычной работы. По умолчанию ВЫКЛ; для восстановления
   полного лога раскомментируй WIFI_VERBOSE_LOG ниже. */
/* #define WIFI_VERBOSE_LOG 1 */
#ifdef WIFI_VERBOSE_LOG
#define WLOG(...)   uart_print(__VA_ARGS__)
#define WLOG_HEX(n) uart_print_hex((uint32_t)(n))
#define WLOG_INT(n) uart_print_int((int)(n))
#else
#define WLOG(...)   do {} while (0)
#define WLOG_HEX(n) do { (void)(n); } while (0)
#define WLOG_INT(n) do { (void)(n); } while (0)
#endif

/* W.557/W.557 [итог]: диагностика TX/RX hot-path своё отработала
   (W.550-W.556: дескриптор/очереди/скорость подтверждены). UART-вывод
   в hot path стоил тактов и мог сбивать приём/передачу — выключен.
   Для отладки задай в командной строке: -DWIFI_TX_DIAG=1 */
#ifndef WIFI_TX_DIAG
#define WIFI_TX_DIAG 0
#endif
/* W.625/W.634/W.641: перевести SDIO-шину на 50 МГц / 4-bit / HS
   (эталон Lakka), фазы sample/output по ccu_phase H3 (50 МГц: output=90°,
   sample=120°). W.625-W.634 data-фаза слетала из-за неверной записи EHS
   (били бит0 CCCR 0x13 вместо бит1) и незаведённых PG4/PG5 — после фиксов
   (W.636/W.639) 50 МГц/4-bit/HS РАБОТАЕТ, data-probe проходит (W.641).
   Флаг: 1 = 50 МГц/4-bit/HS, 2 = тест «4-bit на низкой частоте»,
   0 = 1-bit/низкая сетка (старое поведение W.235-248). */
#ifndef WIFI_SDIO_50MHZ
#define WIFI_SDIO_50MHZ 1
#endif
/* W.606: ТОЛЬКО компактная строка входящих data-кадров (rx etype/plen/fc/
   swdec) в DHCP-цикле — без tx-дампов и снимков очередей. По умолчанию
   выкл.; для диагностики #define WIFI_RX_DIAG 1 */
#ifndef WIFI_RX_DIAG
#define WIFI_RX_DIAG 0
#endif
#if (WIFI_TX_DIAG || WIFI_RX_DIAG)
#define TXDIAG(...)   uart_print(__VA_ARGS__)
#define TXDIAG_HEX(n) uart_print_hex((uint32_t)(n))
#define TXDIAG_INT(n) uart_print_int((int)(n))
#else
#define TXDIAG(...)   do {} while (0)
#define TXDIAG_HEX(n) do {} while (0)
#define TXDIAG_INT(n) do {} while (0)
#endif

/* Регистры SD/MMC хоста (offset от базы контроллера) — даташит 5.3.7. */
#define SDC_GCTL   0x00   /* global control (soft+rst, fifo_rst, dma, int_en) */
#define SDC_CKCR   0x04   /* clock control (card clock en/div) */
#define SDC_TMOR   0x08   /* timeout */
#define SDC_CTYPE  0x0C   /* bus width */
#define SDC_BLKSZ  0x10   /* block size */
#define SDC_BYCNT  0x14   /* byte count */
#define SDC_CMDR   0x18   /* command */
#define SDC_ARG    0x1C   /* command argument */
#define SDC_RESP0  0x20   /* response 0 */
#define SDC_RESP1  0x24   /* response 1 */
#define SDC_RESP2  0x28   /* response 2 */
#define SDC_RESP3  0x2C   /* response 3 */
#define SDC_IMKR   0x30   /* interrupt mask */
#define SDC_MISR   0x34   /* masked interrupt status */
#define SDC_RISR   0x38   /* raw interrupt status (W1C) */
#define SDC_STAR   0x3C   /* status */
#define SDC_FWLR   0x40   /* fifo water level */
#define SDC_FUNS   0x44   /* function select (write 0xceAA для включения функций) */
#define SDC_HWRST  0x78   /* hardware reset control */
#define SDC_DMAC   0x80   /* DMA control */
#define SDC_DLBA   0x84   /* DMA descriptor base address */
#define SDC_IDST   0x88   /* DMA interrupt status */
#define SDC_IDIE   0x8C   /* DMA interrupt enable */
#define SDC_FIFO   0x200  /* data FIFO (read/write data) */

/* Биты SDC_DMAC (IDMA control, sunxi-mmc). */
#define SDC_DMAC_SWRESET   (1u << 0)   /* IDMA software reset */
#define SDC_DMAC_FIXBURST  (1u << 1)   /* fixed burst */
#define SDC_DMAC_IDMA_ON   (1u << 7)   /* IDMA enable */
#define SDC_DMAC_REFETCH   (1u << 31)  /* refetch descriptors */

/* Индексы слов дескриптора IDMA (4 слова по 4 байта). */
#define SDC_DESC_CONFIG  0
#define SDC_DESC_BUFSIZE 1
#define SDC_DESC_BUFADDR 2
#define SDC_DESC_NEXT    3

/* Биты IDMA interrupt status/enable (IDST/IDIE). */
#define SDXC_IDMAC_TRANSMIT_INTERRUPT  (1u << 0)
#define SDXC_IDMAC_RECEIVE_INTERRUPT   (1u << 1)
#define SDXC_IDMAC_FATAL_BUS_ERROR     (1u << 2)
#define SDXC_IDMAC_DESTINATION_INVALID (1u << 4)
#define SDXC_IDMAC_CARD_ERROR_SUM      (1u << 5)

/* Биты DMA-дескриптора (config word, sunxi-mmc sunxi_idma_des.config). */
#define DESC_OWN  (1u << 31)  /* 1=IDMA owner, 0=host */
#define DESC_CES  (1u << 30)  /* card error summary */
#define DESC_CH   (1u << 4)   /* chain mode */
#define DESC_ER   (1u << 5)   /* end of ring */
#define DESC_LD   (1u << 2)   /* last descriptor */
#define DESC_FD   (1u << 3)   /* first descriptor */
#define DESC_DIC  (1u << 1)   /* disable interrupt on completion */
#define DESC_NUM(n) ((n) & 0x3)  /* descriptor number (для одиночного = 0) */

/* Структура DMA-дескриптора IDMA (4 слова) — как sunxi_idma_des. */
struct sdc_idma_desc {
    volatile uint32_t config;
    volatile uint32_t buf_size;
    volatile uint32_t buf_addr;
    volatile uint32_t next_desc;
};

/* Биты SDC_GCTL. */
#define SDC_GCTL_SOFT_RST  (1u << 0)
#define SDC_GCTL_FIFO_RST  (1u << 1)
#define SDC_GCTL_DMA_RST   (1u << 2)
#define SDC_GCTL_RESET     (SDC_GCTL_SOFT_RST | SDC_GCTL_FIFO_RST | SDC_GCTL_DMA_RST)
#define SDC_GCTL_INT_ENB   (1u << 4)
#define SDC_GCTL_CD_DBC    (1u << 8)
#define SDC_GCTL_DMA_ENB   (1u << 5)   /* DMA enable (bit 5) */

/* Биты SDC_CKCR. */
#define SDC_CKCR_CCLK_MASK   (0xFFu << 0)
#define SDC_CKCR_CCLK_ENB    (1u << 16)
#define SDC_CKCR_CCLK_CTRL   (1u << 17)
#define SDC_CKCR_MASK_DATA0  (1u << 31)

/* Биты SDC_STAR: состояние FIFO. */
#define SDC_STAR_FIFO_EMPTY  (1u << 2)
#define SDC_STAR_FIFO_FULL   (1u << 3)

/* Биты SDC_RISR: raw interrupt status. */
#define SDC_RISR_CMD_DONE    (1u << 2)   /* command complete */
#define SDC_RISR_RESP_ERR    (1u << 1)   /* response error (timeout/crc) */
#define SDC_RISR_DATA_DONE   (1u << 3)   /* data transfer complete */
#define SDC_RISR_RX_REQ      (1u << 5)   /* data receive request (read ready) */
#define SDC_RISR_TX_REQ      (1u << 4)   /* data transmit request (write ready) */
#define SDC_RISR_RESP_CRC    (1u << 6)   /* response CRC error */
#define SDC_RISR_DATA_CRC    (1u << 7)   /* data CRC error */
#define SDC_RISR_RESP_TO     (1u << 8)   /* response timeout */
#define SDC_RISR_DATA_TO     (1u << 9)   /* data timeout */
#define SDC_RISR_DATA_STAR_TO (1u << 10) /* data starvation timeout (HTO) */
#define SDC_RISR_FIFO_UNDER  (1u << 11)  /* FIFO underrun/overflow */
#define SDC_RISR_DATA_START_ERR (1u << 13) /* data start error */
#define SDC_RISR_DATA_END_BIT   (1u << 15) /* data end-bit error */
#define SDC_RISR_RAW_ISTA    (0x0000BFC2u) /* только ошибки: RESP_ERR(1) DATA_CRC(7)
                                               RESP_CRC(6) RESP_TO(8) DATA_TO(9)
                                               DATA_STAR_TO(10) FIFO_UNDER(11)
                                               CMD_BUSY(12) DATA_START_ERR(13)
                                               DATA_ENDBIT(15). БЕЗ TX_REQ/RX_REQ/DONE */

/* Биты/поля SDC_CMDR (даташит 5.3.7.7). */
#define SDC_CMDR_START       (1u << 31)  /* START - запуск передачи команды */
#define SDC_CMDR_IDX_MASK    0x3Fu
#define SDC_CMDR_RESP_RCV    (1u << 6)   /* command with response */
#define SDC_CMDR_LONG_RESP   (1u << 7)   /* 1=long (136-bit) response */
#define SDC_CMDR_CHK_CRC     (1u << 8)   /* check response CRC */
#define SDC_CMDR_DATA_TRANS  (1u << 9)   /* data transfer (read/write) */
#define SDC_CMDR_TRANS_DIR   (1u << 10)  /* 1=write, 0=read */
#define SDC_CMDR_TRANS_MODE  (1u << 11)  /* 0=block, 1=stream */
#define SDC_CMDR_STOP_CMD    (1u << 12)  /* auto stop at end of data */
#define SDC_CMDR_WAIT_PRE    (1u << 13)  /* wait previous data transfer over */
#define SDC_CMDR_STOP_ABT    (1u << 14)  /* stop/abort command (CMD12/52 abort) */
#define SDC_CMDR_SEND_INIT   (1u << 15)  /* send init sequence 80 clocks */
#define SDC_CMDR_PRG_CLK     (1u << 21)  /* change clock (PRG_CLK) */

/* Инициализировать контроллер SDC1: такт (OSC24M), пины PG0-5, сброс. */
void h3_sdc1_init(void);

/* Отправить команду без данных (с контролем CRC и ответом). */
int h3_sdc_cmd_no_data(uint32_t cmd, uint32_t arg, uint32_t resp_reg);

/* Низкоуровневый вариант: явно задать has_resp (1/0) и check_crc (1/0).
   Для SDIO-команд (CMD5/3/7/52/53) CRC не проверяется. */
int h3_sdc_cmd_no_data_flags(uint32_t cmd, uint32_t arg, uint32_t resp_reg,
                             int has_resp, int check_crc);

/* Отправить команду с данными (блочное чтение/запись). mode: 0=read, 1=write.
   data: указатель на буфер (для read), blkcnt — число блоков по blksz.
   Возвращает 0 при успехе, не-0 — ошибка. */
int h3_sdc_block_transfer(uint32_t cmd, uint32_t arg, uint32_t blksz,
                          uint32_t blkcnt, int write, void *data);

/* SDIO CMD52 — байтовый доступ к регистру функции чипа (чтение).
   Возвращает 0 при успехе и кладёт значение в *out; не-0 — ошибка. */
int h3_sdc_cmd52_read(uint32_t func, uint32_t reg, uint8_t *out);

/* SDIO CMD52 — байтовый доступ к регистру функции чипа (запись).
   Возвращает 0 при успехе; не-0 — ошибка. */
int h3_sdc_cmd52_write(uint32_t func, uint32_t reg, uint8_t val);

/* Полная инициализация SDIO-карты: CMD5 (IO_SEND_OP_COND), CMD3 (RCA),
   CMD7 (select). Должна быть выполнена до CMD52/CMD53. Возвращает 0 при
   успехе; не-0 — карта не ответила/ошибка. */
int h3_sdio_init_chip(void);

/* SDIO CMD53 — блочное чтение адресного пространства функции чипа.
   addr — стартовый адрес, blks — число блоков по 512 байт, data — буфер.
   Чтение в блок-режиме с автоинкрементом адреса. Возвращает 0 при успехе;
   не-0 — ошибка. Требует завершённой инициализации SDIO (h3_sdio_init_chip). */
int h3_sdc_cmd53_read(uint32_t func, uint32_t addr, uint32_t blks, void *data);

/* SDIO CMD53 — блочная запись в адресное пространство функции чипа.
   Используется для загрузки прошивки. Возвращает 0 при успехе; не-0 — ошибка. */
int h3_sdc_cmd53_write(uint32_t func, uint32_t addr, uint32_t blks, const void *data);

/* SDIO CMD53 — запись в byte mode (len байт). Используется для загрузки
   прошивки по 32-бит словам (как sdio_write32 драйвера Realtek). Возвращает
   0 при успехе; не-0 — ошибка. */
int h3_sdc_cmd53_write_bytes(uint32_t func, uint32_t addr, uint32_t len, const void *data);

/* SDIO CMD53 — чтение в byte mode (len байт). Аналог sdio_readN драйвера:
   читает REG_MACID 0x610 и прочие регистры >= 0x100 (CMD52 для них не работает). */
int h3_sdc_cmd53_read_bytes(uint32_t func, uint32_t addr, uint32_t len, void *data);

/* SDIO CMD53 — byte mode БЕЗ инкремента адреса (increment=0). Для регистров,
   где инкремент на границе 4 байт глючит (REG_MACID 0x610/0x614). */
int h3_sdc_cmd53_write_bytes_noinc(uint32_t func, uint32_t addr, uint32_t len, const void *data);
int h3_sdc_cmd53_read_bytes_noinc(uint32_t func, uint32_t addr, uint32_t len, void *data);

/* Установить block size функции 1 (WLAN IOREG) через CCCR 0x110/0x111.
   Обязательно перед CMD53 в block mode. Возвращает 0 при успехе. */
int h3_sdc_set_fn1_blocksize(uint16_t blksz);

/* Прочитать CIS (Card Information Structure) чипа: CCCR 0x09-0x0B даёт адрес
   Common CIS, затем по нему идём по CIS-туплам, читая каждый через CMD52
   (байтовый доступ функции 0; функция 0 не поддерживает блочный CMD53).
   Ищем TPLID_MANFID (0x20) и кладём 16-бит код производителя в *manf_id
   (0x024C = Realtek) и Card Info (PID) в *card_id (RTL8189FTV = 0xF179).
   Возвращает 0 при успехе, иначе — ошибка. Вызывается после h3_sdio_init_chip(). */
int h3_sdio_read_chip_id(uint16_t *manf_id, uint16_t *card_id);

/* Переключить шину SDIO между 1-bit (four_bit=0) и 4-bit (four_bit=1).
   Обновляет и логический режим в CCCR 0x07, и аппаратную ширину SDC_CTYPE.
   Возвращает 0 при успехе; не-0 — ошибка. */
int h3_sdio_set_bus_width(int four_bit);

/* W.625: перевод SDIO-шины на 50 МГц / 4-bit / HS (эталон Lakka). 0 при
   успехе; не-0 — ошибка. Заглушка при WIFI_SDIO_50MHZ=0. */
int h3_sdc_set_clk_50mhz(void);

/* W.655 [пинг]: сохранить IP/маску/шлюз как рабочий адрес (из OFFER или ACK). */
void h3_wifi_set_ip(const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4]);
/* r796: получить текущий рабочий IP (для TCP/FTP-сервера). 0 при наличии. */
int h3_wifi_get_ip(uint8_t ip[4]);

/* W.655 [пинг]: постоянный опрос RX после join — обрабатывает ICMP echo
   request (отвечает) и ARP (отвечает на наш IP). Возвращает число
   обработанных кадров. */
int h3_wifi_rx_poll(void);

/* Прочитать 32-бит регистр WLAN-функции (function 1) чипа через CMD52.
   reg — ММIO-адрес регистра чипа (напр. REG_SYS_CFG=0xF0). Реальный SDIO-адрес
   = (8<<13) | (reg & 0x7FFF) (WLAN IOREG, device id 8). Кладёт значение в *val.
   Возвращает 0 при успехе; не-0 — ошибка. */
int h3_sdc_read_wlan_reg32(uint32_t reg, uint32_t *val);

/* Прочитать 16-бит регистр WLAN-функции (function 1) через CMD52.
   Аналогично read_wlan_reg32, но возвращает 2 байта. */
int h3_sdc_read_wlan_reg16(uint32_t reg, uint16_t *val);

/* Запись регистра WLAN-функции (function 1): 32/16/8-бит, CMD52 для offset<0x100,
   CMD53 byte-mode для >=0x100 (как sdio_writeN драйвера). */
int h3_sdc_write_wlan_reg32(uint32_t reg, uint32_t val);
int h3_sdc_write_wlan_reg16(uint32_t reg, uint16_t val);
int h3_sdc_write_wlan_reg8(uint32_t reg, uint8_t val);

/* Чтение 8-бит регистра WLAN-функции (function 1): ВСЕГДА CMD52
   (разделения по 0x100 нет, см. h3_sdc_read_wlan_reg8). */
int h3_sdc_read_wlan_reg8(uint32_t reg, uint8_t *val);

/* Специфичная для чипа последовательность включения (power-on) RTL8189FTV:
   переводит карту из CARDEMU в ACTIVE перед загрузкой прошивки. Использует
   CMD52 к function 1. Возвращает 0 при успехе, не-0 — ошибка (см. коды). */
int h3_wifi_power_on_chip(void);

/* Загрузить прошивку чипа через SDIO (протокол MCUFWDL).
   fw — массив прошивки (RTL8189FTV NIC, ~0x4970 байт), size — размер.
   Возвращает 0 при успехе; не-0 — ошибка (см. коды в h3_sdc.c). */
int h3_wifi_download_fw(const void *fw, uint32_t size);

/* Прочитать заводской MAC из eFuse чипа (адрес 0x11A, 6 байт).
   Возвращает 0 при успехе (mac заполнен), -1 при недоступности eFuse. */
int h3_wifi_efuse_read_mac(uint8_t mac[6]);
int h3_wifi_efuse_read_byte(uint16_t addr, uint8_t *v);

/* Состояние питания чипа (диагностика): 0xA0[1:0], 1 = ACTIVE; -1 при ошибке. */
int h3_wifi_power_state(void);

/* Свободные страницы очереди HIQ в TX FIFO (SDIO-local 0x20; счётчики по чётным
   байтам HIQ/MIQ/LOQ/PUBQ, 16-бит LE). -1 при ошибке доступа.
   Перед записью кадра нужно дождаться >= 1 страницы (rtl8189fs_xmit.c:95-121). */
int h3_wifi_tx_free_pages_hiq(void);

/* Свободные страницы очереди LOW (LOQ, data/BE) в TX FIFO (SDIO-local 0x20,
   чётные байты: [4]=LOQ, 16-бит LE). -1 при ошибке доступа. */
int h3_wifi_tx_free_pages_loq(void);

/* Записать MAC в REG_MACID (0x610..0x615): сначала инкрементный путь (как
   драйвер, sdio_writel), при несовпадении чтения — откат на запись без
   инкремента. Возвращает 0 при подтверждённой записи, -1 иначе. */
int h3_wifi_set_mac(const uint8_t mac[6]);

/* RF-калибровка LCK (утечка гетеродина), path A (1T1R).
   Порт phy_LCCalibrate_8188F. Вызывать после загрузки FW, до работы радио. */
void h3_wifi_rf_lck(void);

/* RF-калибровка IQK (дисбаланс I/Q), path A (1T1R).
   Порт phy_IQCalibrate_8188F (Tx IQK + Rx IQK). Вызывать после LCK. */
void h3_wifi_rf_iqk(void);

/* RX-тест: настроить приём (RCR/RRSR/RXFLTMAP), канал 1, опросить
   счётчик принятых пакетов (RXPKT_NUM) — проверка живости RX-пути
   без завершённого IQK. */
void h3_wifi_rf_rx_test(void);

/* TX: передать probe request (механика 802.11, активный скан).
   Рецепт из драйвера: tx_desc 40 байт (MGNT_FRAMETAG) + кадр 802.11,
   запись CMD53 block в TX FIFO (WLAN_TX_HIQ_DEVICE_ID). Возвращает 0/не-0. */
int h3_wifi_tx_probe_req(uint8_t channel);

/* ===== JOINBSS (host-ассоциация): передача Auth/Assoc кадров =====
   Драйвер НЕ использует H2C 0x23 (это P2P_LPS_PARAM) — ассоциацию host
   шлёт сам: issue_auth (opensystem) -> issue_assocreq, ждёт Auth/AssocResp
   в RX. Здесь: целевая сеть для подключения + результат скана. */

#define H3_WIFI_SSID_MAX 32

/* Целевая сеть: SSID + (заполняется из RX-скана) BSSID и канал. */
void h3_wifi_set_target(const char *ssid, uint8_t ssid_len);
/* Внутренний: захват BSSID/channel целевой сети из принятого beacon
   (вызывается из парсера RX-скана при совпадении SSID). */
void h3_wifi_scan_capture_target(const uint8_t *bssid, uint8_t channel);

/* Передать Authentication (opensystem) к целевому BSSID на канале.
   Кадр: frame_ctl=0xB0, addr1/3=BSSID, addr2=наш MAC, alg=0, seq, status=0. */
int h3_wifi_tx_auth(void);

/* Передать Association Request к целевому BSSID.
   Кадр: frame_ctl=0x00, capability, listen interval, SSID IE, supported rates. */
int h3_wifi_tx_assoc_req(void);

/* Выполнить полную host-ассоциацию к целевой сети (пошагово вызывается из
   слоя; драйверный эквивалент issue_auth->issue_assocreq + ожидание ответа).
   Возвращает код состояния (0 = auth принят, далее assoc; см. реализацию). */
int h3_wifi_join_ap(void);

/* ===== Список найденных сетей (для UI «Transfer») =====
   Парсер RX-скана складывает уникальные SSID в буфер; UI читает их. */

#define H3_WIFI_AP_MAX 16

struct h3_wifi_ap {
    char ssid[H3_WIFI_SSID_MAX + 1];
    uint8_t ssid_len;
    uint8_t channel;
    uint8_t bssid[6];
    int rssi_lvl;    /* уровень сигнала (источник: RX-дескриптор, если доступен;
                        иначе 0) */
    /* WPA2: RSN IE из beacon'а (id=0x30) этой сети; rsn_len=0 если открытая.
       Нужен для AssocReq с Privacy+RSN, иначе WPA2-AP даёт rc=-6. */
    uint8_t rsn_ie[64];
    uint8_t rsn_len;
    /* WMM Parameter IE (id=221, OUI 00:50:F2, OUI type 2) из beacon'а.
       Нужен, чтобы AssocReq содержал WMM IE (иначе мы non-QoS STA и QoS Data
       слать нельзя), и чтобы data-кадры шли как QoS. wmm_len=0 — сеть без WMM. */
    uint8_t wmm_ie[32];
    uint8_t wmm_len;
};

/* Сколько сетей найдено в последнем скане. */
int h3_wifi_scan_ap_count(void);
/* Копировать найденную сеть i в *ap. Возвращает 0 при успехе. */
int h3_wifi_scan_ap_get(int i, struct h3_wifi_ap *ap);
/* Очистить список (вызывается перед началом скана). */
void h3_wifi_scan_ap_reset(void);
/* Внутренний: добавить сеть в список (вызывается парсером RX-скана). */
void h3_wifi_scan_ap_add(struct h3_wifi_ap *ap);

/* Установить RSN IE (WPA2) целевой сети из структуры AP (для UI-пути,
   где RSN не заполняется полным beacon-парсером). */
void h3_wifi_set_target_rsn(const struct h3_wifi_ap *ap);

/* Установить WMM IE целевой сети из структуры AP. Нужен для AssocReq
   (WMM IE) и для решения «QoS Data или обычный Data». */
void h3_wifi_set_target_wmm(const struct h3_wifi_ap *ap);

/* W.412: задать PSK passphrase (для WPA2 4-way). PMK будет вычислен из него
   при построении handshake. pass_len>0 — сеть WPA2. */
void h3_wifi_set_passphrase(const char *pass, uint8_t pass_len);

/* Передать 802.11 data-фрейм (ToDS) с payload (LLC/SNAP+ethertype+data)
    в LOW-очередь. dst_mac — L2-адрес получателя (addr3): broadcast для
    DHCP DISCOVER/ARP request, MAC пира для ARP reply/ICMP.
    Используется для DHCP/ARP/ICMP/EAPOL. 0 при успехе. */
int h3_wifi_tx_data(uint16_t ethertype, const uint8_t *dst_mac,
                    const uint8_t *data, uint16_t len);

/* Прочитать наш MAC (REG_MACID 0x610-0x615). Публичный для сетевого слоя. */
void h3_wifi_get_mac(uint8_t m[6]);

/* Регистры чипа (hal8188freg.h). */
#define REG_MCUFWDL_OFF      0x0080
#define FW_8188F_START       0x1000
#define MCUFWDL_RDY          (1u << 1)
#define FWDL_ChkSum_rpt      (1u << 2)
#define WINTINI_RDY          (1u << 6)
#define RAM_DL_SEL           (1u << 7)

/* Диагностика последней блочной передачи (заполняются при ошибке CMD53). */
extern uint32_t sdc_diag_risr;
extern uint32_t sdc_diag_sta;
extern uint32_t sdc_diag_cmdr;
extern uint32_t sdc_diag_cmd;
extern uint32_t sdc_diag_arg;
extern int      sdc_diag_fail_blk;   /* номер блока, на котором упала загрузка FW */

/* Диагностика power-on seq (заполняется при ошибке поллинга). */
extern uint32_t sdc_diag_pseq_step;
extern uint32_t sdc_diag_pseq_base;
extern uint32_t sdc_diag_pseq_off;
extern uint32_t sdc_diag_pseq_val;

/* Значение REG_MCUFWDL при падении FreeToGo (-33). */
extern uint32_t sdc_diag_fw_mcufwdl;

#endif /* H3_SDC_H */