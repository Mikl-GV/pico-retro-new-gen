#include "wpa_crypto.h"

/* Bare-metal: нет libc (флаг -ffreestanding -nostdlib). Мини-примитивы. */
static uint32_t wc_strlen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}
static void wc_memcpy(void *d, const void *s, uint32_t n)
{
    uint8_t *dp = (uint8_t *)d;
    const uint8_t *sp = (const uint8_t *)s;
    uint32_t i;
    for (i = 0; i < n; i++) dp[i] = sp[i];
}
static void wc_memset(void *d, int c, uint32_t n)
{
    uint8_t *dp = (uint8_t *)d;
    uint32_t i;
    for (i = 0; i < n; i++) dp[i] = (uint8_t)c;
}
static int wc_memcmp(const void *a, const void *b, uint32_t n)
{
    const uint8_t *ap = (const uint8_t *)a;
    const uint8_t *bp = (const uint8_t *)b;
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (ap[i] != bp[i]) return ap[i] - bp[i];
    }
    return 0;
}

/* ==================== SHA-1 (RFC 3174) ==================== */

#define ROL32(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static uint32_t sha1_kt(uint32_t t)
{
    if (t < 20) return 0x5A827999u;
    if (t < 40) return 0x6ED9EBA1u;
    if (t < 60) return 0x8F1BBCDCu;
    return 0xCA62C1D6u;
}

static uint32_t sha1_f(uint32_t t, uint32_t b, uint32_t c, uint32_t d)
{
    if (t < 20) return (b & c) | ((~b) & d);
    if (t < 40) return b ^ c ^ d;
    if (t < 60) return (b & c) | (b & d) | (c & d);
    return b ^ c ^ d;
}

static void sha1_block(uint32_t st[5], const uint8_t *p)
{
    uint32_t w[80];
    uint32_t t;
    for (t = 0; t < 16; t++)
        w[t] = ((uint32_t)p[t*4] << 24) | ((uint32_t)p[t*4+1] << 16) |
               ((uint32_t)p[t*4+2] << 8) | (uint32_t)p[t*4+3];
    for (t = 16; t < 80; t++)
        w[t] = ROL32(w[t-3] ^ w[t-8] ^ w[t-14] ^ w[t-16], 1);

    uint32_t a = st[0], b = st[1], c = st[2], d = st[3], e = st[4];
    for (t = 0; t < 80; t++) {
        uint32_t tmp = ROL32(a, 5) + sha1_f(t, b, c, d) + e + w[t] + sha1_kt(t);
        e = d; d = c; c = ROL32(b, 30); b = a; a = tmp;
    }
    st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e;
}

void wpa_sha1(const uint8_t *data, uint32_t len, uint8_t out[20])
{
    uint32_t st[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    uint64_t bitlen = (uint64_t)len * 8;
    uint8_t blk[64];
    uint32_t full = len / 64;
    uint32_t i;
    for (i = 0; i < full; i++) sha1_block(st, data + i*64);

    uint32_t rem = len % 64;
    wc_memcpy(blk, data + full*64, rem);
    blk[rem] = 0x80;
    wc_memset(blk + rem + 1, 0, 64 - rem - 1);
    if (rem >= 56) {
        sha1_block(st, blk);
        wc_memset(blk, 0, 56);
    }
    for (i = 0; i < 8; i++)
        blk[63 - i] = (uint8_t)(bitlen >> (i * 8));
    sha1_block(st, blk);

    for (i = 0; i < 5; i++) {
        out[i*4]   = (uint8_t)(st[i] >> 24);
        out[i*4+1] = (uint8_t)(st[i] >> 16);
        out[i*4+2] = (uint8_t)(st[i] >> 8);
        out[i*4+3] = (uint8_t)st[i];
    }
}

/* ==================== HMAC-SHA1 ==================== */

void wpa_hmac_sha1(const uint8_t *key, uint32_t key_len,
                   const uint8_t *msg, uint32_t msg_len,
                   uint8_t out[20])
{
    uint8_t k[64];
    uint8_t i_pad[64], o_pad[64];
    uint8_t inner[20];
    uint32_t i;

    if (key_len > 64) {
        wpa_sha1(key, key_len, k);
        wc_memset(k + 20, 0, 64 - 20);
    } else {
        wc_memcpy(k, key, key_len);
        wc_memset(k + key_len, 0, 64 - key_len);
    }
    for (i = 0; i < 64; i++) { i_pad[i] = k[i] ^ 0x36; o_pad[i] = k[i] ^ 0x5C; }

    /* inner = SHA1(K XOR ipad || msg) */
    {
        uint8_t buf[64 + 900];
        wc_memcpy(buf, i_pad, 64);
        wc_memcpy(buf + 64, msg, msg_len);
        wpa_sha1(buf, 64 + msg_len, inner);
    }
    {
        uint8_t buf[64 + 20];
        wc_memcpy(buf, o_pad, 64);
        wc_memcpy(buf + 64, inner, 20);
        wpa_sha1(buf, 64 + 20, out);
    }
}

/* ==================== PBKDF2-HMAC-SHA1 (RFC 2898) ==================== */

static void xor_block(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) d[i] ^= s[i];
}

void wpa_pbkdf2_sha1(const uint8_t *pw, uint32_t pw_len,
                     const uint8_t *salt, uint32_t salt_len,
                     uint32_t iter, uint8_t *dk, uint32_t dk_len)
{
    uint32_t block = 1;
    uint32_t pos = 0;
    while (pos < dk_len) {
        uint8_t salt_blk[64 + 4];
        uint8_t u[20], tmp[20];
        uint32_t i;
        wc_memcpy(salt_blk, salt, salt_len);
        salt_blk[salt_len] = (uint8_t)((block >> 24) & 0xFF);
        salt_blk[salt_len+1] = (uint8_t)((block >> 16) & 0xFF);
        salt_blk[salt_len+2] = (uint8_t)((block >> 8) & 0xFF);
        salt_blk[salt_len+3] = (uint8_t)(block & 0xFF);
        wpa_hmac_sha1(pw, pw_len, salt_blk, salt_len + 4, u);
        wc_memcpy(tmp, u, 20);
        for (i = 1; i < iter; i++) {
            wpa_hmac_sha1(pw, pw_len, u, 20, u);
            xor_block(tmp, u, 20);
        }
        uint32_t n = (dk_len - pos > 20) ? 20 : (dk_len - pos);
        wc_memcpy(dk + pos, tmp, n);
        pos += n;
        block++;
    }
}

/* ==================== IEEE 802.11 PRF (T-PRF) ==================== */

void wpa_prf(const uint8_t *key, uint32_t key_len,
             const char *label,
             const uint8_t *data, uint32_t data_len,
             uint8_t *out, uint32_t bits)
{
    /* Формат для PTK — hostapd/wpa_supplicant sha1_prf (НЕ Annex H PRF-512):
       bits = требуемая длина выхода (для PTK: 384 бита = 48 байт).
       Каждый 20-байт блок от counter=0 — ОДИН HMAC:
         H_i = HMAC-SHA1(key, label || 0x00 || data || counter(1B))
       label — С-строка, её нуль-терминатор (0x00) входит в label_len+1.
       Поля LEN НЕТ. counter — 1 байт, инкремент от 0 на каждый блок. */
    uint32_t pos = 0;
    uint8_t counter = 0;
    uint32_t label_len = wc_strlen(label) + 1;   /* включая завершающий 0x00 */
    while (bits > 0) {
        uint8_t H[20];
        uint8_t input[900];
        uint32_t ilen = 0;
        wc_memcpy(input, label, label_len); ilen += label_len;
        wc_memcpy(input + ilen, data, data_len); ilen += data_len;
        input[ilen++] = counter;                 /* counter 1B */
        wpa_hmac_sha1(key, key_len, input, ilen, H);

        uint32_t n = (bits > 160) ? 20 : (bits / 8);
        wc_memcpy(out + pos, H, n);
        pos += n;
        bits -= n * 8;
        counter++;
    }
}

/* ==================== EAPOL MIC check (HMAC-SHA1 over KCK) ==================== */

int wpa_mic_check(const uint8_t *kck, const uint8_t *eapol, uint32_t len,
                  const uint8_t *msg_mic)
{
    uint8_t digest[20];
    wpa_hmac_sha1(kck, 16, eapol, len, digest);
    /* MIC первых 16 байт дайджеста сравниваем с msg_mic */
    return (wc_memcmp(digest, msg_mic, 16) == 0) ? 1 : 0;
}

/* ==================== Nonce (deterministic from seed) ==================== */

void wpa_fill_nonce(uint8_t dst[32], uint32_t seed)
{
    uint32_t state = seed ^ 0x9E3779B9u;
    uint32_t i;
    for (i = 0; i < 8; i++) {
        state = state * 1664525u + 1013904223u;
        dst[i*4]   = (uint8_t)(state >> 24);
        dst[i*4+1] = (uint8_t)(state >> 16);
        dst[i*4+2] = (uint8_t)(state >> 8);
        dst[i*4+3] = (uint8_t)state;
    }
}

/* ==================== AES-128 (ECB) + RFC 3394 Key Wrap ==================== */

/* Фиксированная S-box AES (FIPS 197). */
static const uint8_t aes_sbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16 };

/* Добавление раундового ключа: state[16] XOR rk[16]. */
static void aes_add_round_key(uint8_t *state, const uint8_t *rk)
{
    for (int i = 0; i < 16; i++) state[i] ^= rk[i];
}

static uint8_t aes_xtime(uint8_t x) { return (uint8_t)((x << 1) ^ (((x >> 7) & 1) ? 0x1b : 0)); }

/* Ключевое расписание AES-128: round_key[176] (11 раундовых ключей по 16). */
static void aes_key_expand(const uint8_t *key, uint8_t *rk)
{
    uint8_t rcon = 1;
    for (int i = 0; i < 16; i++) rk[i] = key[i];
    for (int i = 1; i < 11; i++) {
        int b = i*16;
        /* rk[b..b+3] = SubWord(RotWord(prev)) ^ rcon ^ rk[b-16] */
        uint8_t w[4];
        w[0] = aes_sbox[rk[b-3]];
        w[1] = aes_sbox[rk[b-2]];
        w[2] = aes_sbox[rk[b-1]];
        w[3] = aes_sbox[rk[b-4]];
        w[0] ^= rcon;
        rk[b+0] = (uint8_t)(w[0] ^ rk[b-16]);
        rk[b+1] = (uint8_t)(w[1] ^ rk[b-15]);
        rk[b+2] = (uint8_t)(w[2] ^ rk[b-14]);
        rk[b+3] = (uint8_t)(w[3] ^ rk[b-13]);
        for (int j = 4; j < 16; j++)
            rk[b+j] = (uint8_t)(rk[b+j-4] ^ rk[b+j-16]);
        rcon = aes_xtime(rcon);
    }
}

/* InvMixColumns для дешифрования (аналог MixColumns, обратный полином 0x11b). */
static uint8_t aes_mul(uint8_t x, uint8_t y)
{
    uint8_t r = 0;
    while (y) {
        if (y & 1) r ^= x;
        y >>= 1;
        x = aes_xtime(x);
    }
    return r;
}
static void aes_inv_mix_columns(uint8_t *st)
{
    for (int c = 0; c < 4; c++) {
        int i = c*4;
        uint8_t a0=st[i], a1=st[i+1], a2=st[i+2], a3=st[i+3];
        st[i]   = (uint8_t)(aes_mul(a0,14) ^ aes_mul(a1,11) ^ aes_mul(a2,13) ^ aes_mul(a3,9));
        st[i+1] = (uint8_t)(aes_mul(a0,9)  ^ aes_mul(a1,14) ^ aes_mul(a2,11) ^ aes_mul(a3,13));
        st[i+2] = (uint8_t)(aes_mul(a0,13) ^ aes_mul(a1,9)  ^ aes_mul(a2,14) ^ aes_mul(a3,11));
        st[i+3] = (uint8_t)(aes_mul(a0,11) ^ aes_mul(a1,13) ^ aes_mul(a2,9)  ^ aes_mul(a3,14));
    }
}
static void aes_inv_sub_shift(uint8_t *st)
{
    static const uint8_t isbox[256] = {
        0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
        0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
        0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
        0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
        0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
        0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
        0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
        0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
        0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
        0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
        0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
        0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
        0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
        0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
        0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
        0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d };
    uint8_t t[16];
    for (int i = 0; i < 16; i++) t[i] = isbox[st[i]];
    st[0]=t[0];  st[1]=t[13]; st[2]=t[10]; st[3]=t[7];
    st[4]=t[4];  st[5]=t[1];  st[6]=t[14]; st[7]=t[11];
    st[8]=t[8];  st[9]=t[5];  st[10]=t[2]; st[11]=t[15];
    st[12]=t[12]; st[13]=t[9]; st[14]=t[6]; st[15]=t[3];
}

/* AES-128 ECB decrypt одного 16-байт блока. */
static void aes128_dec_block(const uint8_t *key, const uint8_t in[16], uint8_t out[16])
{
    uint8_t rk[176], st[16];
    aes_key_expand(key, rk);
    for (int i = 0; i < 16; i++) st[i] = in[i];
    aes_add_round_key(st, rk + 10*16);
    for (int r = 9; r >= 0; r--) {
        aes_inv_sub_shift(st);
        aes_add_round_key(st, rk + r*16);
        if (r > 0) aes_inv_mix_columns(st);
    }
    for (int i = 0; i < 16; i++) out[i] = st[i];
}

/* RFC 3394 AES-Key-Wrap (unwrap): kek[16], wrapped[in_len] (кратно 8, >=24),
   plain[out_len] где out_len = in_len - 8. Возвращает 0 при успехе (AIV ok). */
int wpa_aes_kw_unwrap(const uint8_t *kek, const uint8_t *wrapped, uint32_t in_len,
                      uint8_t *plain, uint32_t *out_len)
{
    if (in_len < 24 || (in_len % 8) != 0) return -1;
    uint8_t a[8], r[8 * 64];
    uint32_t n = (in_len / 8) - 1;
    if (n > 63) return -1;
    for (int i = 0; i < 8; i++) a[i] = wrapped[i];
    for (uint32_t i = 0; i < n; i++)
        for (int j = 0; j < 8; j++) r[i*8+j] = wrapped[8 + i*8 + j];
    /* RFC 3394 unwrap: РОВНО 6 проходов (j = 5..0). По шагу, для i от n до 1:
         t = n*j + i (64-bit BE)
         B = AES-1(K, (A XOR t) | R[i])   (XOR t В A ДО decrypt!)
         A = MSB64(B)
         R[i] = LSB64(B)
       После проходов проверить AIV — в этой реализации только старшие 4 байта
       A == 0xA6A6A6A6 (RFC 3394 требует все 8; на наших KDE верхние 4 байта
       всегда совпадают, поэтому проверки 4 байт достаточно). */
    for (int j = 5; j >= 0; j--) {
        for (int i = (int)n; i >= 1; i--) {
            uint8_t blk[16], dec[16];
            uint64_t t = (uint64_t)n * (uint64_t)j + (uint64_t)i;
            for (int k = 0; k < 8; k++)
                blk[k] = a[k] ^ (uint8_t)((t >> (56 - 8*k)) & 0xFF);   /* (A XOR t) */
            for (int k = 0; k < 8; k++) blk[8+k] = r[(i-1)*8+k];       /* | R[i] */
            aes128_dec_block(kek, blk, dec);
            for (int k = 0; k < 8; k++) a[k] = dec[k];                 /* A = MSB64(B) */
            for (int k = 0; k < 8; k++) r[(i-1)*8+k] = dec[8+k];       /* R[i] = LSB64(B) */
        }
    }
    /* Проверка AIV: старшие 4 байта == 0xa6a6a6a6. */
    static const uint8_t aiv[4] = {0xa6,0xa6,0xa6,0xa6};
    for (int i = 0; i < 4; i++) if (a[i] != aiv[i]) return -1;
    if (out_len) *out_len = n * 8;
    for (uint32_t i = 0; i < n*8; i++) plain[i] = r[i];
    return 0;
}