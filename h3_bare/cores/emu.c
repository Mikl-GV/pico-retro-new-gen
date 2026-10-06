#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "emu.h"
#include "fb_text.h"
#include "uart.h"
#include "usb_kbd.h"
#include "sega_pad.h"
#include "btn_pad.h"
#include "h3_hs_timer.h"
#include "led.h"
#include "i2s.h"
#include "settings.h"

extern int printf(const char* fmt, ...);

#define EMU_FB  ((uint16_t*)0x5F800000)
#define EMU_W   320
#define EMU_H   240

#define FB_ADDR 0x5F900000
#define FB_W    1024
#define FB_H    600

static uint32_t g_border_color = 0;   // цвет полей (XRGB8888), 0 = чёрный

void emu_set_border_color(uint32_t rgb888) {
    g_border_color = rgb888;
}

// r703 (C3): double-buffer HDMI для direct-писателей (BK/Vectrex).
// ВЫКЛЮЧЕН по умолчанию. DE2 UI-канал запущен с единственным TOP_LADDR=FB_ADDR;
// настоящий vsync-free flip двух буферов без аппаратной проверки опасен (риск
// чёрного/рваного кадра на стенде), поэтому активный dbl-buf закрыт за макросом.
// Функция остаётся точкой интеграции: при включении bk/vecx пишут в back buffer,
// а emu_hdmi_flip() меняет H3_DE2_MUX0_UI->CFG[0].BOT_LADDR (см. h3_de2.h).
#define CONFIG_HDMI_DOUBLE_BUF 0   // 0 = выкл. (безопасно), 1 = вкл. (только со стендом)
// P0 (r725): второй FB для direct-писателей. Прежний 0x5FB40000 пересекался со
// стеками CPU2: FB 1024×600×4 = 0x25C000 → занято 0x5FB40000..0x5FD9C000, а SVC-
// стек CPU2 живёт на 0x5FC01000 (вниз) с банками исключений 0x5FBFD000..0x5FC00000.
// Новый адрес 0x5F400000: конец буфера 0x5F660000 — ниже EMU_FB (0x5F800000),
// ниже HDMI FB (0x5F900000+2.46МБ) и ниже стеков CPU2/CPU1 — пересечений нет.
#define FB_ADDR_BACK 0x5F400000u   // второй FB для direct-писателей (до EMU_FB)

void emu_hdmi_flip(void) {
#if CONFIG_HDMI_DOUBLE_BUF
    // Пометка для документации: здесь переключение адреса DE2 UI-канала.
    // Без правильного vsync-гейта (статус TCON1/DE2) flip может показать
    // пустой буфер — проверяется только на живом железе.
    extern void h3_de2_set_ui_addr(uint32_t addr);
    h3_de2_set_ui_addr(FB_ADDR_BACK);
#else
    (void)FB_ADDR_BACK;
#endif
}

// ---- единый nearest-neighbour скейлер ----
// Оптимизация: без делений в пиксельном цикле. Таблица sx[] (индекс исходной
// колонки для каждой целевой) считается один раз — на Cortex-A7 деление ~12-20
// тактов, а при 500К+ пикселей это съедало 5-10 мс/кадр.
void emu_scale(int src_w, int src_h) {
    if (src_w <= 0 || src_h <= 0) return;
    // r0.417 (H3 аудита): буфер EMU_FB фиксирован 320×240 — клампим входные
    // размеры, иначе hi-res режимы (SNES 512 колонок) читают за его конец.
    if (src_w > EMU_W) src_w = EMU_W;
    if (src_h > EMU_H) src_h = EMU_H;
    int dst_w = (src_w * FB_H) / src_h;
    if (dst_w > FB_W) dst_w = FB_W;
    if (dst_w <= 0) return;
    int dst_h = FB_H;
    int dst_x = (FB_W - dst_w) / 2;
    uint32_t* dst = (uint32_t*)FB_ADDR;

    // Предрасчёт исходной колонки для каждой целевой (nearest neighbour):
    // аккумулятор 16.16 — каждая итерация добавляет src_w/dst_w, обёртка
    // по src_w (амплитуда = src_w, шаг = src_w/dst_w).
    static int sx_tab[FB_W];
    uint32_t step_x = ((uint32_t)src_w << 16) / (uint32_t)dst_w;
    uint32_t acc_x = step_x >> 1;   // округление к ближайшему
    for (int dx = 0; dx < dst_w; dx++) {
        sx_tab[dx] = (int)(acc_x >> 16);
        acc_x += step_x;
        if (acc_x >= ((uint32_t)src_w << 16)) acc_x -= (uint32_t)src_w << 16;
    }

    // левое/правое поле
    if (g_border_color && dst_x > 0) {
        for (int dy = 0; dy < dst_h; dy++)
            for (int dx = 0; dx < dst_x; dx++)
                dst[dy * FB_W + dx] = g_border_color;
    }
    if (g_border_color) {
        int right = dst_x + dst_w;
        for (int dy = 0; dy < dst_h; dy++)
            for (int dx = right; dx < FB_W; dx++)
                dst[dy * FB_W + dx] = g_border_color;
    }

    // Построковый проход: исходная строка = аккумулятор 16.16 => без `/`
    uint32_t step_y = ((uint32_t)src_h << 16) / (uint32_t)dst_h;
    uint32_t y_acc = step_y >> 1;              // округление к ближайшей строке
    int sy = 0;
    for (int dy = 0; dy < dst_h; dy++) {
        const uint16_t* src = EMU_FB + sy * EMU_W;
        uint32_t* d = dst + (uint32_t)dy * FB_W + (uint32_t)dst_x;
        for (int dx = 0; dx < dst_w; dx++) {
            uint16_t p = src[sx_tab[dx]];
            uint32_t r = ((p >> 11) & 0x1F) << 3;
            uint32_t g = ((p >> 5) & 0x3F) << 2;
            uint32_t b = (p & 0x1F) << 3;
            d[dx] = (r << 16) | (g << 8) | b;
        }
        y_acc += step_y;
        int nsy = (int)(y_acc >> 16);
        if (nsy > sy) { if (nsy >= src_h) nsy = src_h - 1; sy = nsy; }
    }
}

// ---- integer scale (целочисленный множитель) для портативных систем ----
// Картинка чёткая: один пиксель исходника = N×N пикселей экрана, поля по
// бокам (снизу/сверху). Множитель — максимальный, влезающий в 1024×600.
// Формат на выходе — RGB565 → XRGB8888 в FB_ADDR (как emu_scale).
void emu_scale_int(int src_w, int src_h) {
    if (src_w <= 0 || src_h <= 0) return;   // r0.198: защита от div-by-0 (как в emu_scale)
    // r0.417 (H3): те же клампы, что в emu_scale — buffer EMU_FB 320×240
    if (src_w > EMU_W) src_w = EMU_W;
    if (src_h > EMU_H) src_h = EMU_H;
    // Выбираем множитель: min(FB_W/src_w, FB_H/src_h), целый
    int mul = FB_W / src_w;
    int mh  = FB_H / src_h;
    if (mh < mul) mul = mh;
    if (mul < 1) { emu_scale(src_w, src_h); return; }  // падаем на stretch

    int dst_w = src_w * mul;
    int dst_h = src_h * mul;
    int dst_x = (FB_W - dst_w) / 2;
    int dst_y = (FB_H - dst_h) / 2;
    uint32_t* dst = (uint32_t*)FB_ADDR;

    // Поля (сверху/снизу/слева/справа)
    if (g_border_color) {
        // верх
        for (int y = 0; y < dst_y; y++)
            for (int x = 0; x < FB_W; x++)
                dst[y * FB_W + x] = g_border_color;
        // низ
        for (int y = dst_y + dst_h; y < FB_H; y++)
            for (int x = 0; x < FB_W; x++)
                dst[y * FB_W + x] = g_border_color;
        // левая/правая полоса (в зоне картинки)
        for (int y = dst_y; y < dst_y + dst_h; y++) {
            for (int x = 0; x < dst_x; x++)
                dst[y * FB_W + x] = g_border_color;
            for (int x = dst_x + dst_w; x < FB_W; x++)
                dst[y * FB_W + x] = g_border_color;
        }
    }

    // Масштабирование: каждый исходный пиксель — блок mul×mul
    for (int sy = 0; sy < src_h; sy++) {
        const uint16_t* src = EMU_FB + sy * EMU_W;
        int dy0 = dst_y + sy * mul;
        for (int sx = 0; sx < src_w; sx++) {
            uint16_t p = src[sx];
            uint32_t c = (((p >> 11) & 0x1F) << 3) << 16
                       | (((p >> 5) & 0x3F) << 2) << 8
                       | ((p & 0x1F) << 3);
            int dx0 = dst_x + sx * mul;
            for (int dy = 0; dy < mul; dy++)
                for (int dx = 0; dx < mul; dx++)
                    dst[(dy0 + dy) * FB_W + dx0 + dx] = c;
        }
    }
}

void emu_clear_fb(void) {
    memset((void*)EMU_FB, 0, EMU_W * EMU_H * 2);
}

// r0.256: единая подготовка перед запуском ЛЮБОГО эмулятора.
//  - gb_heap_reset(): bump-пул ядровых malloc — при повторном входе без сброса
//    память эмулятора остаётся от предыдущего запуска (и растёт);
//  - EMU_FB + HDMI-кадр: у систем, чей рендер НЕ покрывает весь EMU_FB
//    (напр. Fuse пишет только 256/320 колонок, а emu_scale читает все 320),
//    на экране оставались куски «загруженного до этого».
extern void gb_heap_reset(void);
extern void newlib_heap_reset(void);
extern void cheats_reset(void);
void emu_prepare(void) {
    gb_heap_reset();
    newlib_heap_reset();   // r0.417 (H2): newlib-куча (_sbrk) не растёт между запусками игр
    // r0.417 (M14/M15): читы ОТКЛЮЧЕНЫ до послойной доработки — список всегда
    // пуст на входе в любой эмулятор (включая builtin; rom_browser сбрасывал
    // только свои пути). Пустой список = применение в host-слоях — no-op.
    cheats_reset();
    emu_period_us = 16667;   // r622: сброс периода в дефолт — иначе «протечка»
                             // между системами (напр. NGP ставил 16200, а выход
                             // возвращал saved_period от прошлого, не 16667).
    i2s_ring_reset();   // r506: кольцо I2S не должно нести сэмплы предыдущей системы
    // r702: ring_reset больше НЕ делает auto-RESUME (чтобы тест-тон/клик,
    // пишущие напрямую в FIFO, держали CPU2 в паузе). Эмулятору долив нужен —
    // возвращаем CPU2 в работу явно (watermark накопит запас, CPU2 заиграет).
    i2s_audio_cmd(AUDIO_CMD_RESUME);
    emu_clear_fb();
    fb_clear();
    fb_flush();
}

// r0.385: манифест звуковых ядер системы — только лог для послойного
// подключения звука. Сами ядра линкуются, звук НЕ задействован.
void snd_manifest(const char* sys, const char* cores) {
    // r631(лог): строка «(sound OFF)» устарела — звук подключён у большинства
    // систем (r623+). Печатаем только список звуковых ядер системы.
    if (!sys) sys = "?";
    if (!cores || !cores[0]) cores = "(none)";
    printf("SND %s cores: %s\n", sys, cores);
}

// ---- throttle ----
// C5: throttle от РЕАЛЬНОГО времени, с накоплением «долга» переработки.
// Раньше (r585): emu_ts0 переустанавливался на now каждый кадр — если кадр
// эмуляции был дольше периода (тяжёлая сцена), переработка ТЕРЯЛАСЬ: видео
// «плавало» по реальному времени, а звук (ведомый железным 48 кГц FIFO на
// CPU2) не догонялся → накапливался рассинхрон видео↔звук и «хвосты».
// C5: база сдвигается на +period от СТАРОЙ базы (а не на now), а всё, что
// ушло на эмуляцию сверх периода, засчитывается в emu_debt. Следующий кадр
// ждёт период - debt: мелкая переработка догоняется, дрейф не копится.
static uint32_t emu_ts0 = 0;      // следующая «распланированная» граница кадра
static int32_t  emu_debt = 0;     // переработка [мкс], вычтем из следующего wait

// r703 (C4): liveness CPU2. g_audio_beat инкрементится CPU2 в цикле — если он
// перестал расти, аудио-ядро зависло (напр. на I2S_FIFO_STA-спине) при том, что
// g_audio_core_active() всё ещё =1. Нужен явный detection + fallback на долив core0,
// иначе зависший CPU2 = вечная тишина без возврата к штатному выводу.
// r739 (ОТКАТ fallback): hang-детект с доливом core0 УДАЛЁН — он создавал
// ВТОРОГО писателя TX FIFO (см. ниже). Единственный писатель FIFO = CPU2 (или
// DMA). Мониторинг живости CPU2 остаётся инструментом диагностики (Шаг 4 плана),
// но НЕ должен включать долив core0, пока CPU2 жив.
void emu_throttle(void) {
    // r155: мигание alive (PL10, «код жив») убрано с core0 — теперь его делает
    // ЯДРО 1 (led_heartbeat_cpu1), чтобы core0 не писал в PA_DAT (гонка с PA21/CS).

    uint32_t now = h3_hs_timer_lo_us();
    if (!emu_ts0) emu_ts0 = now;   // первый кадр — стартовая база
    uint32_t period = emu_period_us;

    // Сколько в этой итерации реально заняла эмуляция с прошлого throttle.
    uint32_t elapsed = now - emu_ts0 + (uint32_t)emu_debt;
    emu_debt = 0;

    if (elapsed < period) {
        // В уложились: ждём остаток периода. ЕДИНСТВЕННЫЙ писатель TX FIFO —
        // CPU2 (или DMA). core0 ТОЛЬКО синтезирует и льёт в кольцо (i2s_push_sample),
        // FIFO НЕ трогает НИКОГДА.
        // r739: fallback-долив core0 УДАЛЁН. Раньше при ложном hang-детекте
        // (stale-чтение flush_cnt) core0 включал i2s_flush_max → ДВА писателя FIFO
        // (core0 + живой CPU2) → FIFO наглухо полон, долив CPU2 мёртв, кольцо
        // переполняется (dropped), звук «песок/рваный». Если CPU2 реально завис —
        // звук молчит до перезапуска/восстановления ядра (отдельная задача Шаг 4),
        // но второго писателя НЕ создаём.
        uint32_t wait = period - elapsed;
        udelay(wait);
        // База сдвигается ровно на один период от СТАРОЙ границы:
        // переработка (если она была) «догоняется» здесь, а не теряется.
        emu_ts0 += period;
    } else {
        // Переработали период (кадр был тяжёлым). Не ждём — сразу рисуем,
        // а переработку копим как долг, чтобы следующие кадры его догнали.
        emu_debt = (int32_t)(elapsed - period);
        if (emu_debt > (int32_t)(period * 3)) emu_debt = (int32_t)(period * 3); // кап: не уходить в минус надолго
        emu_ts0 += period;   // граница всё равно сдвинута на период
        // r739: fallback долива убран — см. выше (единственный писатель FIFO = CPU2).
    }
}

void emu_throttle_reset(void) {
    emu_ts0 = 0;
    emu_debt = 0;
}

// ---- единый выход из эмулятора: удержание ~0.9 с ----
// Клавиша ESC (USB) ИЛИ Sega-геймпад Start+Mode вместе.
// Для геймпада не сканируем пад каждый кадр: кэш 50 мс — достаточно,
// чтобы поймать удержание, и не плодим лишние I2C-транзакции на шине.
// r155: armed-предохранитель — удержание НЕ стартует, пока после входа в
// эмулятор не случился хотя бы один «чистый» кадр без нажатий. Иначе
// зажатый при выходе из предыдущей игры ESC/Start+Mode «доезжает» в новую
// и через ~0.9 с выкидывает её обратно в меню («условие выхода осталось»).
static uint32_t g_esc_hold_us = 0;
static uint32_t g_pad_esc_t = 0;
static uint16_t g_pad_esc_val = 0;
static int      g_esc_armed = 0;
static uint32_t g_no_esc_since = 0;   // r155: sticky — отсутствие нажатия выхода
static uint32_t g_esc_arm_t = 0;      // r158: сколько держится «доезд» после входа
static int      g_esc_hold_from_pad = 0; // r590: hold накоплен от Start (геймпад)

void emu_esc_hold_reset(void) {
    g_esc_hold_us = 0;
    g_pad_esc_val = 0;
    g_pad_esc_t   = 0;
    g_esc_armed   = 0;
    g_no_esc_since = 0;
    g_esc_arm_t   = 0;
    g_esc_hold_from_pad = 0;
    usb_kbd_esc3_reset();   // ESC x3 (Low-Speed донгл) не должен «доехать»
}

int emu_esc_hold(void) {
    // Выход по ESC x3 — для Low-Speed донгла (I8 Pro), где длинное удержание
    // ненадёжно. Срабатывает от фронтов, «доезд» залипшей ESC не сгенерирует
    // (повторов не бывает), поэтому арм-предохранитель обходим сознательно.
    if (usb_kbd_esc3_pressed()) {
        i2s_ring_reset();
        // r735 (P1-2): ring_reset ставит CPU2 на PAUSE (r702, без авто-RESUME);
        // здесь возвращаем долив явно — иначе после выхода по ESC×3 аудио-ядро
        // оставалось в паузе до следующего emu_prepare (фон меню молчал).
        if (i2s_audio_core_active()) i2s_audio_cmd(AUDIO_CMD_RESUME);
        return 1;
    }

    uint8_t raw_keys[6];
    // r0.221: НИ ОДНОГО нового USB-чтения здесь! Два usb_kbd_get_raw за кадр
    // (ядро + этот полл) пере-армят interrupt-IN TD, пока HC ещё обрабатывает
    // его → ED-цепочка OHCI рассинхронизируется и клавиатура «замирает» во
    // всех эмуляторах и в меню после выхода. Читаем ТОЛЬКО кэш последнего
    // отчёта, который обновил host-слой ядра на этом кадре.
    int n = usb_kbd_get_last(raw_keys, 6);
    int esc = 0;
    for (int i = 0; i < n; i++)
        if (raw_keys[i] == 41) { esc = 1; break; }

// Геймпад: выход в меню — ТОЛЬКО Start+Select удержанием ~0.9 с.
    // Select: на джойстике 6-btn это Mode (0x0800), на кнопочной плате — X
    // (0x0100, btn_pad «X=Select/Coin»). Чтобы выход работал на ОБОИХ
    // устройствах, комбинация = Start(0x0080) + (Mode|X).
    // Одиночный Start НЕ выходит: он во многих ядрах нужен как игровая кнопка
    // (пауза/старт), и короткие нажатия Start не должны выкидывать из игры.
    uint32_t now = h3_hs_timer_lo_us();
    if (now - g_pad_esc_t > 50000) {
        g_pad_esc_t = now;
        g_pad_esc_val = pad_scan_combined();
    }
    // r590/r624: выход по геймпаду — ТОЛЬКО непрерывное удержание
    // Start+Mode ~1 с. Одиночный Start не накапливает hold.
    int pad_start = 0;
    int pad_esc = (g_pad_esc_val & 0x0080) && (g_pad_esc_val & (0x0800 | 0x0100));
    if (pad_esc) { esc = 1; pad_start = 1; }   // Start+Select (джойстик Mode / плата X)

    if (esc) {
        g_no_esc_since = 0;
        if (!g_esc_armed) {
            // «Доезд» из прошлой игры блокируем, НО не навсегда: если нажатие
            // держится >1.5 с — это не остаток, а залип пада или реальное
            // удержание. Разоружаемся, иначе выход был бы мёртв навсегда
            // (симптом: «не выходит из эмулятора» + спам при esc=1).
            if (!g_esc_arm_t) g_esc_arm_t = now;
            else if (now - g_esc_arm_t > 1500000) { g_esc_armed = 1; g_esc_arm_t = 0; }
            if (!g_esc_armed) return 0;
        }
        if (!g_esc_hold_us) { g_esc_hold_us = now; g_esc_hold_from_pad = pad_start; }
        else if (now - g_esc_hold_us > 900000) { g_esc_hold_us = 0; i2s_ring_reset(); return 1; }
    } else {
        g_esc_arm_t = 0;
        g_esc_hold_from_pad = 0;   // Start отпущен — любое накопление недействительно
        // r155: клавиатура шлёт отчёты ПАЧКАМИ (между ними «пустые» промежутки),
        // поэтому непрерывное удержание не требуется: отсчёт выхода теряется
        // только если нажатие отсутствует >250 мс подряд. Иначе после
        // «поиграть» пустой пакет клавы каждые ~10-30 мс рвал бы удержание
        // и выход никогда не накапливался.
        if (g_esc_hold_us) {
            if (g_esc_hold_from_pad) {
                // hold от геймпада: отпускание Start сбрасывает hold СРАЗУ
                // (только непрерывное удержание 1 с, без «суммирования»
                // коротких нажатий).
                g_esc_hold_us = 0;
                g_no_esc_since = 0;
            } else {
                if (!g_no_esc_since) g_no_esc_since = now;
                else if (now - g_no_esc_since > 250000) { g_esc_hold_us = 0; g_no_esc_since = 0; }
            }
        } else {
            g_no_esc_since = 0;
            g_esc_armed = 1;   // чистый кадр без удержания — «доезд» разряжен
        }
    }
    return 0;
}

// ---- эмуляторы ----
extern void atari2600_init(const uint8_t* rom, uint32_t size);
extern void atari2600_run_frame(void);
extern void atari2600_set_difficulty(int p1_expert);
extern int a7800_init_game(const uint8_t* rom, uint32_t size);
extern void a7800_run_frame(void);
extern int a5200_init_game(const uint8_t* rom, uint32_t size);
extern void a5200_run_frame(void);
extern int sms_init_game(const uint8_t* rom, uint32_t size);
extern void sms_run_frame(void);
extern int gg_init_game(const uint8_t* rom, uint32_t size);
extern void gg_run_frame(void);
extern int portfolio_init_game(const uint8_t* rom, uint32_t size);
extern void portfolio_run_frame(void);
extern int portfolio_exit_requested(void);
extern int gb_init_game(const uint8_t* rom, uint32_t size);
extern void gb_run_frame(void);
extern void gb_render_frame(void);
extern int gba_init_game(const uint8_t* rom, uint32_t size);
extern void gba_run_frame(void);
extern void gba_render_frame(void);
extern int lynx_init_game(const uint8_t* rom, uint32_t size);
extern void lynx_run_frame(void);
extern void lynx_render_frame(void);
extern int ngp_init_game(const uint8_t* rom, uint32_t size);
extern void ngp_run_frame(void);
extern void emu_run_snes(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_nes(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_megadrive(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_msx(const uint8_t* rom, uint32_t size, const char* rom_name);
extern void emu_run_coleco(const uint8_t* rom, uint32_t size, const char* rom_name);

void emu_run_a7800(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("a7800", "psg pokey");
    if (a7800_init_game(rom, size) != 1) {
        printf("A7800: init failed\n"); return;
    }
    printf("A7800: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00281206);   // тёмно-бордовый
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        a7800_run_frame(); emu_throttle(); emu_scale(320, 240); fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: i2s_ring_reset(); fb_clear(); fb_flush();
}

void emu_run_a5200(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("a5200", "pokey");
    if (a5200_init_game(rom, size) != 1) {
        printf("A5200: init failed\n"); return;
    }
    printf("A5200: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00061428);   // тёмно-синий
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        a5200_run_frame();
        emu_throttle();
        emu_scale(320, 240);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: i2s_ring_reset(); fb_clear(); fb_flush();
}

void emu_run_sms(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("sms", "psg sn76496");
    if (sms_init_game(rom, size) != 1) {
        printf("SMS: init failed\n"); return;
    }
    printf("SMS: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00081430);   // тёмно-синий (SMS)
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        sms_run_frame();
        emu_throttle();
        emu_scale(256, 192);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: i2s_ring_reset(); fb_clear(); fb_flush();
}

void emu_run_gg(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    if (gg_init_game(rom, size) != 1) {
        printf("GG: init failed\n"); return;
    }
    printf("GG: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00082030);   // тёмно-синий (GG)
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        gg_run_frame();
        emu_throttle();
        emu_scale_int(160, 144);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: i2s_ring_reset(); fb_clear(); fb_flush();
}

void emu_run_a2600_mcume(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("a2600", "tia");
    atari2600_init(rom, size);
    atari2600_set_difficulty(a2600_diff_expert);
    printf("MCUME: \"%s\" size=%d diff=%s\n", rom_name ? rom_name : "?", (int)size,
           a2600_diff_expert ? "Expert" : "Novice");
    emu_set_border_color(0x00201A08);   // тёмно-янтарный (woodgrain A2600)
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        atari2600_run_frame(); emu_throttle(); emu_scale(160, 192); fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: i2s_ring_reset(); fb_clear(); fb_flush();
}

void emu_run_portfolio(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("portfolio", "none");
    if (portfolio_init_game(rom, size) != 1) {
        printf("Portfolio: init failed\n"); return;
    }
    printf("Portfolio: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x00101816);   // тёмно-оливковый
    uint32_t fc = 0;
    emu_throttle_reset();
    for (;;) {
        portfolio_run_frame();
        if (portfolio_exit_requested()) break;
        emu_throttle();
        emu_scale(320, 240);
        fb_flush();
        fc++;
    }
    fb_clear(); fb_flush();
    i2s_ring_reset();   // r735: выходим из Portfolio — сброс звукового состояния
}

void emu_run_gameboy(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("gameboy", "apu");
    if (gb_init_game(rom, size) != 1) {
        printf("GameBoy: init failed\n"); return;
    }
    printf("GameBoy: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x000E1A0E);   // тёмно-зелёный (DMG)
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        gb_run_frame();
        gb_render_frame();
        emu_throttle();
        emu_scale_int(160, 144);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: i2s_ring_reset(); fb_clear(); fb_flush();
}

void emu_run_gba(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("gba", "psg");
    if (gba_init_game(rom, size) != 1) {
        printf("GBA: init failed\n"); return;
    }
    printf("GBA: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x000E1A2E);   // тёмно-синий (GBA)
    emu_throttle_reset();
    emu_esc_hold_reset();
    for (;;) {
        gba_run_frame();
        // r645: период кадра от ФАКТИЧЕСКОГО числа пар звука за кадр
        // (вместо магического адреса 0x01C22028 — хрупкий прямой доступ к I2S).
        {
            extern uint32_t gba_last_pairs(void);
            uint32_t p = gba_last_pairs();
            if (p >= 400 && p <= 2000) {
                uint32_t peri = (uint32_t)(((uint64_t)p * 1000000u) / 48000u);
                if (peri >= 12000 && peri <= 24000)
                    emu_period_us = (uint16_t)(((uint32_t)emu_period_us * 7 + peri) / 8);
            }
        }
        gba_render_frame();
        emu_throttle();
        emu_scale_int(240, 160);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: emu_period_us = 16667; i2s_ring_reset(); fb_clear(); fb_flush();
}

void emu_run_lynx(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("lynx", "i2s");
    if (lynx_init_game(rom, size) != 1) {
        printf("Lynx: init failed\n"); return;
    }
    printf("Lynx: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x000E0D26);   // тёмно-фиолетовый (Lynx)
    emu_throttle_reset();
    emu_esc_hold_reset();
    // r511: рефреш Lynx ~75 Гц (48000/75 = 640 сэмплов/кадр), а не 60 Гц.
    // Период кадра считаем от фактического числа сэмплов (пара = 21 мкс).
    // r727 (синхронизация кольца): стартовый период СРАЗУ 13333 мкс (75 Гц),
    // а не 16667 с EMA-сходимостью за ~8 кадров. Пока EMA сходилась, кольцо
    // пустело (продюсер медленнее железа) и CPU2 заливал тишину, затем
    // переполнялось — стартовый дрейф/плавание задержки в первые секунды.
    uint16_t saved_period = emu_period_us;
    emu_period_us = 13333;   // 75 Гц (48000/640 пар)
    for (;;) {
        lynx_run_frame();
        // r645: период кадра от ФАКТИЧЕСКОГО числа пар звука за кадр
        // (убрано слежение по магическому адресу 0x01C22028 — хрупкий прямой
        // доступ к I2S). Lynx: 75 Гц, ~640 пар/кадр при 48000.
        {
            extern uint32_t lynx_last_pairs(void);
            uint32_t p = lynx_last_pairs();
            if (p >= 300 && p <= 1200) {
                uint32_t peri = (uint32_t)(((uint64_t)p * 1000000u) / 48000u);
                if (peri >= 8000 && peri <= 16000)
                    emu_period_us = (uint16_t)(((uint32_t)emu_period_us * 7 + peri) / 8);
            }
        }
        lynx_render_frame();
        emu_throttle();
        emu_scale_int(160, 102);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit: emu_period_us = 16667; i2s_ring_reset(); fb_clear(); fb_flush();
}

void emu_run_ngp(const uint8_t* rom, uint32_t size, const char* rom_name) {
    emu_prepare();
    snd_manifest("ngp", "psg dac");
    if (ngp_init_game(rom, size) != 1) {
        printf("NGP: init failed\n"); return;
    }
    printf("NGP: \"%s\" size=%d\n", rom_name ? rom_name : "?", (int)size);
    emu_set_border_color(0x000E1A2B);   // тёмно-синий (NGP)
    emu_throttle_reset();
    emu_esc_hold_reset();
    // r0.417 (NGP, п.5 сессии): ядро RACE считает кадр 515×198=101970 тиков
    // при Ticks=6·2^20 → ~61.7 Гц, а throttle стоял на общих 60 Гц — игра шла
    // на ~2.8% медленнее. Подстраиваем период под ядро; возвращаем общий 60 Гц
    // на выходе (паттерн из CPS, r0.390).
    uint16_t saved_period = emu_period_us;
    emu_period_us = 16200;   // 61.7 Гц
    for (;;) {
        ngp_run_frame();
        emu_throttle();
        emu_scale_int(160, 152);
        fb_flush();
        if (emu_esc_hold()) goto exit;
    }
exit:
    emu_period_us = 16667;   // r645: всегда дефолтный период (нет протечки от NGP 16200)
    i2s_ring_reset();   // r735: сброс звука при выходе
    fb_clear(); fb_flush();
}