// frodo_stubs.c — стабы POSIX/окружения для ядра Frodo (Commude 64, bare-metal H3).
//
// r1284 (встройка Frodo вместо VICE): записи, которых нет в bare-metal
// newlib/fs_layer/uae4all_stubs, но на которые ссылается ядро:
// - chdir: 1541fs.cpp переходит в каталог диска перед fopen (AppDirPath "/");
//   на H3 каталогов нет — fopen ходит через файловый слой по полному пути,
//   поэтому chdir всегда успешен (диск 1541 через ImageDrive/FSDrive).
// - sigaction: SAM.cpp (init_abort/exit_abort) регистрирует обработчик
//   SIGINT на время печати SAM-текста; bare-metal сигналов нет, стаб no-op
//   (как signal() в fs_layer.c).
#include <stddef.h>

int chdir(const char* path)
{
    (void)path;
    return 0;   // каталогов нет — успех; fopen идёт через файловый слой
}

struct sigaction;

int sigaction(int sig, const struct sigaction* act, struct sigaction* old)
{
    (void)sig; (void)act; (void)old;
    return 0;   // bare-metal сигналов нет — no-op (как signal() в fs_layer)
}