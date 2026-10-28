// net_tcp.c — минимальный TCP-стек с двумя слотами (control + data) для FTP.
// r796: поверх WiFi (h3_wifi_tx_data / IPv4 из wifi_net).
#include <string.h>
#include "net_tcp.h"
#include "h3_sdc.h"
#include "uart.h"

/* r835: отладочная трассировка SYN (см. net_tcp_handle). По умолчанию
   выключена — при автопоиске клиента это десятки строк на адрес. */
#define TCP_DBG 0

struct tcp_slot {
    int      used;          // слот занят (слушатель)
    int      active;        // соединение установлено (SYN принят)
    uint16_t listen_port;   // порт слушателя (0 если слот data-инициализирован)
    uint16_t local_port;    // активный локальный порт
    uint16_t peer_port;
    uint8_t  peer_ip[4];
    uint8_t  peer_mac[6];
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint32_t iss;           // r818: initial send seq — для повтора SYN-ACK
    net_tcp_app_rx_t app_rx;
    void*    ctx;
    /* r835: буфер переупорядочивания. Потеря одного кадра WiFi даёт сегмент
       с seq > rcv_nxt; раньше он отбрасывался, поток вставал до таймаута
       клиента (обрыв STOR на 98–101 КБ). Теперь такие сегменты копятся и
       отдаются приложению по порядку, когда придёт пропущенный. */
    uint32_t ooo_seq[NET_TCP_OOO];
    uint16_t ooo_len[NET_TCP_OOO];
    uint8_t  ooo_data[NET_TCP_OOO][NET_TCP_MSS];
};

static struct tcp_slot g_slots[NET_TCP_SLOTS];

static void ipv4_csum(uint8_t* hdr) {
    uint32_t s = 0;
    for (int i = 0; i < 20; i += 2) s += ((uint16_t)hdr[i] << 8) | hdr[i+1];
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    uint16_t c = (uint16_t)~s;
    hdr[10] = (uint8_t)(c >> 8); hdr[11] = (uint8_t)c;
}

static void tcp_csum(uint8_t* pkt, uint16_t tcp_len) {
    // r810: контрольная сумма TCP по псевдозаголовку: src IP(12-15) + dst
    // IP(16-19) + 0x00/0x06 + tcp_len + TCP-сегмент (16-бит словами, BE).
    // Раньше dst-IP учитывался ДВАЖДЫ (второй цикл по 16-19) — все TCP-кадры
    // (SYN-ACK/220/ACK/данные) уходили с НЕВЕРНОЙ csum, Windows молча их
    // дропал, FTP-подключение не устанавливалось (ни проводник, ни клиент).
    uint32_t acc = 0;
    for (int i = 12; i < 20; i += 2) acc += ((uint16_t)pkt[i] << 8) | pkt[i+1];
    /* r820: псевдозаголовок TCP — слово (0x00, 0x06) = 0x0006.
       Было `6u << 8` (0x0600): ошибка ровно 0x05FA в КАЖДОМ TCP-сегменте
       (SYN-ACK/220/ACK/данные) -> Windows молча дропал все пакеты,
       FTP-соединение не устанавливалось. Проверка по захвату: в пакете
       csum=0x8600, правильная 0x8BFA, разница 0x05FA. */
    acc += 0x0006u;
    acc += tcp_len;
    for (int i = 20; i + 1 < 20 + (int)tcp_len; i += 2)
        acc += ((uint16_t)pkt[i] << 8) | pkt[i+1];
    if (tcp_len & 1) acc += (uint16_t)pkt[20 + tcp_len - 1] << 8;
    while (acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);
    uint16_t c = (uint16_t)~acc;
    pkt[20 + 16] = (uint8_t)(c >> 8); pkt[20 + 17] = (uint8_t)c;
}

// Отправка сегмента в слоте.
static void slot_send_raw(struct tcp_slot* s, uint8_t flags, uint32_t seq,
                          uint32_t ack, const uint8_t* data, uint16_t len)
{
    if (!s->active && !(flags & NET_TCP_SYN)) return;
    if (len > NET_TCP_MSS) len = NET_TCP_MSS;   // защита: не больше MSS (r808)
    // r818: 48 (20 IP + 20 TCP + 4 опции MSS + запас) + MSS. Раньше 40+MSS
    // (1500): при SYN с MSS-опцией и полном MSS-блоке данных писали до
    // pkt[1503] — переполнение стека на 4 байта.
    uint8_t pkt[48 + NET_TCP_MSS];
    uint8_t my_mac[6], my_ip[4];
    h3_wifi_get_mac(my_mac);
    if (h3_wifi_get_ip(my_ip) != 0) return;

    // r815: SYN-пакеты (SYN и SYN-ACK) с MSS-опцией (kind=2,len=4,MSS=1460).
    // Windows без MSS в SYN-ACK отбрасывает соединение.
    // ВАЖНО: data offset = (20+mss_len)/4 = 6 (0x60) для 24-байт заголовка.
    // r816: раньше писали 0x54 — старший ниббл 5 (20 байт заголовка!)
    // ломал парсинг: опции игнорировались, младший ниббл 4 читался как
    // Reserved-флаг (tshark: Flags 0x412 SYN+ACK+Reserved) → Windows
    // обрывал соединение.
    int mss_len = (flags & NET_TCP_SYN) ? 4 : 0;   // байтов в опции MSS
    uint16_t tot = 20 + 20 + mss_len + len;
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x45; pkt[1] = 0;
    pkt[2] = (uint8_t)(tot >> 8); pkt[3] = (uint8_t)tot;
    pkt[8] = 64; pkt[9] = 6;
    // r813: уникальный IP-ID + DF=1 (без этого Windows дропал пакеты).
    {
        static uint16_t ip_id = 0x1234;
        ip_id++;
        pkt[4] = (uint8_t)(ip_id >> 8); pkt[5] = (uint8_t)ip_id;
        pkt[6] = 0x40;   /* DF=1 */
    }
    memcpy(&pkt[12], my_ip, 4);
    memcpy(&pkt[16], s->peer_ip, 4);
    ipv4_csum(pkt);

    pkt[20] = (uint8_t)(s->local_port >> 8); pkt[21] = (uint8_t)s->local_port;
    pkt[22] = (uint8_t)(s->peer_port >> 8);  pkt[23] = (uint8_t)s->peer_port;
    pkt[24] = (uint8_t)(seq >> 24); pkt[25] = (uint8_t)(seq >> 16);
    pkt[26] = (uint8_t)(seq >> 8);  pkt[27] = (uint8_t)seq;
    pkt[28] = (uint8_t)(ack >> 24); pkt[29] = (uint8_t)(ack >> 16);
    pkt[30] = (uint8_t)(ack >> 8);  pkt[31] = (uint8_t)ack;
    pkt[32] = (mss_len ? 0x60 : 0x50);  /* data offset: 24 или 20 байт */
    pkt[33] = flags;
    pkt[34] = (uint8_t)(NET_TCP_WINDOW >> 8); pkt[35] = (uint8_t)NET_TCP_WINDOW;
    if (mss_len) {
        // r817: опции TCP — С БАЙТА 40 (20 фикс. TCP + 20 IP = 40). Раньше
        // писали в 36-39 — это ПОЛЯ CHECKSUM/URGENT, а не опции! Wireshark
        // видел 4xEOL вместо MSS, control-сумма была неверной → Windows
        // отбрасывал SYN-ACK.
        pkt[40] = 2;                    /* kind = MSS */
        pkt[41] = 4;                    /* len = 4 */
        pkt[42] = (uint8_t)(NET_TCP_MSS >> 8); pkt[43] = (uint8_t)NET_TCP_MSS;
    }
    if (data && len) memcpy(&pkt[40 + mss_len], data, len);
    tcp_csum(pkt, (uint16_t)(20 + mss_len + len));
    h3_wifi_tx_data(0x0800, s->peer_mac, pkt, tot);
}

// r808: сегментация по MSS. Раньше net_tcp_send клал ВСЕ данные одним
// сегментом — при len > NET_TCP_MSS (LIST 4KB, ответы > 1460) писал ЗА
// буфер pkt[40+MSS] (stack overflow) и ломал память всего стека.
// Теперь разбиваем на куски ≤ MSS, каждый с корректным seq.
void net_tcp_send(int slot, uint8_t flags, const uint8_t* data, uint16_t len) {
    if (slot < 0 || slot >= NET_TCP_SLOTS) return;
    struct tcp_slot* s = &g_slots[slot];
    if (!s->active) return;
    uint16_t sent = 0;
    while (sent < len) {
        uint16_t n = len - sent;
        if (n > NET_TCP_MSS) n = NET_TCP_MSS;
        slot_send_raw(s, flags | NET_TCP_ACK, s->snd_nxt, s->rcv_nxt,
                      data ? data + sent : 0, n);
        s->snd_nxt += n;
        sent += n;
    }
}

int net_tcp_listen(uint16_t port, net_tcp_app_rx_t app_rx, void* ctx) {
    for (int i = 0; i < NET_TCP_SLOTS; i++) {
        if (!g_slots[i].used) {
            struct tcp_slot* s = &g_slots[i];
            memset(s, 0, sizeof(*s));
            s->used = 1;
            s->listen_port = port;
            s->app_rx = app_rx;
            s->ctx = ctx;
            return i;
        }
    }
    return -1;
}

void net_tcp_close(int slot) {
    // r823: закрытие ДОЛЖНО слать FIN. Раньше слот просто затирался — клиент
    // (ftp: data-канал после LIST/RETR) не получал EOF и висел до таймаута:
    // 226 уходил, а data-соединение оставалось «открытым» с нашей стороны.
    if (slot < 0 || slot >= NET_TCP_SLOTS) return;
    struct tcp_slot* s = &g_slots[slot];
    if (s->active) {
        slot_send_raw(s, NET_TCP_FIN | NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
        s->snd_nxt += 1;
    }
    memset(s, 0, sizeof(*s));
}

void net_tcp_abort(int slot) {
    if (slot < 0 || slot >= NET_TCP_SLOTS) return;
    struct tcp_slot* s = &g_slots[slot];
    if (s->active)
        slot_send_raw(s, NET_TCP_RST | NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
    s->active = 0;
    /* слот остаётся used (слушатель) */
}

int net_tcp_connected(int slot) {
    if (slot < 0 || slot >= NET_TCP_SLOTS) return 0;
    return g_slots[slot].active;
}

// Найти слот: активный на порту dp, либо слушатель dp.
static struct tcp_slot* slot_by_dport(uint16_t dp) {
    for (int i = 0; i < NET_TCP_SLOTS; i++) {
        struct tcp_slot* s = &g_slots[i];
        if (s->used && s->active && s->local_port == dp) return s;
    }
    for (int i = 0; i < NET_TCP_SLOTS; i++) {
        struct tcp_slot* s = &g_slots[i];
        if (s->used && !s->active && s->listen_port == dp) return s;
    }
    return 0;
}

int net_tcp_handle(const uint8_t* pkt, uint16_t len, const uint8_t* src_mac,
                   const uint8_t* src_ip, const uint8_t my_ip[4])
{
    if (len < 20) return -1;
    uint16_t sp = (uint16_t)((pkt[0] << 8) | pkt[1]);
    uint16_t dp = (uint16_t)((pkt[2] << 8) | pkt[3]);
    uint32_t seq = ((uint32_t)pkt[4] << 24) | ((uint32_t)pkt[5] << 16)
                 | ((uint32_t)pkt[6] << 8) | pkt[7];
    uint32_t ack = ((uint32_t)pkt[8] << 24) | ((uint32_t)pkt[9] << 16)
                 | ((uint32_t)pkt[10] << 8) | pkt[11];
    uint8_t off = (uint8_t)((pkt[12] >> 4) * 4);
    uint8_t flags = pkt[13];
    if (off < 20 || off > (uint16_t)len) return -1;

    struct tcp_slot* s = slot_by_dport(dp);
    if (!s) return 0;   // не наш порт

    // r835: трассировка SYN — под флагом (автопоиск клиента даёт десятки
    // строк на каждый адрес подсети). Включить: TCP_DBG 1.
#if TCP_DBG
    if (flags & NET_TCP_SYN) {
        extern int uart0_printf(const char* fmt, ...);
        uart0_printf("  net: SYN sp=%u dp=%u seq=%u is=%d rcv=%u snd=%u\r\n",
                     sp, dp, seq, s->active, s->rcv_nxt, s->snd_nxt);
    }
#endif

    memcpy(s->peer_ip, src_ip, 4);
    memcpy(s->peer_mac, src_mac, 6);

    if (flags & NET_TCP_SYN) {
        if (s->active) {
            // Тот же сокет (retransmit SYN) — повторный SYN-ACK с ИСХОДНЫМ
            // seq (iss). Раньше слали snd_nxt-1, а после отправки 220
            // snd_nxt уже сдвинут на его длину → seq неверный, клиент
            // отвергал SYN-ACK.
            if (s->peer_port == sp) {
                slot_send_raw(s, NET_TCP_SYN | NET_TCP_ACK, s->iss,
                              s->rcv_nxt, 0, 0);
                return 0;
            }
            // ДРУГОЙ клиент на тот же порт (наш слот "залип" active после
            // прошлого клиента): переустанавливаем соединение со СВЕЖИМ
            // seq (полное переподключение).
        }
        s->active = 1;
        s->local_port = dp;
        s->peer_port = sp;
        s->rcv_nxt = seq + 1;
        s->snd_nxt = 0x40000000u;
        s->iss     = 0x40000000u;
        for (int i = 0; i < NET_TCP_OOO; i++) s->ooo_len[i] = 0;   /* r835 */
        slot_send_raw(s, NET_TCP_SYN | NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
        s->snd_nxt++;
        // r809: уведомить приложение о новом соединении (data=NULL, len=0).
        // Windows-клиент ждёт баннер "220" сразу после TCP-connect.
        if (s->app_rx)
            s->app_rx((int)(s - g_slots), 0, 0, s->peer_ip, s->peer_mac, s->ctx);
        return 0;
    }
    if (!s->active) return 0;

    uint16_t pay = (uint16_t)(len - off);

    // r812: FIN обрабатываем ДО ACK-ветки. Раньше пакет FIN+ACK (fl=0x14)
    // попадал в ACK-ветку (pay==0 → return), ветка FIN не достигалась →
    // active оставался 1, слот "залипал" после каждого клиента, повторные
    // подключения игнорировались. Теперь: закрыть соединение всегда.
    if (flags & NET_TCP_FIN) {
        // FIN занимает 1 байт sequence (даже если с ним пришли данные).
        if (seq == s->rcv_nxt) s->rcv_nxt += pay + 1;
        else                   s->rcv_nxt += pay;
        if (pay && s->app_rx)
            s->app_rx((int)(s - g_slots), &pkt[off], pay, s->peer_ip, s->peer_mac, s->ctx);
        for (int i = 0; i < NET_TCP_OOO; i++) s->ooo_len[i] = 0;   /* r835 */
        slot_send_raw(s, NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
        s->active = 0;   // слот свободен для нового клиента
        return 0;
    }

    if (flags & NET_TCP_ACK) {
        if (pay == 0) return 0;   // чистый ACK
        int slot_i = (int)(s - g_slots);
        if (seq == s->rcv_nxt) {
            /* По порядку: отдать приложению, затем всё, что уже накопилось
               в буфере переупорядочивания и теперь стыкуется. */
            s->rcv_nxt += pay;
            if (s->app_rx)
                s->app_rx(slot_i, &pkt[off], pay, s->peer_ip, s->peer_mac, s->ctx);
            for (;;) {
                int idx = -1;
                for (int i = 0; i < NET_TCP_OOO; i++) {
                    if (s->ooo_len[i] && s->ooo_seq[i] == s->rcv_nxt) { idx = i; break; }
                }
                if (idx < 0) break;
                uint16_t n = s->ooo_len[idx];
                s->ooo_len[idx] = 0;
                s->rcv_nxt += n;
                if (s->app_rx)
                    s->app_rx(slot_i, s->ooo_data[idx], n,
                              s->peer_ip, s->peer_mac, s->ctx);
            }
            slot_send_raw(s, NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
            return 0;
        }
        if ((int32_t)(seq - s->rcv_nxt) > 0) {
            /* r835: сегмент ВПЕРЁД (потерян предыдущий кадр). Копим, чтобы
               отдать по порядку; клиенту сразу шлём ACK с текущим rcv_nxt —
               это дублирующий ACK и ускоряет ретрансмиссию пропущенного. */
            int free_idx = -1, dup = 0;
            for (int i = 0; i < NET_TCP_OOO; i++) {
                if (s->ooo_len[i] && s->ooo_seq[i] == seq) { dup = 1; break; }
                if (!s->ooo_len[i] && free_idx < 0) free_idx = i;
            }
            if (!dup && free_idx >= 0 && pay <= NET_TCP_MSS) {
                s->ooo_seq[free_idx] = seq;
                s->ooo_len[free_idx] = pay;
                memcpy(s->ooo_data[free_idx], &pkt[off], pay);
            }
            slot_send_raw(s, NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
            return 0;
        }
        /* seq < rcv_nxt — старый дубль: подтверждаем текущий rcv_nxt. */
        slot_send_raw(s, NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
        return 0;
    }
    return 0;
}

void net_tcp_poll(void) {}