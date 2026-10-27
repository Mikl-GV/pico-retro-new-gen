#ifndef WIFI_NET_H
#define WIFI_NET_H

#include <stdint.h>

/* Состояние IPv4 после DHCP. */
struct net_state {
    uint8_t have;      /* 1 = адрес подтверждён DHCP ACK */
    uint8_t ip[4];     /* наш IP (из yiaddr OFFER'а) */
    uint8_t net[4];    /* сеть = ip & mask (вычисляется при использовании) */
    uint8_t mask[4];   /* маска подсети (DHCP опция 1) */
    uint8_t gw[4];     /* шлюз (DHCP опция 3) */
    uint8_t dns[4];    /* DNS-сервер (DHCP опция 6; 0.0.0.0 если не пришёл) */
};

/* Отправить DHCP DISCOVER (broadcast). Возвращает 0 при успешной отправке. */
int net_dhcp_discover(struct net_state *st);

/* W.592: 1 если DHCP OFFER уже получен (стадия SELECTING, ждём ACK).
   Используется циклом DHCP, чтобы не слать повторный DISCOVER. */
int net_dhcp_offer_pending(void);

/* W.594: сброс состояния DHCP на старте нового join (dhcp_offer_seen=0). */
void net_dhcp_reset(void);

/* W.601/W.673: повторная отправка DHCP REQUEST (после OFFER, если ACK
   задержался) — ретраи как у dhclient. Идёт broadcast (L2+IP, 255.255.255.255)
   с xid того же DISCOVER, опции 50/54, ciaddr=0 (SELECTING).
   opt61 (client-id) намеренно НЕ шлётся (W.769) — оба сообщения консистентны. */
int net_dhcp_request(void);

/* W.722: забрать флаг «OFFER получен, нужен первый REQUEST». Парсер OFFER
   работает внутри чтения RX-FIFO, поэтому сам кадр не шлёт — это делает
   главный цикл join. Возвращает 1 один раз на каждый OFFER. */
int net_dhcp_take_request_pending(void);

/* W.650: вернуть текущий адрес для UI. ip/mask/gw заполняются, если адрес
   известен (OFFER получен); *have_ack=1 только после DHCP ACK.
   Возвращает 1 при известном адресе. */
int net_dhcp_get_ip(uint8_t ip[4], uint8_t mask[4], uint8_t gw[4], int *have_ack);

/* Входящий IPv4 (после 802.11 + LLC/SNAP + ethertype).
   src_mac — L2-адрес отправителя из 802.11-заголовка.
   Обрабатывает DHCP (OFFER -> REQUEST, ACK -> фиксация адреса) и ICMP
   echo request (отправляет echo reply). */
void net_handle_ipv4(const uint8_t *pkt, uint16_t len, const uint8_t *src_mac,
                     struct net_state *st);

/* Входящий ARP: отвечает на request, адресованный нашему IP. */
void net_handle_arp(const uint8_t *pkt, uint16_t len, const uint8_t *src_mac,
                    struct net_state *st);

#endif /* WIFI_NET_H */
