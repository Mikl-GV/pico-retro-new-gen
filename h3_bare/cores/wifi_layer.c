#include "wifi_layer.h"
#include "uart.h"
#include "h3_hs_timer.h"
#include "h3_sdc.h"
#include "wifi_net.h"

/* r795: вставка в проект — у нас micros() = h3_hs_timer_lo_us(). */
#define micros() h3_hs_timer_lo_us()

/* Отдельный слой WiFi (SDIO RTL8189FTV). Вызывается из пункта меню
   "Transfer" — инициализация/прошивка/калибровка чипа выполняется только
   при входе в экран, а не при каждом старте прошивки. */

extern const unsigned char fw_blob_start[];
extern const unsigned char fw_blob_end[];

int wifi_active = 0;

static int wifi_started = 0;

int wifi_layer_started(void)
{
    return wifi_started;
}

/* Печать байта как %02X (две hex-цифры). */
static void print_u8_hex(uint8_t v)
{
    static const char hexd[] = "0123456789ABCDEF";
    uart_putc(hexd[(v >> 4) & 0xF]);
    uart_putc(hexd[v & 0xF]);
}

int wifi_layer_start(void)
{
    if (wifi_started)
        return 0;

    uart_print("wifi init...\r\n");

    /* Включить чип (WIFI_EN=PL7), инициализировать SDC1 и SDIO-карту. */
    WLOG("  pl_before=0x"); WLOG_HEX(*(volatile uint32_t *)0x01F02C10);
    WLOG("\r\n");
    h3_sdc1_init();
    WLOG("  pl_after=0x"); WLOG_HEX(*(volatile uint32_t *)0x01F02C10);
    WLOG("\r\n");

    /* Диагностика после инициализации контроллера (расширенный лог). */
    WLOG("  gctl=0x"); WLOG_HEX(*(volatile uint32_t *)0x01C10000);
    WLOG(" ckcr=0x");  WLOG_HEX(*(volatile uint32_t *)0x01C10004);
    WLOG(" sclk=0x");  WLOG_HEX(*(volatile uint32_t *)0x01C2008C);
    WLOG(" star=0x");  WLOG_HEX(*(volatile uint32_t *)0x01C1003C);
    WLOG(" rst0=0x");  WLOG_HEX(*(volatile uint32_t *)0x01C202C0);
    WLOG(" pg_cfg0=0x");WLOG_HEX(*(volatile uint32_t *)0x01C208D8);
    WLOG(" pg_cfg1=0x");WLOG_HEX(*(volatile uint32_t *)0x01C208DC);
    WLOG(" pl_cfg=0x");WLOG_HEX(*(volatile uint32_t *)0x01F02C00);
    WLOG(" pl_dat=0x");WLOG_HEX(*(volatile uint32_t *)0x01F02C10);
    WLOG("\r\n");

    {
        int rc = h3_sdio_init_chip();
        if (rc != 0) {
            uart_print("  sdio_init FAIL rc=");
            uart_print_int(rc);
            uart_print("\r\n");
            /* W.739 [аудит]: при сбое инициализации контроллера слой НЕ
               помечаем запущенным. Раньше wifi_started ставился безусловно
               ниже — UI «Transfer» показывал «running» и вызывал
               h3_wifi_tx_probe_req/join на неинициализированный чип.
               Выход сразу, чтобы wifi_layer_started()==0. */
            uart_print("wifi FAIL\r\n");
            return rc;
        }
        if (rc == 0) {
            wifi_active = 1;
            uint8_t ccid0 = 0, ccid1 = 0;
            h3_sdc_cmd52_read(0, 0x00, &ccid0);   /* CCCR/SDIO revision */
            h3_sdc_cmd52_read(0, 0x01, &ccid1);   /* SDIO function info */
            WLOG("  ccid0=0x");
            WLOG_HEX(ccid0);
            WLOG(" ccid1=0x");
            WLOG_HEX(ccid1);
            WLOG("\r\n");

            /* ID чипа из CIS: manf (0x024C Realtek) + card PID (0xF179 8189F). */
            {
                uint16_t manf = 0, card = 0;
                int id_rc = h3_sdio_read_chip_id(&manf, &card);
                uart_print("  chip_id rc=");
                uart_print_int(id_rc);
                if (id_rc == 0) {
                    uart_print(" manf=0x");
                    uart_print_hex(manf);
                    uart_print(" card=0x");
                    uart_print_hex(card);
                }
                uart_print("\r\n");
            }

            /* Power-on чипа: CARDEMU -> ACTIVE. */
            {
                int pon = h3_wifi_power_on_chip();
                if (pon != 0) {
                    uart_print("  pwon FAIL rc=");
                    uart_print_int(pon);
                    uart_print(" step=");
                    uart_print_int((int)sdc_diag_pseq_step);
                    uart_print(" base=");
                    uart_print_int((int)sdc_diag_pseq_base);
                    uart_print(" off=0x");
                    uart_print_hex((uint32_t)sdc_diag_pseq_off);
                    uart_print(" val=0x");
                    uart_print_hex((uint32_t)sdc_diag_pseq_val);
                    uart_print("\r\n");
                }
            }

            /* Этап 3: включить I/O-функцию 1 (CCCR 0x02, бит IOEx=0x02). */
            {
                uint8_t ioen;
                h3_sdc_cmd52_read(0, 0x02, &ioen);
                h3_sdc_cmd52_write(0, 0x02, (uint8_t)(ioen | 0x02));
                h3_sdc_cmd52_read(0, 0x02, &ioen);
                WLOG("  fn1 ioen=0x");
                WLOG_HEX(ioen);
                WLOG("\r\n");
            }

            /* Этап 4: загрузка прошивки чипа (NIC firmware). */
            {
                uint32_t fwlen = (uint32_t)(fw_blob_end - fw_blob_start);
                int frc = h3_wifi_download_fw(fw_blob_start, fwlen);
                uart_print("  fwdl rc=");
                uart_print_int(frc);
                uart_print(" len=0x");
                uart_print_hex(fwlen);
                if (frc != 0) {
                    uart_print(" blk=");
                    uart_print_int(sdc_diag_fail_blk);
                    uart_print(" cmd=");
                    uart_print_hex(sdc_diag_cmd);
                    uart_print(" arg=0x");
                    uart_print_hex(sdc_diag_arg);
                    uart_print(" cmdr=0x");
                    uart_print_hex(sdc_diag_cmdr);
                    uart_print(" risr=");
                    uart_print_hex(sdc_diag_risr);
                    uart_print(" sta=");
                    uart_print_hex(sdc_diag_sta);
                    if (frc == -33) {
                        uart_print(" mcufwdl=0x");
                        uart_print_hex((uint32_t)sdc_diag_fw_mcufwdl);
                    }
                } else {
                    /* Этап 5: читаем eFuse (заводской MAC); если пуст — задаём
                       программно (как драйвер 8189fs) и пишем в REG_MACID. */
                    uint8_t mac[6];
                    uint32_t seed = micros();
                    mac[0] = 0x02; mac[1] = 0x11; mac[2] = 0x22;
                    mac[3] = (uint8_t)(seed >> 16);
                    mac[4] = (uint8_t)(seed >> 8);
                    mac[5] = (uint8_t)(seed);
                    int erc = h3_wifi_efuse_read_mac(mac);
                    uart_print("  efuse rc=");
                    uart_print_int(erc);
                    if (erc == 0) {
                        int valid = 1;
                        for (int k = 0; k < 6; k++)
                            if (mac[k] == 0xFF || mac[k] == 0x00) { valid = 0; break; }
                        if (!valid) {
                            /* eFuse пуст/биттая: генерируем ЛОКАЛЬНО-АДМИНИСТРИРУЕМЫЙ
                               MAC (02:...), как это делает эталонная Lakka в драйвере
                               8189fs (02:81:38:...). Первый октет 0x02 = unicast +
                               locally administered — валиден для DHCP. */
                            /* W.712 [проверка MAC-гипотезы — ОТКЛОНЕНА в W.713]: вендорский MAC
                               00:E0:4C не изменил поведение (OFFER так же
                               приходил только на A05s, ACK не появился).
                               Дело не в MAC — вернулись к своему
                               локально-администрируемому 02:11:22:xx. */
                            uint32_t t = micros();
                            mac[0] = 0x02; mac[1] = 0x11; mac[2] = 0x22;
                            mac[3] = (uint8_t)(t >> 16);
                            mac[4] = (uint8_t)(t >> 8);
                            mac[5] = (uint8_t)(t);
                        }
                        /* W.672: УБРАНО принудительное mac[0]=0x00 (эксперимент W.659).
                           Он давал MAC 00:00:00:xx — невалидный OUI, DHCP-сервер
                           (A05s/dnsmasq) молча дропает REQUEST от клиента с нулевым
                           OUI. Эталон живой Lakka работает с 02:81:38:27:88:cd. */
                    }
                    uart_print("  efuse mac=");
                    for (int k = 0; k < 6; k++) {
                        print_u8_hex(mac[k]);
                        if (k < 5) uart_print(":");
                    }
                    uart_print("\r\n");

                    /* Запись MAC в REG_MACID (0x610-0x615) — результат кратко. */
                    {
                        int wk = h3_wifi_set_mac(mac);
                        uart_print("  mac_set=");
                        uart_print(wk == 0 ? "OK" : "FAIL");
                        uart_print("\r\n");
                    }
                }

                /* RF-калибровка LCK. */
                h3_wifi_rf_lck();

                /* На старте — скан-обзор БЕЗ автоподключения (W.514). */
                uart_print("join: boot scan (no autoconnect)\r\n");
                {
                    h3_wifi_rf_rx_test();
                    uart_print("join: ap_list=");
                    uart_print_int(h3_wifi_scan_ap_count());
                    uart_print("\r\n");
                }
            }
        }
    }

    uart_print("wifi ok\r\n");
    wifi_started = 1;
    return 0;
}

/* Подключение к ВЫБРАННОЙ сети из списка (UI «Transfer»): по индексу
   берём её BSSID/channel из буфера scan_aps[] (заполнен парсером скана
   в h3_wifi_rf_rx_test) и захватываем как цель JOINBSS-движка
   (h3_wifi_scan_capture_target -> h3_wifi_join_ap БЕЗ повторного скана).
   Возвращает: 0 = ассоциация подтверждена; -1 = плохой индекс; иначе
   код от h3_wifi_join_ap (например -4 — AuthResp не пришла). */
/* W.612→W.690 [корень]: кулдаун 3 с был ИСКУССТВЕННЫМ ТОРМОЗОМ переходов
   между сетями (живая Lakka переключается мгновенно). Причина W.606
   (flood rc=-6/-4/-4) была не в скорости join, а в грязном состоянии
   (seq=0, MSR/CAM не сброшены — исправлено W.687/W.689). После фиксов
   кулдаун не нужен; оставляем 200 мс — только против дребезга двойного
   клика, не против быстрых переходов. */
static int join_in_progress = 0;
static uint32_t last_join_ms = 0;
/* W.707: флаг «IP уже принят» — сбрасывается при каждом connect (см. ниже). */
static int offer_fallback_done = 0;
#define JOIN_COOLDOWN_MS 200

int wifi_layer_connect(int index)
{
    struct h3_wifi_ap ap;
    int rc;
    uint32_t now = micros() / 1000;

    if (!wifi_started)
        return -2;                       /* слой не инициализирован */
    if (index < 0 || h3_wifi_scan_ap_get(index, &ap) != 0)
        return -1;                       /* нет такой сети */

    /* W.612: занят — не начинать повторный join (защита от вложенного
       вызова / быстрых кликов). */
    if (join_in_progress) { uart_print("connect: busy\r\n"); return -10; }
    /* W.612: кулдаун — минимум 200 мс между попытками (JOIN_COOLDOWN_MS).
       Защита только от дребезга кликов; живая Lakka переключается мгновенно
       (см. пояснение W.612→W.690 выше). */
    if (last_join_ms && (now - last_join_ms) < JOIN_COOLDOWN_MS) {
        uart_print("connect: cooldown (");
        uart_print_int((int)(now - last_join_ms));
        uart_print("ms)\r\n"); return -11;
    }
    join_in_progress = 1;
    offer_fallback_done = 0;   /* W.707: новый join — IP новой сети принимаем заново */

    uart_print("join: sel=");
    uart_print_int(index);
    uart_print(" bssid=");
    for (int k = 0; k < 6; k++) { uart_print_hex((uint32_t)ap.bssid[k]); if (k < 5) uart_print(":"); }
    uart_print(" ch=");
    uart_print_int((int)ap.channel);
    uart_print(" ssid=");
    for (int k = 0; k < ap.ssid_len; k++) uart_putc(ap.ssid[k]);
    uart_print("\r\n");

    /* Захватить выбранную сеть как цель ассоциации — полностью (SSID, BSSID,
       канал). Без установки SSID join_target остаётся от старого таргета (A05s
       из kmain), и AssocReq к другой сети несёт чужой SSID — AP молча дропает
       (rc=-6, W.382). */
    h3_wifi_set_target(ap.ssid, ap.ssid_len);
    h3_wifi_scan_capture_target(ap.bssid, ap.channel);
    /* WPA2: передать RSN IE выбранной сети в цель — без него AssocReq к
       WPA2-AP уходит без Privacy+RSN (cap=1) и AP не ассоциирует (rc=-6). */
    h3_wifi_set_target_rsn(&ap);
    h3_wifi_set_target_wmm(&ap);
    /* W.412: для WPA2-сети (rsn_len>0) зафиксировать PSK passphrase по SSID.
       PMK = PBKDF2-HMAC-SHA1(pass, ssid, 4096, 32) предвычисляется сразу в
       h3_wifi_set_passphrase (кэш, вне hot-path — чтобы не платить 5.5с на
       первый msg1). Ровно две известные WPA2-сети: A05s (8645449Qq),
       MTS_GPON_9290e0 (B236f8g4). Остальные — без пароля.
       Открытая A05s (rsn_len==0) идёт в else-ветку ниже и тоже без пароля —
       решение принимается по RSN IE из скана, а не по названию. */
    if (ap.rsn_len > 0) {
        if (ap.ssid_len >= 4 &&
            ap.ssid[0]=='A' && ap.ssid[1]=='0' && ap.ssid[2]=='5' && ap.ssid[3]=='s') {
            h3_wifi_set_passphrase("8645449Qq", 9);
            uart_print("  passphrase: A05s\r\n");
        } else if (ap.ssid_len >= 13 &&
                   ap.ssid[0]=='M' && ap.ssid[1]=='T' && ap.ssid[2]=='S' &&
                   ap.ssid[3]=='_' && ap.ssid[4]=='G' && ap.ssid[5]=='P' &&
                   ap.ssid[6]=='O' && ap.ssid[7]=='N' && ap.ssid[8]=='_' &&
                   ap.ssid[9]=='9' && ap.ssid[10]=='2' && ap.ssid[11]=='9' &&
                   ap.ssid[12]=='0' && ap.ssid[13]=='e' && ap.ssid[14]=='0') {
            /* W.485: B236f8g4 известен ТОЛЬКО для MTS_GPON_9290e0.
               Для d23898/b4d378 пароль другой — 4-way даст MIC-fail (AP
               начнёт новый handshake с новым ANonce), это НЕ баг TX.
               Так как автопаролей ровно две, MTS_* не угадываем. */
            h3_wifi_set_passphrase("B236f8g4", 8);
            uart_print("  passphrase: MTS_GPON_9290e0\r\n");
        } else {
            /* Незнакомый SSID: не угадывать пароль (ложный handshake). */
            h3_wifi_set_passphrase("", 0);
            uart_print("  passphrase: unknown SSID, none\r\n");
        }
        uart_print("  wpa2 passphrase set (rsn_len=");
        uart_print_int((int)ap.rsn_len);
        /* W.515: RSN capabilities — последние 2 байта RSN IE.
           bit7=MFPC (PMF capable), bit6=MFPR (PMF required). Если MFPR=1,
           AP требует 802.11w и unprotected data (DHCP) будет дропаться. */
        if (ap.rsn_len >= 4) {
            uint8_t caps0 = ap.rsn_ie[ap.rsn_len - 2];
            uart_print(" pmf_cap=");
            uart_print_int((int)((caps0 >> 7) & 1));
            uart_print(" pmf_req=");
            uart_print_int((int)((caps0 >> 6) & 1));
        }
        uart_print(")\r\n");
    } else {
        /* Открытая сеть: сбросить passphrase предыдущей цели (иначе в
           join_ap останется pass_len>0 и DHCP в WPA2-ветке не отправится). */
        h3_wifi_set_passphrase("", 0);
        uart_print("  open network (no passphrase)\r\n");
    }

    rc = h3_wifi_join_ap();
    join_in_progress = 0;
    last_join_ms = micros() / 1000;   /* кулдаун стартует всегда (и при успехе) */
    uart_print("join: rc=");
    uart_print_int(rc);
    uart_print("\r\n");
    return rc;
}

int wifi_layer_rescan(void)
{
    if (!wifi_started)
        return -2;                       /* слой не инициализирован */

    uart_print("wifi: rescan...\r\n");
    /* Сбросить список найденных сетей и захват цели (join_target.bssid_captured),
       затем заново прогнать скан 13 каналов — иначе повторный скан не обновит
       BSSID/SSID захваченной сети и автоjoin возьмёт старую цель. */
    h3_wifi_scan_ap_reset();
    h3_wifi_set_target("", 0);      /* сброс цели (bssid_captured=0) */
    h3_wifi_rf_rx_test();
    uart_print("wifi: rescan done (");
    uart_print_int(h3_wifi_scan_ap_count());
    uart_print(" ap)\r\n");
    return 0;
}

/* W.655 [пинг]: постоянный опрос сетевого RX в главном цикле. Если адрес
   получен по ACK (или по OFFER-фолбэку) — поднимает L3 и обрабатывает
   входящие кадры (ARP/ICMP) через h3_wifi_rx_poll(). */
int wifi_layer_poll(void)
{
    if (!wifi_started) return 0;

    /* W.743: фоновые ретраи DHCP. Пока ACK не пришёл, REQUEST ретраится
       с backoff (2/4/8/16 с). Если ACK нет и после серии попыток — сервер,
       вероятно, потерял состояние (или наш REQUEST не дошёл): начинаем
       НОВЫЙ цикл DHCP (net_dhcp_reset + DISCOVER с новым xid), а не долбим
       REQUEST вечно. Иначе в логе бесконечные строки REQUEST каждые ~12 с. */
    if (net_dhcp_offer_pending()) {
        static uint32_t last_req_ms = 0;
        static int req_retries = 0;
        uint8_t ip[4], mk[4], gw[4];
        int ack = 0;
        int have = net_dhcp_get_ip(ip, mk, gw, &ack);
        if (have && !ack) {
            uint32_t now = micros() / 1000;
            uint32_t gap = 2000u << (req_retries < 3 ? req_retries : 3);
            if (last_req_ms == 0 || (uint32_t)(now - last_req_ms) >= gap) {
                if (req_retries < 4) {
                    net_dhcp_request();
                    req_retries++;
                } else {
                    /* Серия REQUEST без ACK исчерпана — перезапуск DHCP. */
                    uart_print("  poll: DHCP restart (no ACK, rediscover)\r\n");
                    net_dhcp_reset();          /* новый xid + чистое состояние */
                    net_dhcp_discover(0);      /* st не используется */
                    req_retries = 0;
                }
                last_req_ms = now;
            }
        } else {
            /* ACK получен (или адреса нет) — сбросить счётчик ретраев. */
            last_req_ms = 0;
            req_retries = 0;
        }
    }

    {
        uint8_t ip[4], mk[4], gw[4];
        int ack = 0;
        if (net_dhcp_get_ip(ip, mk, gw, &ack)) {
            if (ack) {
                h3_wifi_set_ip(ip, mk, gw);          /* финальный ACK-адрес */
                offer_fallback_done = 1;
            } else if (!offer_fallback_done) {
                h3_wifi_set_ip(ip, mk, gw);          /* OFFER fallback, один раз */
                offer_fallback_done = 1;
                uart_print("  poll: IP accepted from OFFER (no ACK)\r\n");
            }
            /* r796: при получении адреса — включить FTP-сервер (один раз).
             * Даёт доступ к SD/USB-флешке через Проводник: ftp://<IP> */
            {
                static int ftp_started = 0;
                extern int ftp_server_start(void);
                extern void ftp_server_poll(void);
                if (!ftp_started) {
                    if (ftp_server_start() == 0)
                        uart_print("  poll: FTP server on :21\r\n");
                    ftp_started = 1;
                }
                ftp_server_poll();   /* приём FTP-команд/данных */
            }
        } else {
            return 0;                                /* адреса ещё нет */
        }
    }
    return h3_wifi_rx_poll();
}