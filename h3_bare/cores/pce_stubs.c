// pce_stubs.c — заглушки CD/libretro-common для HuCard-only сборки PC Engine.
//
// Beetle PCE Fast собран без CD (CDAccess/libchdr/zlib/tremor/vfs/streams).
// libretro.c всё равно ССЫЛАЕТСЯ на CD-функции (ветки PCE_IsCD) и на несколько
// хелперов libretro-common — линкеру их надо разрешить. Для HuCard они не
// вызываются, но даём честные no-op/безопасные реализации, чтобы поведение
// было предсказуемым, если когда-нибудь дёрнутся.
//
// ВАЖНО: заголовки намеренно НЕ включаем — определения совпадают по имени,
// тип на линковке не проверяется; так избегаем конфликтов прототипов.

#include <stddef.h>
#include <stdint.h>

// ---- настройки CD (libretro.c присваивает; определены обычно в settings) ----
int setting_pce_fast_cdignoreerrors = 0;

// ---- libretro-common helpers ----
uint32_t encoding_crc32(uint32_t crc, const void* data, size_t len)
{
    // реальный CRC32 (IEEE) — Load() считает его для OrderOfGriffonFix
    const uint8_t* p = (const uint8_t*)data;
    uint32_t c = crc ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
    }
    return c ^ 0xFFFFFFFFu;
}

size_t strlcpy_retro__(char* dst, const char* src, size_t size)
{
    size_t srclen = 0;
    if (src) while (src[srclen]) srclen++;
    if (size) {
        size_t n = (srclen < size - 1) ? srclen : size - 1;
        for (size_t i = 0; i < n; i++) dst[i] = src[i];
        dst[n] = 0;
    }
    return srclen;
}

void string_trim_whitespace_right(char* s)
{
    if (!s) return;
    size_t n = 0;
    while (s[n]) n++;
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') s[--n] = 0;
        else break;
    }
}

char* filestream_gets(void* stream, char* s, int len) { (void)stream; (void)s; (void)len; return 0; }
int   filestream_read_file(const char* p, void** b, long long* l) { (void)p; if (b) *b = 0; if (l) *l = 0; return 0; }
int   filestream_vfs_init(const void* info) { (void)info; return 0; }

// ---- CD (не поддерживается) ----
int  CDIF_Open(void)  { return 0; }
void CDIF_Close(void) { }
int  CDUtility_Init(void) { return 0; }

int  PCECD_Init(void)      { return 0; }
void PCECD_Close(void)     { }
void PCECD_Power(void)     { }
int  PCECD_Read(void)      { return 0; }
void PCECD_Write(void)     { }
void PCECD_Run(void)       { }
void PCECD_ResetTS(void)   { }
int  PCECD_SetSettings(void) { return 0; }
int  PCECD_IsBRAMEnabled(void) { return 0; }
int  PCECD_StateAction(void) { return 1; }
void PCECD_Drive_SetDisc(void) { }
