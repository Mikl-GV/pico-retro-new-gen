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

/* r835: построчная трассировка FTP-команд/ответов выключена (в т.ч. не
   логируется аргумент PASS). Ключевые события — старт сервера, завершение
   STOR, ошибки записи — печатаются всегда. Включить трассировку: FTP_DBG 1. */
#define FTP_DBG 0
#if FTP_DBG
extern int uart0_printf(const char* fmt, ...);
#define FTPLOG(...) uart0_printf(__VA_ARGS__)
#else
#define FTPLOG(...) do {} while (0)
#endif

// ---- Состояние ----
static char    g_cwd[FAT_NAME_LEN] = "";
static int     g_ctrl_slot = -1;
static int     g_data_slot = -1;
static uint16_t g_pasv_port = 21000;

// Состояние приёма STOR (r832): данные пишутся в FAT сразу, без буфера —
// файлы больше 64 КБ больше не обрезаются.
static char     g_stor_name[FAT_NAME_LEN];
static char     g_stor_dir[FAT_NAME_LEN];
static int      g_stor_active = 0;   /* 1 = идёт приём STOR (ждём FIN data-слота) */
static int      g_stor_failed = 0;   /* r845: ошибка записи в FAT — аборт */
static uint32_t g_stor_len;          /* принято байт (для лога) */
/* r833: буфер-накопитель 8 КБ. Писать в FAT на каждый TCP-сегмент (1460 б)
   слишком дорого (USB на сектор) — WiFi-RX простаивал, кадры терялись и
   клиент рвал канал. Копим и сбрасываем пачкой. */
static uint8_t  g_stor_buf[8192];
static uint32_t g_stor_used;

// ---- Утилиты ----
static void ftp_send_ctrl(const char* s) {
    if (g_ctrl_slot >= 0 && net_tcp_connected(g_ctrl_slot))
        net_tcp_send(g_ctrl_slot, NET_TCP_PSH, (const uint8_t*)s, (uint16_t)strlen(s));
}

static void ftp_reply(int code, const char* msg) {
    // r818: одна строка ответа одним TCP-сегментом (раньше — 3 сегмента:
    // код, текст, CRLF). Меньше пакетов, меньше риск потери на WiFi.
    char buf[320];
    int n = 0;
    buf[n++] = (char)('0' + code / 100);
    buf[n++] = (char)('0' + (code / 10) % 10);
    buf[n++] = (char)('0' + code % 10);
    buf[n++] = ' ';
    for (int i = 0; msg[i] && n < (int)sizeof(buf) - 3; i++) buf[n++] = msg[i];
    buf[n++] = '\r';
    buf[n++] = '\n';
    buf[n] = 0;
    FTPLOG("  ftp> %d %s\r\n", code, msg);
    ftp_send_ctrl(buf);
}

// Полный путь (абсолютный) — g_cwd + arg, если arg относительный.
// r845: нормализуем «/a/b/../c» и повторные слеши; «..» выше корня
// остаётся у корня (не даём уйти за /roms).
static void make_path(const char* arg, char* out, int outsz) {
    char tmp[FAT_NAME_LEN];
    if (arg[0] == '/') {
        strncpy(tmp, arg, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
    } else {
        if (g_cwd[0]) {
            strncpy(tmp, g_cwd, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
            int l = (int)strlen(tmp);
            if (l > 0 && tmp[l-1] != '/') { tmp[l++]='/'; tmp[l]=0; }
            strncat(tmp, arg, sizeof(tmp) - strlen(tmp) - 1);
        } else {
            strncpy(tmp, "/roms/", sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
            strncat(tmp, arg, sizeof(tmp) - strlen(tmp) - 1);
        }
    }
    /* Нормализация: сворачиваем "x/../" и "//". Проходим слева направо,
       собираем компоненты. */
    char* dst = out;
    const char* src = tmp;
    while (*src) {
        if (src[0]=='/' && src[1]=='/') { src++; continue; }
        if (src[0]=='/' && src[1]=='.' && (src[2]=='/' || src[2]==0)) {
            src += 2;   /* "/." или "/./" — просто пропустить */
            if (*src == '/') src++;
            continue;
        }
        if (src[0]=='/' && src[1]=='.' && src[2]=='.' &&
            (src[3]=='/' || src[3]==0)) {
            src += 3;
            if (*src == '/') src++;
            /* подняться на уровень: откатить последний компонент в out */
            if (dst > out) {
                dst--;                     /* убрать завершающий '/' */
                while (dst > out && dst[-1] != '/') dst--;
            }
            continue;
        }
        if ((int)(dst - out) < outsz - 2) *dst++ = *src++;
        else break;
    }
    if (dst == out) { *dst++ = '/'; }
    else if (dst[-1] == '/' && dst > out + 1) dst--;
    *dst = 0;
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
    /* r830: видно, доходят ли данные больших файлов. Печатаем только
       реальные данные (len>0) и только в STOR — раньше счётчик съедали
       служебные вызовы (len=0) при connect на LIST. */
    if (len > 0 && g_stor_active) {
        /* r833: копим в буфер 8 КБ, в FAT пишем пачкой. */
        const uint8_t* p = data;
        uint32_t n = len;
        while (n) {
            uint32_t space = (uint32_t)sizeof(g_stor_buf) - g_stor_used;
            uint32_t take = (n < space) ? n : space;
            memcpy(g_stor_buf + g_stor_used, p, take);
            g_stor_used += take;
            p += take;
            n -= take;
            if (g_stor_used == (uint32_t)sizeof(g_stor_buf)) {
                if (fat_write_data(g_stor_buf, g_stor_used) < 0) {
                    extern int uart0_printf(const char* fmt, ...);
                    uart0_printf("  ftp: STOR write err total=%u\r\n", g_stor_len);
                    /* r845: запись не удалась — останавливаем приём, при
                       финализации файл будет удалён (fat_abort_write). */
                    g_stor_failed = 1;
                    g_stor_used = 0;
                    return 0;
                }
                g_stor_used = 0;
            }
        }
        g_stor_len += len;
        /* прогресс раз в 16 КБ */
        static uint32_t mark;
        if (g_stor_len - mark >= 16384u) {
            mark = g_stor_len;
            extern int uart0_printf(const char* fmt, ...);
            uart0_printf("  ftp: STOR rx %u\r\n", g_stor_len);
        }
    }
    (void)slot;
    return 0;
}

// ---- Обработчик control-слота ----
static int ftp_ctrl_rx(int slot, const uint8_t* data, uint16_t len,
                       const uint8_t* src_ip, const uint8_t* src_mac, void* ctx)
{
    (void)src_ip; (void)src_mac; (void)ctx;
    // r809: сигнал "TCP-connect" (data==NULL, len==0) — слать приглашение 220.
    // Windows-проводник ждёт баннер сразу после установки соединения;
    // без него FTP-подключение виснет (проводник молча ждёт 220).
    if (!data || len == 0) {
        ftp_send_ctrl("220 MultiTool FTP ready\r\n");
        return 0;
    }
    static char line[256];
    static uint16_t ll = 0;
    for (uint16_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            /* r822: срезаем CR. Клиент шлёт "CMD\r\n"; при разборе команды
               БЕЗ аргумента (PASV, PWD, LIST) CR попадал в имя команды ->
               cmd="PASV\r" и strcmp(cmd,"PASV") не совпадал -> 502.
               В UART CR возвращал каретку, поэтому печать выглядела верной.
               Команды с аргументом (USER user\r) не страдали: разбор
               останавливался на пробеле. */
            if (ll && line[ll-1] == '\r') ll--;
            line[ll] = 0; ll = 0;
            // путь команды и аргумент
            char cmd[8]; int ci = 0;
            while (line[ci] && line[ci] != ' ' && line[ci] != '\r' && ci < 7) { cmd[ci] = line[ci]; ci++; }
            cmd[ci] = 0;
            const char* arg = line[ci] ? line + ci + 1 : "";
            for (int k = 0; cmd[k]; k++) if (cmd[k] >= 'a' && cmd[k] <= 'z') cmd[k] -= 32;
            while (*arg == ' ') arg++;
            /* r822: пустая строка (клиент иногда шлёт голый CRLF) — не
               ошибка, просто игнорируем; раньше отвечали 502. */
            if (!cmd[0]) continue;

            /* r821: трассировка команд (под FTP_DBG — аргумент PASS не логируется). */
            FTPLOG("  ftp< %s %s\r\n", cmd, arg);

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
            else if (strcmp(cmd,"CDUP")==0) {
                /* r845 (аудит, «CDUP ничего не меняет»): реально поднимаемся
                   на уровень выше. С /roms/nes → /roms, с /roms → корень. */
                if (g_cwd[0]) {
                    char p[FAT_NAME_LEN];
                    strncpy(p, g_cwd, sizeof(p)-1); p[sizeof(p)-1]=0;
                    int l=(int)strlen(p);
                    while (l>1 && p[l-1]=='/') p[--l]=0;
                    char* slash = strrchr(p, '/');
                    if (slash) {
                        if (slash == p) g_cwd[0]=0;
                        else { *slash=0; strncpy(g_cwd, p, sizeof(g_cwd)-1); g_cwd[sizeof(g_cwd)-1]=0; }
                    } else g_cwd[0]=0;
                }
                ftp_reply(250,"cdup ok");
            }
            else if (strcmp(cmd,"PASV")==0) {
                /* Сначала открыть data-слушатель на НОВОМ порту, затем
                   сообщить клиенту именно его номер. r845: если слушатель
                   не открылся — не отвечаем 227 (клиент пошёл бы на
                   несуществующий порт). */
                if (g_data_slot >= 0) { net_tcp_close(g_data_slot); g_data_slot=-1; }
                g_pasv_port = 21000 + (uint8_t)((unsigned)(slot+1)*37 % 1000);
                g_data_slot = net_tcp_listen(g_pasv_port, ftp_data_rx, 0);
                if (g_data_slot < 0) {
                    ftp_reply(425, "cannot open data connection");
                } else {
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
                        // r818: папка по атрибуту FAT (0x10), а не по size==0 —
                        // иначе пустой файл выглядел каталогом. Fallback на
                        // size==0 для записей без атрибута.
                        int isdir = (dirs[i].attr & 0x10) != 0
                                 || (dirs[i].attr == 0 && dirs[i].size == 0);
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
                    /* r823: закрыть data-канал ПОСЛЕ данных и ДО 226 —
                       клиент читает до EOF (FIN), только потом ждёт 226.
                       Без FIN он висел до таймаута и переподключался. */
                    if (g_data_slot >= 0) { net_tcp_close(g_data_slot); g_data_slot = -1; }
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
                    /* r823: FIN до 226 — клиент ждёт EOF на data-канале. */
                    if (g_data_slot >= 0) { net_tcp_close(g_data_slot); g_data_slot = -1; }
                    ftp_reply(226, "transfer complete");
                }
            }
            else if (strcmp(cmd,"STOR")==0) {
                char p[FAT_NAME_LEN]; make_path(arg, p, sizeof(p));
                char dir[FAT_NAME_LEN], nm[FAT_NAME_LEN];
                path_split(p, dir, sizeof(dir), nm, sizeof(nm));
                g_stor_len = 0;
                g_stor_used = 0;
                g_stor_failed = 0;
                strncpy(g_stor_name, nm, sizeof(g_stor_name)-1);
                g_stor_name[sizeof(g_stor_name)-1] = 0;
                strncpy(g_stor_dir, dir, sizeof(g_stor_dir)-1);
                g_stor_dir[sizeof(g_stor_dir)-1] = 0;
                /* r832: открываем потоковую запись ДО 150 — если каталог/имя
                   не годятся, клиент сразу получит 550. */
                if (fat_write_begin(dir[0] ? dir : "/roms", nm) < 0) {
                    ftp_reply(550, "cannot create file");
                } else {
                    g_stor_active = 1;
                    ftp_reply(150, "ok to send data");
                }
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

// Статус приёма STOR: данные пишутся в FAT потоково (fat_write_data).

void ftp_server_poll(void) {
    net_tcp_poll();
    // Завершение STOR: клиент закрыл data-слот (FIN → net_tcp сбросил active).
    // Когда data-слот перестал быть connected — пишем принятые данные в FAT.
    if (g_stor_active) {
        if (g_data_slot < 0 || !net_tcp_connected(g_data_slot)) {
            g_stor_active = 0;
            int rc = -1;
            {
                extern int uart0_printf(const char* fmt, ...);
                uart0_printf("  ftp: STOR fin len=%u dir=%s name=%s\r\n",
                             g_stor_len, g_stor_dir[0] ? g_stor_dir : "/roms",
                             g_stor_name);
                if (g_stor_failed) {
                    /* r845: была ошибка записи — удаляем частичный файл. */
                    uart0_printf("  ftp: STOR abort (write error)\r\n");
                    fat_abort_write();
                    g_stor_used = 0;
                } else {
                    /* r833: дослать остаток из буфера, затем закрыть файл. */
                    if (g_stor_used) {
                        if (fat_write_data(g_stor_buf, g_stor_used) < 0)
                            uart0_printf("  ftp: STOR tail write err\r\n");
                        g_stor_used = 0;
                    }
                    rc = fat_write_end();
                    uart0_printf("  ftp: STOR write rc=%d\r\n", rc);
                }
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