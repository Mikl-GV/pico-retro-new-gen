#ifndef NET_TCP_H3_H
#define NET_TCP_H3_H

#include <stdint.h>

// r796: минимальный TCP-стек для FTP-сервера (файловый трансфер по WiFi).
// Работает поверх WiFi (h3_wifi_tx_data / входящие IPv4 из wifi_net).
//
// Модель: ДВА слота соединений — control (порт 21) и data (PASV-порт).
// SEQ/ACK отслеживаются честно: SYN/SYN-ACK/ACK, приём PSH-данных,
// FIN/RST.
//
// Слот идентифицируется номером (0/1). Слушатель: net_tcp_listen(port,..)
// возвращает слот; когда клиент коннектится на этот порт — слот активен.

#define NET_TCP_SLOTS    2
#define NET_TCP_WINDOW   4096u
#define NET_TCP_MSS      1460u

// Колбэк прикладного уровня: данные сегмента. Возвращает 0.
typedef int (*net_tcp_app_rx_t)(int slot, const uint8_t* data, uint16_t len,
                                const uint8_t* src_ip, const uint8_t* src_mac,
                                void* ctx);

// Обработка входящего TCP-сегмента (из net_handle_ipv4).
// pkt — TCP-заголовок+данные, len — длина, src_ip/src_mac — отправитель,
// my_ip — наш адрес.
int  net_tcp_handle(const uint8_t* pkt, uint16_t len, const uint8_t* src_mac,
                    const uint8_t* src_ip, const uint8_t my_ip[4]);

// Начать прослушивание порта. Возвращает слот (0/1) или -1.
int  net_tcp_listen(uint16_t port, net_tcp_app_rx_t app_rx, void* ctx);

// Снять слушателя/закрыть слот.
void net_tcp_close(int slot);

// Отправить данные/сегмент приложения в соединении slot.
void net_tcp_send(int slot, uint8_t flags, const uint8_t* data, uint16_t len);

// Сбросить соединение (RST).
void net_tcp_abort(int slot);

// Соединение в слоте установлено?
int  net_tcp_connected(int slot);

// Опрос исходящих ретрансляций (вызывать из wifi_layer_poll).
void net_tcp_poll(void);

#define NET_TCP_FIN 0x01u
#define NET_TCP_SYN 0x02u
#define NET_TCP_RST 0x04u
#define NET_TCP_PSH 0x08u
#define NET_TCP_ACK 0x10u

#endif /* NET_TCP_H3_H */