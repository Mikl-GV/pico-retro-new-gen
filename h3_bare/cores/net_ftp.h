#ifndef NET_FTP_H3_H
#define NET_FTP_H3_H

#include <stdint.h>

// r796: FTP-сервер поверх WiFi (TCP). Даёт доступ к SD/USB-флешке через
// Проводник Windows: ftp://<IP> — листинг, чтение, запись, удаление, mkdir.
// Использует единый FAT-слой (fat_list/fat_read_file/fat_write_file/
// fat_delete_file/fat_mkdir) — не важно, какой носитель активен (SD или
// USB, выбор сделан при загрузке).
//
// Запуск: ftp_server_start() после успешного join+DHCP (wifi_layer).
// Работает через net_tcp (порт 21) + PASV (data-порт).

// Старт FTP-сервера (порт 21). Возвращает 0 при успехе.
int  ftp_server_start(void);

// Опрос FTP (вызывать из wifi_layer_poll после IP). Обрабатывает
// транзакции, ретрансмиссии.
void ftp_server_poll(void);

// Активен ли FTP (есть ли установленное TCP-соединение / слушатель).
int  ftp_server_active(void);

#endif /* NET_FTP_H3_H */