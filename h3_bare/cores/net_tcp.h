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

// r964: 6 слотов = до 3 одновременных control-сессий на :21 + data-слоты
// PASV. Раньше было 2 слота: второе TCP-подключение (автопоиск клиента,
// Проводник) переустанавливало единственный control-слот и обрывало уже
// идущий обмен — ответы съезжали (клиент видел «200 type set»).
#define NET_TCP_SLOTS    6
#define NET_TCP_WINDOW   4096u
#define NET_TCP_MSS      1460u
// r835: глубина буфера переупорядочивания (сегментов на слот). Покрывает
// потерю нескольких кадров WiFi подряд без остановки потока.
#define NET_TCP_OOO      4
// r846 (аудит, «нет ретрансмиссий»): очередь повторной отправки. Потеря
// WiFi-кадра на RETR/LIST/ответе FTP без повтора = вечная пауза у клиента.
// r964: 12 сегментов (>16 КБ) — листинг каталога отдаётся целиком и должен
// повторяться весь, а не только первые 8 сегментов.
#define NET_TCP_RETX     12
#define NET_TCP_RTO_MAX  15   // ~15 вызовов poll (~1-2 c на WiFi 100 Мбит) до сдачи

// Колбэк прикладного уровня: данные сегмента. Возвращает 0.
typedef int (*net_tcp_app_rx_t)(int slot, const uint8_t* data, uint16_t len,
                                const uint8_t* src_ip, const uint8_t* src_mac,
                                void* ctx);

// Обработка входящего TCP-сегмента (из net_handle_ipv4).
// pkt — TCP-заголовок+данные, len — длина, src_ip/src_mac — отправитель.
int  net_tcp_handle(const uint8_t* pkt, uint16_t len, const uint8_t* src_mac,
                    const uint8_t* src_ip);

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

// r964: сколько сегментов слота ещё не подтверждено клиентом.
// >0 — закрывать соединение нельзя (данные/226 потеряются без повтора).
int  net_tcp_unacked(int slot);

// r964: сколько слотов слушает порт (проверка/восстановление :21).
int  net_tcp_listener_count(uint16_t port);

// Опрос исходящих ретрансляций (вызывать из wifi_layer_poll).
void net_tcp_poll(void);

#define NET_TCP_FIN 0x01u
#define NET_TCP_SYN 0x02u
#define NET_TCP_RST 0x04u
#define NET_TCP_PSH 0x08u
#define NET_TCP_ACK 0x10u

#endif /* NET_TCP_H3_H */