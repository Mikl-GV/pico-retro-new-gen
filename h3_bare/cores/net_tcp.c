// net_tcp.c — минимальный TCP-стек с двумя слотами (control + data) для FTP.
// r796: поверх WiFi (h3_wifi_tx_data / IPv4 из wifi_net).
#include <string.h>
#include "net_tcp.h"
#include "h3_sdc.h"
#include "uart.h"

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
    net_tcp_app_rx_t app_rx;
    void*    ctx;
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
    acc += 6u << 8;
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
    uint8_t pkt[40 + NET_TCP_MSS];
    uint8_t my_mac[6], my_ip[4];
    h3_wifi_get_mac(my_mac);
    if (h3_wifi_get_ip(my_ip) != 0) return;

    uint16_t tot = 20 + 20 + len;
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 0x45; pkt[1] = 0;
    pkt[2] = (uint8_t)(tot >> 8); pkt[3] = (uint8_t)tot;
    pkt[8] = 64; pkt[9] = 6;
    memcpy(&pkt[12], my_ip, 4);
    memcpy(&pkt[16], s->peer_ip, 4);
    ipv4_csum(pkt);

    pkt[20] = (uint8_t)(s->local_port >> 8); pkt[21] = (uint8_t)s->local_port;
    pkt[22] = (uint8_t)(s->peer_port >> 8);  pkt[23] = (uint8_t)s->peer_port;
    pkt[24] = (uint8_t)(seq >> 24); pkt[25] = (uint8_t)(seq >> 16);
    pkt[26] = (uint8_t)(seq >> 8);  pkt[27] = (uint8_t)seq;
    pkt[28] = (uint8_t)(ack >> 24); pkt[29] = (uint8_t)(ack >> 16);
    pkt[30] = (uint8_t)(ack >> 8);  pkt[31] = (uint8_t)ack;
    pkt[32] = 0x50;
    pkt[33] = flags;
    pkt[34] = (uint8_t)(NET_TCP_WINDOW >> 8); pkt[35] = (uint8_t)NET_TCP_WINDOW;
    if (data && len) memcpy(&pkt[40], data, len);
    tcp_csum(pkt, (uint16_t)(20 + len));
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
    if (slot >= 0 && slot < NET_TCP_SLOTS) memset(&g_slots[slot], 0, sizeof(g_slots[0]));
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
    extern int uart0_printf(const char* fmt, ...);
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

    // r810-DIAG: каждый входящий TCP (временно, для отладки FTP)
    uart0_printf("  net: tcp %u:%u->%u fl=0x%X seq=%u ack=%u len=%u\r\n",
                 (unsigned)src_ip[0], (unsigned)sp, (unsigned)dp,
                 (unsigned)flags, (unsigned)seq, (unsigned)ack, (unsigned)len);

    struct tcp_slot* s = slot_by_dport(dp);
    if (!s) { uart0_printf("  net: tcp no-slot dp=%u\r\n", (unsigned)dp); return 0; }

    memcpy(s->peer_ip, src_ip, 4);
    memcpy(s->peer_mac, src_mac, 6);

    if (flags & NET_TCP_SYN) {
        if (s->active) {
            // Тот же сокет (retransmit SYN) — повторный SYN-ACK.
            if (s->peer_port == sp) {
                slot_send_raw(s, NET_TCP_SYN | NET_TCP_ACK, s->snd_nxt - 1,
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
        if (seq == s->rcv_nxt) s->rcv_nxt += pay ? pay : 1;
        else                   s->rcv_nxt += pay;
        if (pay && s->app_rx)
            s->app_rx((int)(s - g_slots), &pkt[off], pay, s->peer_ip, s->peer_mac, s->ctx);
        slot_send_raw(s, NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
        s->active = 0;   // слот свободен для нового клиента
        return 0;
    }

    if (flags & NET_TCP_ACK) {
        if (pay == 0) return 0;   // чистый ACK
        if (seq != s->rcv_nxt) {
            // retransmit-ACK с ожидаемым номером
            slot_send_raw(s, NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
            return 0;
        }
        s->rcv_nxt += pay;
        if (s->app_rx)
            s->app_rx((int)(s - g_slots), &pkt[off], pay, s->peer_ip, s->peer_mac, s->ctx);
        slot_send_raw(s, NET_TCP_ACK, s->snd_nxt, s->rcv_nxt, 0, 0);
        return 0;
    }
    return 0;
}

void net_tcp_poll(void) {}