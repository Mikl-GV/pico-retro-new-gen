#ifndef WPA_CRYPTO_H
#define WPA_CRYPTO_H

#include <stdint.h>

/* Компактный крипто-набор для WPA2-PSK host-supplicant (bare-metal).
   Именно его считает хост; CCMP-шифрование данных делает чип через CAM.
   - SHA-1 (общий хеш)
   - HMAC-SHA1
   - PBKDF2-HMAC-SHA1 (вывод PMK из пароля, 4096 итераций)
   - IEEE 802.11 T-PRF (PRF-512 для генерации PTK/KCK/KEK/TK)
   - Проверка EAPOL MIC (HMAC-SHA1 по KCK) */

/* SHA-1: output 20 байт в *out. input может быть произвольной длины. */
void wpa_sha1(const uint8_t *data, uint32_t len, uint8_t out[20]);

/* HMAC-SHA1: key/c key glen, msg/m mlen, output 20 байт. */
void wpa_hmac_sha1(const uint8_t *key, uint32_t key_len,
                   const uint8_t *msg, uint32_t msg_len,
                   uint8_t out[20]);

/* PBKDF2-HMAC-SHA1 (RFC 2898). password pw/plen, salt sl/slen, iter,
   выводит dk_len байт. Используется для PMK (dk_len=32, iter=4096). */
void wpa_pbkdf2_sha1(const uint8_t *pw, uint32_t pw_len,
                     const uint8_t *salt, uint32_t salt_len,
                     uint32_t iter, uint8_t *dk, uint32_t dk_len);

/* IEEE 802.11 PRF (T-PRF): label + data, bits/8 байт в *out.
   Используется для PTK (KCK|KEK|TK = 48 байт) и GMK. */
void wpa_prf(const uint8_t *key, uint32_t key_len,
             const char *label,
             const uint8_t *data, uint32_t data_len,
             uint8_t *out, uint32_t bits);

/* Проверка MIC EAPOL-кадра: HMAC-SHA1(KCK) по кадру. Реальный MIC в кадре
   должен быть обнулён перед подсчётом. Возвращает 1 при совпадении. */
int wpa_mic_check(const uint8_t *kck, const uint8_t *eapol, uint32_t len,
                  const uint8_t *msg_mic);

/* Минимальный RNG для nonce (deterministic: LCG от времени/cчётчика).
   Заполняет dst[0..16). Вызывающий подмешивает AP nonce. */
void wpa_fill_nonce(uint8_t dst[32], uint32_t seed);

/* RFC 3394 AES-Key-Wrap unwrap: расшифровать wrapped (кратно 8, >=24) ключом
   kek[16], положить в plain (out_len = in_len-8). 0 при успехе (AIV 0xa6..). */
int wpa_aes_kw_unwrap(const uint8_t *kek, const uint8_t *wrapped, uint32_t in_len,
                      uint8_t *plain, uint32_t *out_len);

#endif /* WPA_CRYPTO_H */