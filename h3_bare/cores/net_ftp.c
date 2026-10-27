// net_ftp.c — упрощённый FTP-сервер поверх WiFi для доступа к SD/USB.
// r796. Два TCP-слота: control (порт 21) + data (PASV-порт).
// Поддерживает команды для Проводника Windows:
//   USER/PASS, SYST, TYPE, PWD/CWD/CDUP, PASV,
//   LIST/NLST, RETR, STOR, DELE, MKD, RMD, QUIT, NOOP.
// Файлы ходят через единый FAT-слой (активный источник — SD или USB).
#include <string.h>
#include "net_ftp.h"
#include "net_tcp.h"
#include "h3_sdc.h"
#include "fat.h"
#include "uart.h"

// ---- Состояние ----
static char    g_cwd[FAT_NAME_LEN] = "";
static int     g_ctrl_slot = -1;
static int     g_data_slot = -1;
static uint16_t g_pasv_port = 21000;

// Буфер приёма STOR (до 64 КБ) и позиция.
static uint8_t  g_stor[65536];
static uint32_t g_stor_len;
static char     g_stor_name[FAT_NAME_LEN];
static char     g_stor_dir[FAT_NAME_LEN];
static int      g_stor_active = 0;   /* 1 = идёт приём STOR (ждём FIN data-слота) */

// ---- Утилиты ----
static void ftp_send_ctrl(const char* s) {
    if (g_ctrl_slot >= 0 && net_tcp_connected(g_ctrl_slot))
        net_tcp_send(g_ctrl_slot, NET_TCP_PSH, (const uint8_t*)s, (uint16_t)strlen(s));
}

static void ftp_reply(int code, const char* msg) {
    char t[8];
    t[0] = (char)('0' + code/100);
    t[1] = (char)('0' + (code/10)%10);
    t[2] = (char)('0' + code%10);
    t[3] = ' ';
    t[4] = 0;
    ftp_send_ctrl(t);
    ftp_send_ctrl(msg);
    ftp_send_ctrl("\r\n");
}

// Полный путь (абсолютный) — g_cwd + arg, если arg относительный.
static void make_path(const char* arg, char* out, int outsz) {
    if (arg[0] == '/') {
        strncpy(out, arg, outsz-1); out[outsz-1]=0;
    } else {
        if (g_cwd[0]) {
            strncpy(out, g_cwd, outsz-1); out[outsz-1]=0;
            int l = (int)strlen(out);
            if (l > 0 && out[l-1] != '/') { out[l++]='/'; out[l]=0; }
            strncat(out, arg, outsz - strlen(out) - 1);
        } else {
            strncpy(out, "/roms/", outsz-1); out[outsz-1]=0;
            strncat(out, arg, outsz - strlen(out) - 1);
        }
    }
}

// Разделить путь "/roms/nes/game.nes" → dir="/roms/nes", name="game.nes".
// Корень "/roms" → dir="/roms", name="". Возвращает 1 если есть каталог.
static int path_split(const char* p, char* dir, int dirsz, char* name, int namesz) {
    if (!p || !*p) return 0;
    strncpy(dir, p, dirsz-1); dir[dirsz-1]=0;
    char* slash = strrchr(dir, '/');
    if (slash) {
        *slash = 0;
        if (dir[0] == 0) strcpy(dir, "/");
        if (slash[1]) {
            strncpy(name, slash+1, namesz-1); name[namesz-1]=0;
        } else name[0] = 0;
        return 1;
    }
    name[0] = 0;
    return 1;
}

// Отправить данные по data-слоту.
static void ftp_data_send(const uint8_t* d, int n) {
    if (g_data_slot >= 0 && net_tcp_connected(g_data_slot))
        net_tcp_send(g_data_slot, NET_TCP_PSH, d, (uint16_t)n);
}

// ---- Обработчик data-слота (для STOR) ----
static int ftp_data_rx(int slot, const uint8_t* data, uint16_t len,
                       const uint8_t* src_ip, const uint8_t* src_mac, void* ctx)
{
    (void)src_ip; (void)src_mac; (void)ctx;
    if (g_stor_len + len <= sizeof(g_stor)) {
        memcpy(&g_stor[g_stor_len], data, len);
        g_stor_len += len;
    }
    (void)slot;
    return 0;
}

// ---- Обработчик control-слота ----
static int ftp_ctrl_rx(int slot, const uint8_t* data, uint16_t len,
                       const uint8_t* src_ip, const uint8_t* src_mac, void* ctx)
{
    (void)src_ip; (void)src_mac; (void)ctx;
    static char line[256];
    static uint16_t ll = 0;
    for (uint16_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            line[ll] = 0; ll = 0;
            // путь команды и аргумент
            char cmd[8]; int ci = 0;
            while (line[ci] && line[ci] != ' ' && ci < 7) { cmd[ci] = line[ci]; ci++; }
            cmd[ci] = 0;
            const char* arg = line[ci] ? line + ci + 1 : "";
            for (int k = 0; cmd[k]; k++) if (cmd[k] >= 'a' && cmd[k] <= 'z') cmd[k] -= 32;
            while (*arg == ' ') arg++;

            if (strcmp(cmd,"USER")==0 || strcmp(cmd,"PASS")==0) ftp_reply(230,"logged in");
            else if (strcmp(cmd,"SYST")==0) ftp_reply(215,"UNIX Type: L8");
            else if (strcmp(cmd,"TYPE")==0) ftp_reply(200,"type set");
            else if (strcmp(cmd,"PWD")==0 || strcmp(cmd,"XPWD")==0) {
                char pr[180]; pr[0]='"'; pr[1]=0;
                strcat(pr, g_cwd[0]?g_cwd:"/roms"); strcat(pr, "\"");
                ftp_reply(257, pr);
            }
            else if (strcmp(cmd,"CWD")==0) {
                char p[FAT_NAME_LEN]; make_path(arg, p, sizeof(p));
                // нормализация: убрать хвостовой '/'
                int l=(int)strlen(p);
                while (l>1 && p[l-1]=='/') p[--l]=0;
                if (strcmp(p, "/")==0) { g_cwd[0]=0; ftp_reply(250,"cwd ok"); }
                else {
                    strncpy(g_cwd, p, sizeof(g_cwd)-1); g_cwd[sizeof(g_cwd)-1]=0;
                    ftp_reply(250,"cwd ok");
                }
            }
            else if (strcmp(cmd,"CDUP")==0) { ftp_reply(250,"cdup ok"); }
            else if (strcmp(cmd,"PASV")==0) {
                /* Сначала открыть data-слушатель на НОВОМ порту, затем
                   сообщить клиенту именно его номер. */
                if (g_data_slot >= 0) { net_tcp_close(g_data_slot); g_data_slot=-1; }
                g_pasv_port = 21000 + (uint8_t)((unsigned)(slot+1)*37 % 1000);
                g_data_slot = net_tcp_listen(g_pasv_port, ftp_data_rx, 0);
                uint8_t ip[4]; h3_wifi_get_ip(ip);
                char buf[96]; int n=0;
                buf[n++]='(';
                for (int k=0;k<4;k++) {
                    unsigned v=ip[k]; char t[8]; int tn=0; char ts[6]; int ti=0;
                    if (v==0) ts[ti++]='0';
                    while (v) { ts[ti++]=(char)('0'+v%10); v/=10; }
                    while (ti) t[tn++]=ts[--ti];
                    for (int p=0;p<tn;p++) buf[n++]=t[p];
                    buf[n++]=',';
                }
                unsigned v=g_pasv_port>>8; char t[8]; int tn=0; char ts[6]; int ti=0;
                if (v==0) ts[ti++]='0';
                while (v) { ts[ti++]=(char)('0'+v%10); v/=10; }
                while (ti) t[tn++]=ts[--ti];
                for (int p=0;p<tn;p++) buf[n++]=t[p];
                buf[n++]=',';
                tn=0; ti=0; v=g_pasv_port&0xFF;
                if (v==0) ts[ti++]='0';
                while (v) { ts[ti++]=(char)('0'+v%10); v/=10; }
                while (ti) t[tn++]=ts[--ti];
                for (int p=0;p<tn;p++) buf[n++]=t[p];
                buf[n++]=')'; buf[n]=0;
                ftp_reply(227, buf);
            }
            else if (strcmp(cmd,"LIST")==0 || strcmp(cmd,"NLST")==0) {
                char body[4096];
                uint32_t bl = 0;
                fat_entry_t* dirs = fat_scratch();
                const char* d = g_cwd[0] ? g_cwd : "/roms";
                int n = fat_list(d, dirs, FAT_MAX_ENTRIES);
                if (n < 0) { ftp_reply(550, "no such dir"); }
                else {
                    for (int i=0; i<n && bl < sizeof(body)-40; i++) {
                        int isdir = (dirs[i].size==0);
                        body[bl++] = isdir ? 'd' : '-';
                        const char* perm = "rw-------";
                        for (int p=0;p<9 && bl<sizeof(body)-1;p++) body[bl++]=perm[p];
                        char sz[16]; int sn=0; unsigned v=(unsigned)dirs[i].size;
                        if (v==0) sz[sn++]='0';
                        while (v) { char tmp[12]; int ti=0; while (v) { tmp[ti++]=(char)('0'+v%10); v/=10; } while (ti) sz[sn++]=tmp[--ti]; }
                        // нормализовать: пока <10 цифр — пробелы
                        for (int p=sn; p<10 && bl<sizeof(body)-1; p++) body[bl++]=' ';
                        for (int p=0; p<sn && bl<sizeof(body)-1; p++) body[bl++]=sz[p];
                        static const char* dt = " Jan  1  2026 ";
                        for (int p=0; dt[p] && bl<sizeof(body)-1; p++) body[bl++]=dt[p];
                        int nl=(int)strlen(dirs[i].name);
                        if (nl>80) nl=80;
                        for (int p=0;p<nl && bl<sizeof(body)-1;p++) body[bl++]=dirs[i].name[p];
                        body[bl++]='\r'; body[bl++]='\n';
                    }
                    ftp_reply(150, "listing");
                    ftp_data_send((const uint8_t*)body, (int)bl);
                    ftp_reply(226, "transfer complete");
                }
            }
            else if (strcmp(cmd,"RETR")==0) {
                char p[FAT_NAME_LEN]; make_path(arg, p, sizeof(p));
                char dir[FAT_NAME_LEN], nm[FAT_NAME_LEN];
                fat_entry_t f;
                if (!path_split(p, dir, sizeof(dir), nm, sizeof(nm)) || !nm[0] ||
                    !fat_find(dir, nm, &f)) { ftp_reply(550, "no such file"); }
                else {
                    ftp_reply(150, "opening binary");
                    static uint8_t rdbuf[512];
                    uint32_t off = 0;
                    while (off < f.size) {
                        int got = fat_read_file(&f, off, rdbuf, sizeof(rdbuf));
                        if (got <= 0) break;
                        for (int o = 0; o < got; o += 512) {
                            int chunk = got - o;
                            if (chunk > 512) chunk = 512;
                            ftp_data_send(&rdbuf[o], chunk);
                        }
                        off += (uint32_t)got;
                    }
                    ftp_reply(226, "transfer complete");
                }
            }
            else if (strcmp(cmd,"STOR")==0) {
                char p[FAT_NAME_LEN]; make_path(arg, p, sizeof(p));
                char dir[FAT_NAME_LEN], nm[FAT_NAME_LEN];
                path_split(p, dir, sizeof(dir), nm, sizeof(nm));
                g_stor_len = 0;
                strncpy(g_stor_name, nm, sizeof(g_stor_name)-1);
                g_stor_name[sizeof(g_stor_name)-1] = 0;
                strncpy(g_stor_dir, dir, sizeof(g_stor_dir)-1);
                g_stor_dir[sizeof(g_stor_dir)-1] = 0;
                g_stor_active = 1;
                ftp_reply(150, "ok to send data");
                /* Данные асинхронно копятся в g_stor (ftp_data_rx);
                   запись в FAT — в ftp_server_poll по закрытию data-слота. */
            }
            else if (strcmp(cmd,"DELE")==0) {
                char p[FAT_NAME_LEN]; make_path(arg, p, sizeof(p));
                char dir[FAT_NAME_LEN], nm[FAT_NAME_LEN];
                if (!path_split(p, dir, sizeof(dir), nm, sizeof(nm)) || !nm[0] ||
                    fat_delete_file(dir, nm) != 0) ftp_reply(550, "delete fail");
                else ftp_reply(250,"deleted");
            }
            else if (strcmp(cmd,"MKD")==0) {
                char p[FAT_NAME_LEN]; make_path(arg, p, sizeof(p));
                // fat_mkdir(parent, name) — вычленяем последний компонент
                int l=(int)strlen(p);
                char parent[FAT_NAME_LEN]; strncpy(parent, p, sizeof(parent)-1); parent[sizeof(parent)-1]=0;
                char* slash = strrchr(parent, '/');
                if (slash && slash != parent) { *slash=0; const char* nm = slash+1;
                    if (fat_mkdir(parent, nm)==0) { char r[180]; r[0]='"'; r[1]=0; strcat(r,p); strcat(r,"\""); ftp_reply(257, r); }
                    else ftp_reply(550, "mkdir fail");
                } else ftp_reply(550, "mkdir fail");
            }
            else if (strcmp(cmd,"RMD")==0) { ftp_reply(502, "rmd stub"); }
            else if (strcmp(cmd,"QUIT")==0) { ftp_reply(221,"bye"); }
            else if (strcmp(cmd,"NOOP")==0) { ftp_reply(200,"ok"); }
            else ftp_reply(502, "command not implemented");
        } else if (ll < sizeof(line)-1) line[ll++] = c;
        else ll = 0;
    }
    return 0;
}

// ---- Публичный API ----
int ftp_server_start(void) {
    if (g_ctrl_slot >= 0) return 0;
    g_cwd[0] = 0;
    g_ctrl_slot = net_tcp_listen(21, ftp_ctrl_rx, 0);
    uart_print("ftp: server on :21 (PASV :21000..)\r\n");
    return g_ctrl_slot >= 0 ? 0 : -1;
}

// Статус приёма STOR (1 = идём; данные копятся в g_stor) — вверху файла.

void ftp_server_poll(void) {
    net_tcp_poll();
    // Завершение STOR: клиент закрыл data-слот (FIN → net_tcp сбросил active).
    // Когда data-слот перестал быть connected — пишем принятые данные в FAT.
    if (g_stor_active) {
        if (g_data_slot < 0 || !net_tcp_connected(g_data_slot)) {
            g_stor_active = 0;
            int rc = -1;
            if (g_stor_len > 0) {
                rc = fat_write_file(g_stor_dir[0] ? g_stor_dir : "/roms",
                                    g_stor_name, g_stor, g_stor_len);
            }
            if (rc >= 0) ftp_reply(226, "transfer complete");
            else         ftp_reply(426, "write failed");
            if (g_data_slot >= 0) { net_tcp_close(g_data_slot); g_data_slot = -1; }
        }
    }
}

int ftp_server_active(void) {
    return (g_ctrl_slot >= 0 && net_tcp_connected(g_ctrl_slot));
}