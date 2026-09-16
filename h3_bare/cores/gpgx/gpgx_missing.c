// gpgx_missing.c — недостающие символы для линковки Genesis Plus GX.
// rf* — файловые стабы (ROM подаём напрямую в картридж, файлов нет).
// BIOS-пути — пустые (BIOS на SD не грузим).
#include <string.h>
#include "types.h"
#include "osd.h"
#include "ntsc/md_ntsc.h"
#include "ntsc/sms_ntsc.h"

// ---- ntsc-фильтры: отключены (указатели NULL), ядро делает прямые кадры ----
md_ntsc_t *md_ntsc = NULL;
sms_ntsc_t *sms_ntsc = NULL;

// ---- crc32 (для SRAM) ----
unsigned long crc32(unsigned long crc, const unsigned char *buf, unsigned int len) {
    unsigned long c = crc ^ 0xFFFFFFFFUL;
    for (unsigned int i = 0; i < len; i++) {
        c ^= buf[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320UL & (0UL - (c & 1)));
    }
    return c ^ 0xFFFFFFFFUL;
}

// ---- osd/cheats (не используем чит-энджин) ----
void osd_input_update(void) {}
void ROMCheatUpdate(void) {}

// RFILE-стабы (файлов нет — ROM в памяти)
RFILE *rfopen(const char *path, const char *mode) { (void)path; (void)mode; return NULL; }
int rfclose(RFILE *stream) { (void)stream; return 0; }
int64_t rfread(void *buf, size_t size, size_t count, RFILE *stream) { (void)buf; (void)size; (void)count; (void)stream; return 0; }
int64_t rfseek(RFILE *stream, int64_t offset, int whence) { (void)stream; (void)offset; (void)whence; return 0; }
int64_t rftell(RFILE *stream) { (void)stream; return 0; }
int rfgets(char *buf, size_t size, RFILE *stream) { (void)buf; (void)size; (void)stream; return 0; }
int64_t rfwrite(const void *buf, size_t size, size_t count, RFILE *stream) { (void)buf; (void)size; (void)count; (void)stream; return 0; }

// Пути BIOS (не используются — ROM грузится напрямую)
char GG_ROM[256], AR_ROM[256], SK_ROM[256], SK_UPMEM[256];
char GG_BIOS[256], MD_BIOS[256];
char CD_BIOS_EU[256], CD_BIOS_US[256], CD_BIOS_JP[256];
char MS_BIOS_US[256], MS_BIOS_EU[256], MS_BIOS_JP[256];

// load_archive: заглушка (мы не читаем архивов; ROM в памяти)
int load_archive(char *filename, unsigned char *buffer, int maxsize, char *extension) {
    (void)filename; (void)buffer; (void)maxsize;
    if (extension) extension[0] = 0;
    return 0;
}
// стаб для ARM EABI (нужен newlib setjmp/longjmp)
void __aeabi_unwind_cpp_pr0(void) {}
