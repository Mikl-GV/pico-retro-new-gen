/* Встроенная прошивка чипа RTL8189FTV (NIC) для загрузки через SDIO.
 * Массив сгенерирован из Array_MP_8188F_FW_NIC (halhwimg8188f_fw.c).
 * Символы:
 *   fw_blob_start — начало
 *   fw_blob_end   — конец
 */
    .section .text.fw_blob, "a"
    .balign 4
    .globl fw_blob_start
fw_blob_start:
    .incbin "fw_nic.bin"
    .globl fw_blob_end
fw_blob_end: