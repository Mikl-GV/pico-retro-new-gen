// net_ftp.c — упрощённый FTP-сервер поверх WiFi для доступа к SD/USB.
// r964. Несколько одновременных control-сессий (:21) + PASV data-канал на
// каждую. data-канал закрывается (FIN) только после подтверждения данных
// клиентом — иначе потерянный WiFi-кадр нечем повторить.
// Поддерживает команды для Проводника Windows:
//   USER/PASS, SYST, TYPE, PWD/CWD/CDUP, PASV,
//   LIST/NLST, RETR, STOR, DELE, MKD, QUIT, NOOP.
// Файлы ходят через единый FAT-слой (активный источник — SD или USB).
#include <string.h>
#include <stdio.h>
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

// r964: до трёх control-сессий одновременно — второй клиент больше не
// переустанавливает единственный слот (это рвало текущий обмен).
#define FTP_MAX_CTRL  3
// Листинг каталога целиком: FAT_MAX_ENTRIES записей по 112 байт.
#define FTP_LIST_LINE 112
#define FTP_LIST_MAX  (FAT_MAX_ENTRIES * FTP_LIST_LINE)

// ---- Состояние сессии (индекс — номер control-слота) ----
struct ftp_session {
    int      used;          /* соединение установлено (клиент на :21) */
    char     cwd[FAT_NAME_LEN];
    int      data_slot;     /* PASV-слушатель/соединение этой сессии */
    uint16_t pasv_port;
    int      fin_slot;      /* data-слот, ждущий ACK перед FIN */
    uint16_t fin_ticks;     /* страховка: не ждать подтверждения вечно */
    int      reply_226;     /* после FIN отправить 226 владельцу */
};
static struct ftp_session g_ss[NET_TCP_SLOTS];

// ---- Состояние приёма STOR (r832/r845/r964) ----
// FAT-слой один на всю систему: параллельный STOR невозможен, второй
// клиент получает 450, пока первый пишет.
static char     g_stor_name[FAT_NAME_LEN];
static char     g_stor_dir[FAT_NAME_LEN];
static int      g_stor_active;
static int      g_stor_owner = -1;   /* control-слот, начавший STOR */
static int      g_stor_data  = -1;   /* data-слот приёма */
static int      g_stor_failed;
static uint32_t g_stor_len;
static uint32_t g_stor_used;
// r833: буфер-накопитель 8 КБ (запись в FAT пачками, а не на каждый сегмент).
static uint8_t  g_stor_buf[8192];

// ---- Утилиты ----
static struct ftp_session* sess(int ctrl) {
    if (ctrl < 0 || ctrl >= NET_TCP_SLOTS) return 0;
    return &g_ss[ctrl];
}

static void ftp_send_ctrl(int ctrl, const char* s) {
    if (ctrl >= 0 && net_tcp_connected(ctrl))
        net_tcp_send(ctrl, NET_TCP_PSH, (const uint8_t*)s, (uint16_t)strlen(s));
}

static void ftp_reply(int ctrl, int code, const char* msg) {
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
    ftp_send_ctrl(ctrl, buf);
}

// FTP-корень — /roms. Путь считается допустимым, только если это сам
// /roms или что-то внутри него.
static int path_in_root(const char* p) {
    return (strcmp(p, "/roms") == 0) || (strncmp(p, "/roms/", 6) == 0);
}

// Полный путь (абсолютный) — cwd + arg, если arg относительный.
// Нормализуем «/a/b/../c» и повторные слеши, затем принудительно держим
// результат внутри /roms (chroot-поведение).
static void make_path(const char* cwd, const char* arg, char* out, int outsz) {
    char tmp[FAT_NAME_LEN];
    if (arg[0] == '/') {
        strncpy(tmp, arg, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
    } else {
        if (cwd[0]) {
            strncpy(tmp, cwd, sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
            int l = (int)strlen(tmp);
            if (l > 0 && tmp[l-1] != '/') { tmp[l++]='/'; tmp[l]=0; }
            strncat(tmp, arg, sizeof(tmp) - strlen(tmp) - 1);
        } else {
            strncpy(tmp, "/roms/", sizeof(tmp)-1); tmp[sizeof(tmp)-1]=0;
            strncat(tmp, arg, sizeof(tmp) - strlen(tmp) - 1);
        }
    }
    /* Нормализация: сворачиваем "x/../" и "//". */
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

    /* chroot: всё, что оказалось вне /roms, переносим внутрь него. */
    if (!path_in_root(out)) {
        char t[FAT_NAME_LEN];
        const char* tail = (out[0] == '/') ? out : "/";
        snprintf(t, sizeof(t), "/roms%s", tail);
        strncpy(out, t, (size_t)outsz - 1);
        out[outsz - 1] = 0;
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

// ---- data-канал ----
static void ftp_data_send(struct ftp_session* s, const uint8_t* d, int n) {
    if (s->data_slot >= 0 && net_tcp_connected(s->data_slot))
        net_tcp_send(s->data_slot, NET_TCP_PSH, d, (uint16_t)n);
}

// Закрыть отложенный data-слот (FIN + освобождение) и ответить 226.
static void ftp_data_close(int ctrl, struct ftp_session* s) {
    if (s->fin_slot < 0) return;
    net_tcp_close(s->fin_slot);
    s->fin_slot = -1;
    if (s->reply_226) {
        s->reply_226 = 0;
        ftp_reply(ctrl, 226, "transfer complete");
    }
}

// r964: закончили отправку — НЕ закрываем сразу. Ждём, пока клиент
// подтвердит все сегменты (net_tcp_unacked == 0), иначе потерянный кадр
// WiFi уже не повторить, а клиент останется с обрезанным листингом/файлом.
static void ftp_data_finish(int ctrl, struct ftp_session* s) {
    if (s->data_slot < 0) { ftp_reply(ctrl, 226, "transfer complete"); return; }
    s->fin_slot  = s->data_slot;
    s->data_slot = -1;
    s->fin_ticks = 0;
    s->reply_226 = 1;
    if (!net_tcp_connected(s->fin_slot) || net_tcp_unacked(s->fin_slot) == 0)
        ftp_data_close(ctrl, s);
}

// Сбросить data-канал сессии (новый PASV/QUIT/обрыв): подтверждения больше
// не нужны, 226 по старой команде не шлём.
static void ftp_data_drop(struct ftp_session* s) {
    if (s->data_slot >= 0) { net_tcp_close(s->data_slot); s->data_slot = -1; }
    if (s->fin_slot  >= 0) { net_tcp_close(s->fin_slot);  s->fin_slot  = -1; }
    s->reply_226 = 0;
}

// ---- Обработчик data-слота (приём STOR) ----
static int ftp_data_rx(int slot, const uint8_t* data, uint16_t len,
                       const uint8_t* src_ip, const uint8_t* src_mac, void* ctx)
{
    (void)src_ip; (void)src_mac; (void)ctx;
    if (len > 0 && g_stor_active && slot == g_stor_data) {
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
                    /* r845: запись не удалась — приём останавливаем, при
                       финализации файл будет удалён (fat_abort_write). */
                    g_stor_failed = 1;
                    g_stor_used = 0;
                    return 0;
                }
                g_stor_used = 0;
            }
        }
        g_stor_len += len;
    }
    return 0;
}

// ---- Листинг каталога ----
// r964: каталог отдаётся ЦЕЛИКОМ (128 записей × 112 байт в .bss, не на
// стеке). Раньше body[4096] молча обрезал каталоги длиннее ~55 файлов,
// но клиенту всё равно уходило «226 transfer complete».
static void ftp_list(int ctrl, struct ftp_session* s) {
    static char body[FTP_LIST_MAX];
    uint32_t bl = 0;
    fat_entry_t* dirs = fat_scratch();
    const char* d = s->cwd[0] ? s->cwd : "/roms";
    int n = fat_list(d, dirs, FAT_MAX_ENTRIES);
    if (n < 0) { ftp_reply(ctrl, 550, "no such dir"); return; }
    for (int i = 0; i < n; i++) {
        // r818: папка по атрибуту FAT (0x10), а не по size==0 — иначе
        // пустой файл выглядел каталогом. Fallback на size==0 без атрибута.
        int isdir = (dirs[i].attr & 0x10) != 0
                 || (dirs[i].attr == 0 && dirs[i].size == 0);
        char line[FTP_LIST_LINE];
        int ln = 0;
        line[ln++] = isdir ? 'd' : '-';
        const char* perm = "rw-------";
        for (int p = 0; p < 9; p++) line[ln++] = perm[p];
        char sz[12]; int sn = 0; unsigned v = (unsigned)dirs[i].size;
        if (v == 0) sz[sn++] = '0';
        while (v) { sz[sn++] = (char)('0' + v % 10); v /= 10; }
        for (int p = sn; p < 10; p++) line[ln++] = ' ';
        while (sn) line[ln++] = sz[--sn];
        static const char* dt = " Jan  1  2026 ";
        for (int p = 0; dt[p]; p++) line[ln++] = dt[p];
        int nl = (int)strlen(dirs[i].name);
        if (nl > FTP_LIST_LINE - 38) nl = FTP_LIST_LINE - 38;   /* имя */
        for (int p = 0; p < nl; p++) line[ln++] = dirs[i].name[p];
        line[ln++] = '\r'; line[ln++] = '\n';
        if (bl + (uint32_t)ln > sizeof(body)) break;
        memcpy(body + bl, line, (size_t)ln);
        bl += (uint32_t)ln;
    }
    ftp_reply(ctrl, 150, "listing");
    ftp_data_send(s, (const uint8_t*)body, (int)bl);
    ftp_data_finish(ctrl, s);
}

// ---- Обработчик control-слота ----
static int ftp_ctrl_rx(int slot, const uint8_t* data, uint16_t len,
                       const uint8_t* src_ip, const uint8_t* src_mac, void* ctx)
{
    (void)src_ip; (void)src_mac; (void)ctx;
    int ctrl = slot;
    struct ftp_session* s = sess(ctrl);
    if (!s) return 0;

    // r809: сигнал "TCP-connect" (data==NULL, len==0) — слать приглашение 220.
    // Windows-проводник ждёт баннер сразу после установки соединения;
    // без него FTP-подключение виснет. Здесь же сбрасываем состояние:
    // слот мог остаться от прошлого клиента.
    if (!data || len == 0) {
        if (g_stor_active && g_stor_owner == ctrl) {
            g_stor_active = 0;
            fat_abort_write();
            g_stor_used = 0;
        }
        ftp_data_drop(s);
        s->used = 1;
        s->cwd[0] = 0;
        ftp_send_ctrl(ctrl, "220 MultiTool FTP ready\r\n");
        return 0;
    }
    static char line[256];
    static uint16_t ll = 0;
    for (uint16_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            /* r822: срезаем CR. Клиент шлёт "CMD\r\n"; при разборе команды
               БЕЗ аргумента (PASV, PWD, LIST) CR попадал в имя команды ->
               cmd="PASV\r" и strcmp(cmd,"PASV") не совпадал -> 502. */
            if (ll && line[ll-1] == '\r') ll--;
            line[ll] = 0; ll = 0;
            char cmd[8]; int ci = 0;
            while (line[ci] && line[ci] != ' ' && line[ci] != '\r' && ci < 7) { cmd[ci] = line[ci]; ci++; }
            cmd[ci] = 0;
            const char* arg = line[ci] ? line + ci + 1 : "";
            for (int k = 0; cmd[k]; k++) if (cmd[k] >= 'a' && cmd[k] <= 'z') cmd[k] -= 32;
            while (*arg == ' ') arg++;
            /* r822: пустая строка (клиент иногда шлёт голый CRLF) — не
               ошибка, просто игнорируем; раньше отвечали 502. */
            if (!cmd[0]) continue;

            FTPLOG("  ftp< %s %s\r\n", cmd, arg);

            if (strcmp(cmd,"USER")==0 || strcmp(cmd,"PASS")==0) ftp_reply(ctrl,230,"logged in");
            else if (strcmp(cmd,"SYST")==0) ftp_reply(ctrl,215,"UNIX Type: L8");
            else if (strcmp(cmd,"TYPE")==0) ftp_reply(ctrl,200,"type set");
            else if (strcmp(cmd,"PWD")==0 || strcmp(cmd,"XPWD")==0) {
                char pr[180];
                snprintf(pr, sizeof(pr), "\"%s\"", s->cwd[0] ? s->cwd : "/roms");
                ftp_reply(ctrl,257, pr);
            }
            else if (strcmp(cmd,"CWD")==0) {
                char p[FAT_NAME_LEN]; make_path(s->cwd, arg, p, sizeof(p));
                // нормализация: убрать хвостовой '/'
                int l=(int)strlen(p);
                while (l>1 && p[l-1]=='/') p[--l]=0;
                if (strcmp(p, "/")==0) s->cwd[0]=0;
                else { strncpy(s->cwd, p, sizeof(s->cwd)-1); s->cwd[sizeof(s->cwd)-1]=0; }
                ftp_reply(ctrl,250,"cwd ok");
            }
            else if (strcmp(cmd,"CDUP")==0) {
                /* r845: реально поднимаемся на уровень выше — с /roms/nes
                   в /roms, с /roms в корень (для каждого клиента свой cwd). */
                if (s->cwd[0]) {
                    char p[FAT_NAME_LEN];
                    strncpy(p, s->cwd, sizeof(p)-1); p[sizeof(p)-1]=0;
                    int l=(int)strlen(p);
                    while (l>1 && p[l-1]=='/') p[--l]=0;
                    char* slash = strrchr(p, '/');
                    if (slash) {
                        if (slash == p) s->cwd[0]=0;
                        else { *slash=0; strncpy(s->cwd, p, sizeof(s->cwd)-1); s->cwd[sizeof(s->cwd)-1]=0; }
                    } else s->cwd[0]=0;
                }
                ftp_reply(ctrl,250,"cdup ok");
            }
            else if (strcmp(cmd,"PASV")==0) {
                /* Сначала закрыть прошлый data-канал сессии, затем открыть
                   слушатель на своём порту (зависит от control-слота, чтобы
                   у параллельных клиентов порты не совпали). Если слушатель
                   не открылся — 425, а не 227 на несуществующий порт. */
                ftp_data_drop(s);
                s->pasv_port = (uint16_t)(21000u + ((unsigned)(ctrl + 1) * 37u) % 1000u);
                s->data_slot = net_tcp_listen(s->pasv_port, ftp_data_rx, 0);
                if (s->data_slot < 0) {
                    ftp_reply(ctrl, 425, "cannot open data connection");
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
                    unsigned v=s->pasv_port>>8; char t[8]; int tn=0; char ts[6]; int ti=0;
                    if (v==0) ts[ti++]='0';
                    while (v) { ts[ti++]=(char)('0'+v%10); v/=10; }
                    while (ti) t[tn++]=ts[--ti];
                    for (int p=0;p<tn;p++) buf[n++]=t[p];
                    buf[n++]=',';
                    tn=0; ti=0; v=s->pasv_port&0xFF;
                    if (v==0) ts[ti++]='0';
                    while (v) { ts[ti++]=(char)('0'+v%10); v/=10; }
                    while (ti) t[tn++]=ts[--ti];
                    for (int p=0;p<tn;p++) buf[n++]=t[p];
                    buf[n++]=')'; buf[n]=0;
                    ftp_reply(ctrl, 227, buf);
                }
            }
            else if (strcmp(cmd,"LIST")==0 || strcmp(cmd,"NLST")==0) {
                ftp_list(ctrl, s);
            }
            else if (strcmp(cmd,"RETR")==0) {
                char p[FAT_NAME_LEN]; make_path(s->cwd, arg, p, sizeof(p));
                char dir[FAT_NAME_LEN], nm[FAT_NAME_LEN];
                fat_entry_t f;
                if (!path_split(p, dir, sizeof(dir), nm, sizeof(nm)) || !nm[0] ||
                    !fat_find(dir, nm, &f)) { ftp_reply(ctrl, 550, "no such file"); }
                else if (s->data_slot < 0 || !net_tcp_connected(s->data_slot)) {
                    ftp_reply(ctrl, 425, "no data connection");
                }
                else {
                    ftp_reply(ctrl, 150, "opening binary");
                    static uint8_t rdbuf[512];
                    uint32_t off = 0;
                    int fail = 0;
                    while (off < f.size) {
                        int got = fat_read_file(&f, off, rdbuf, sizeof(rdbuf));
                        if (got <= 0) { fail = 1; break; }
                        for (int o = 0; o < got; o += 512) {
                            int chunk = got - o;
                            if (chunk > 512) chunk = 512;
                            ftp_data_send(s, &rdbuf[o], chunk);
                        }
                        off += (uint32_t)got;
                    }
                    if (fail) {
                        /* r971: чтение FAT дало сбой/недопрочитано — клиент
                           НЕ должен получить «transfer complete» после
                           битых данных. Закрываем data-канал и 451. */
                        ftp_data_drop(s);
                        ftp_reply(ctrl, 451, "read error");
                    } else {
                        /* r964: FIN и 226 — из poll после ACK всех данных. */
                        ftp_data_finish(ctrl, s);
                    }
                }
            }
            else if (strcmp(cmd,"STOR")==0) {
                if (g_stor_active) {
                    ftp_reply(ctrl, 450, "another transfer in progress");
                } else if (s->data_slot < 0 || !net_tcp_connected(s->data_slot)) {
                    ftp_reply(ctrl, 425, "no data connection");
                } else {
                    char p[FAT_NAME_LEN]; make_path(s->cwd, arg, p, sizeof(p));
                    char dir[FAT_NAME_LEN], nm[FAT_NAME_LEN];
                    path_split(p, dir, sizeof(dir), nm, sizeof(nm));
                    if (!nm[0]) {
                        ftp_reply(ctrl, 550, "bad file name");
                    /* r832: открываем потоковую запись ДО 150 — если каталог
                       или имя не годятся, клиент сразу получит 550. */
                    } else if (fat_write_begin(dir[0] ? dir : "/roms", nm) < 0) {
                        ftp_reply(ctrl, 550, "cannot create file");
                    } else {
                        g_stor_len = 0;
                        g_stor_used = 0;
                        g_stor_failed = 0;
                        g_stor_active = 1;
                        g_stor_owner = ctrl;
                        g_stor_data = s->data_slot;
                        strncpy(g_stor_name, nm, sizeof(g_stor_name)-1);
                        g_stor_name[sizeof(g_stor_name)-1] = 0;
                        strncpy(g_stor_dir, dir, sizeof(g_stor_dir)-1);
                        g_stor_dir[sizeof(g_stor_dir)-1] = 0;
                        ftp_reply(ctrl, 150, "ok to send data");
                    }
                }
            }
            else if (strcmp(cmd,"DELE")==0) {
                char p[FAT_NAME_LEN]; make_path(s->cwd, arg, p, sizeof(p));
                char dir[FAT_NAME_LEN], nm[FAT_NAME_LEN];
                if (!path_split(p, dir, sizeof(dir), nm, sizeof(nm)) || !nm[0] ||
                    fat_delete_file(dir, nm) != 0) ftp_reply(ctrl, 550, "delete fail");
                else ftp_reply(ctrl, 250,"deleted");
            }
            else if (strcmp(cmd,"MKD")==0) {
                char p[FAT_NAME_LEN]; make_path(s->cwd, arg, p, sizeof(p));
                // fat_mkdir(parent, name) — вычленяем последний компонент
                char parent[FAT_NAME_LEN]; strncpy(parent, p, sizeof(parent)-1); parent[sizeof(parent)-1]=0;
                char* slash = strrchr(parent, '/');
                if (slash && slash != parent) { *slash=0; const char* nm = slash+1;
                    if (fat_mkdir(parent, nm)==0) { char r[180]; r[0]='"'; r[1]=0; strcat(r,p); strcat(r,"\""); ftp_reply(ctrl,257, r); }
                    else ftp_reply(ctrl,550, "mkdir fail");
                } else ftp_reply(ctrl,550, "mkdir fail");
            }
            else if (strcmp(cmd,"RMD")==0) { ftp_reply(ctrl,502, "rmd stub"); }
            else if (strcmp(cmd,"QUIT")==0) {
                ftp_reply(ctrl,221,"bye");
                if (g_stor_active && g_stor_owner == ctrl) {
                    g_stor_active = 0;
                    fat_abort_write();
                    g_stor_used = 0;
                }
                ftp_data_drop(s);
                s->used = 0;
                /* r964: закрыть именно эту сессию, не трогая остальные.
                   Слот освобождается — слушатели :21 вернёт poll. */
                net_tcp_close(ctrl);
            }
            else if (strcmp(cmd,"NOOP")==0) { ftp_reply(ctrl,200,"ok"); }
            else ftp_reply(ctrl,502, "command not implemented");
        } else if (ll < sizeof(line)-1) line[ll++] = c;
        else ll = 0;
    }
    return 0;
}

// ---- Публичный API ----
// r964: держим FTP_MAX_CTRL слушателей на :21 (по одному на возможную
// сессию), чтобы второе подключение занимало свободный слот, а не
// переустанавливало уже работающий.
static void ftp_ensure_listeners(void) {
    int have = net_tcp_listener_count(21);
    for (int i = have; i < FTP_MAX_CTRL; i++)
        if (net_tcp_listen(21, ftp_ctrl_rx, 0) < 0) break;
}

int ftp_server_start(void) {
    for (int i = 0; i < NET_TCP_SLOTS; i++) {
        memset(&g_ss[i], 0, sizeof(g_ss[i]));
        g_ss[i].data_slot = -1;
        g_ss[i].fin_slot  = -1;
    }
    g_stor_active = 0;
    g_stor_owner  = -1;
    g_stor_data   = -1;
    ftp_ensure_listeners();
    int n = net_tcp_listener_count(21);
    {
        extern int uart0_printf(const char* fmt, ...);
        uart0_printf("ftp: server on :21 x%d (PASV from :21037)\r\n", n);
    }
    return n > 0 ? 0 : -1;
}

void ftp_server_poll(void) {
    net_tcp_poll();

    for (int i = 0; i < NET_TCP_SLOTS; i++) {
        struct ftp_session* s = &g_ss[i];
        /* сессия закрыта клиентом (FIN/RST) — освободить её ресурсы */
        if (s->used && !net_tcp_connected(i)) {
            if (g_stor_active && g_stor_owner == i) {
                g_stor_active = 0;
                fat_abort_write();
                g_stor_used = 0;
                g_stor_data = -1;
            }
            ftp_data_drop(s);
            s->used = 0;
        }
        /* r964: данные отправлены — FIN и 226 только после ACK клиента */
        if (s->fin_slot >= 0) {
            if (!net_tcp_connected(s->fin_slot) ||
                net_tcp_unacked(s->fin_slot) == 0 ||
                ++s->fin_ticks > 400)
                ftp_data_close(i, s);
        }
    }

    /* Завершение STOR: клиент закрыл data-слот (FIN → net_tcp снял active). */
    if (g_stor_active) {
        if (g_stor_data < 0 || !net_tcp_connected(g_stor_data)) {
            int ctrl = g_stor_owner;
            int data = g_stor_data;
            g_stor_active = 0;
            g_stor_data = -1;
            int rc = -1;
            {
                extern int uart0_printf(const char* fmt, ...);
                /* r965: логи только по ошибке (успешная запись молчит). */
                if (g_stor_failed) {
                    /* r845: была ошибка записи — удаляем частичный файл. */
                    uart0_printf("  ftp: STOR abort (write error) len=%u %s/%s\r\n",
                                 g_stor_len, g_stor_dir[0] ? g_stor_dir : "/roms",
                                 g_stor_name);
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
                    if (rc < 0)
                        uart0_printf("  ftp: STOR write rc=%d len=%u %s/%s\r\n",
                                     rc, g_stor_len,
                                     g_stor_dir[0] ? g_stor_dir : "/roms",
                                     g_stor_name);
                }
            }
            if (ctrl >= 0) {
                if (rc >= 0) ftp_reply(ctrl, 226, "transfer complete");
                else         ftp_reply(ctrl, 426, "write failed");
            }
            if (data >= 0) net_tcp_close(data);
            struct ftp_session* s = sess(ctrl);
            if (s && s->data_slot == data) s->data_slot = -1;
        }
    }

    /* r964: восстановить слушателей :21, если сессии их израсходовали */
    ftp_ensure_listeners();
}

int ftp_server_active(void) {
    for (int i = 0; i < NET_TCP_SLOTS; i++)
        if (g_ss[i].used && net_tcp_connected(i)) return 1;
    return 0;
}
