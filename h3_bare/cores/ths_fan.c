/* ths_fan.c — температура H3 (THS) + вентилятор с обратной связью (FG/tacho).
 *
 * Orange Pi Lite, bare-metal H3:
 *   THS (thermal sensor) база 0x01C25000, один сенсор.
 *   Формула (sun8i-h3, linux sun8i_thermal.c):
 *       t[м°C] = 217000 - (reg * 1211) / 10,  reg = 12-бит TEMP_DATA.
 *   Такты (sun8i-h3 ccu):
 *       APB1 gate 0x068 bit8  (bus-ths),
 *       APB1 reset 0x2D0 bit8 (RST_BUS_THS),
 *       ths mod clk 0x074: gate bit31, div [1:0]=0 (osc24M/1).
 *
 * Вентилятор (2-проводной с тахо, или 3-проводной):
 *   PA6  = управление (выход; HIGH = вкл через транзистор/MOSFET).
 *   PA7  = FG (тахометр, вход с подтяжкой): 2 импульса на оборот.
 *   RPM = (перепадов за 1 с) * 30.
 *
 * Пороги (гистерезис):
 *   вентилятор вкл при  T >= 60°C, выкл при T <= 50°C;
 *   «перегрев» (оверлей/предупреждение) при T >= 55°C, красный при >= 85°C.
 *
 * Поток: ths_fan_tick() вызывается из fb_flush() (каждый кадр любой системы и
 * меню) — внутри только дешёвые проверки по времени; оверлей рисует
 * ths_fan_overlay() сразу за ним (до clean D-cache), поэтому попадёт в кадр.
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "ths_fan.h"
#include "h3_hs_timer.h"
#include "fb_text.h"

extern int printf(const char* fmt, ...);

/* ---- THS ---- */
#define THS_BASE   0x01C25000u
#define THS_CTRL0  (*(volatile uint32_t*)(THS_BASE + 0x00u))
#define THS_CTRL2  (*(volatile uint32_t*)(THS_BASE + 0x40u))
#define THS_IC     (*(volatile uint32_t*)(THS_BASE + 0x44u))
#define THS_MFC    (*(volatile uint32_t*)(THS_BASE + 0x70u))
#define THS_TDATA  (*(volatile uint32_t*)(THS_BASE + 0x80u))

/* ---- CCU ---- */
#define CCU_BASE       0x01C20000u
#define CCU_APB1_GATE  (*(volatile uint32_t*)(CCU_BASE + 0x068u))  /* bit8 = bus-ths */
#define CCU_APB1_RST   (*(volatile uint32_t*)(CCU_BASE + 0x2D0u))  /* bit8 = ths */
#define CCU_THS_CLK    (*(volatile uint32_t*)(CCU_BASE + 0x074u))  /* div[1:0], gate31 */

#define THS_CAL_MD   217000      /* offset (м°C) */
#define THS_CAL_SCALE 1211       /* scale      */
#define THS_RAW_MASK  0xFFFu

/* ---- вентилятор ---- */
#define PA_BASE   0x01C20800u
#define PA_CFG0   (*(volatile uint32_t*)(PA_BASE + 0x00u))
#define PA_PULL0  (*(volatile uint32_t*)(PA_BASE + 0x1Cu))
#define PA_DAT    (*(volatile uint32_t*)(PA_BASE + 0x10u))
#define FAN_CTL   6              /* PA6 = выход управления */
#define FAN_FG    7              /* PA7 = вход тахо */

/* пороги (м°C) */
#define FAN_ON_MD   60000
#define FAN_OFF_MD  50000
#define OVERHEAT_MD 55000
#define CRIT_MD     85000

static int  g_ths_ok = 0;
static int  g_fan_on = 0;
static int  g_rpm = 0;
static int  g_temp_md = 0;       /* последнее измерение, м°C */
static uint32_t g_temp_t = 0;    /* мкс последнего чтения THS */

/* счётчик FG: уровень, счёт перепадов, время окна */
static int      g_fg_level = 0;
static uint32_t g_fg_edges = 0;
static uint32_t g_fg_last_change = 0;
static uint32_t g_rpm_t = 0;     /* время последнего расчёта RPM */

static inline void mb(void) {
    __asm volatile("dsb sy" ::: "memory");
}

void ths_fan_get(int* temp_c10, int* rpm) {
    if (temp_c10) *temp_c10 = g_temp_md / 100;
    if (rpm)      *rpm      = g_rpm;
}

int ths_fan_overheated(void) { return g_temp_md >= OVERHEAT_MD; }

int ths_read_millideg(void) {
    if (!g_ths_ok) return 0;
    uint32_t raw = THS_TDATA & THS_RAW_MASK;
    if (!raw) return 0;                     /* датчик ещё без данных */
    return THS_CAL_MD - (int)((raw * (uint32_t)THS_CAL_SCALE) / 10u);
}

void ths_fan_init(void) {
    /* 1) такты CCU: gate + снять reset + mod-clk (24 МГц, div=1) */
    CCU_APB1_GATE |= (1u << 8);
    mb();
    CCU_APB1_RST  |= (1u << 8);
    mb();
    CCU_THS_CLK   = (1u << 31) | 0u;   /* gate on, div 1 -> osc24M */
    mb();

    /* 2) инициализация THS (как sun8i_h3_thermal_init) */
    THS_MFC    = (1u << 2) | 1u;        /* filter enable, type 1 (avg 4) */
    THS_IC     = (365u << 12) | 0x100u; /* период ~0.25 c, IRQ line en 1 датчика */
    THS_CTRL0  = 479u;                  /* T_acq = 20 мкс @24 МГц */
    THS_CTRL2  = (479u << 16) | 1u;     /* T_acq1 + сенсор 0 вкл */
    mb();

    /* 3) вентилятор: PA6 выход (по умолчанию выкл), PA7 вход + подтяжка */
    PA_CFG0   &= ~(0xFu << (FAN_CTL * 4));
    PA_CFG0   |=  (0x1u << (FAN_CTL * 4));       /* PA6 = output */
    PA_CFG0   &= ~(0xFu << (FAN_FG  * 4));       /* PA7 = input  */
    PA_PULL0  &= ~(0x3u << (FAN_FG * 2));
    PA_PULL0  |=  (0x1u << (FAN_FG * 2));        /* pull-up (open-drain FG) */
    mb();
    PA_DAT    &= ~(1u << FAN_CTL);
    mb();

    g_fg_level = (PA_DAT >> FAN_FG) & 1;
    g_ths_ok = 1;
    g_temp_t = h3_hs_timer_lo_us();
    g_rpm_t  = g_temp_t;
    printf("THS: init ok (PA6=fan, PA7=FG)\n");
}

/* Перепады FG: вызывается каждый кадр; дёшево — сравнение уровня. */
static void ths_fan_fg_poll(void) {
    int lvl = (PA_DAT >> FAN_FG) & 1;
    if (lvl != g_fg_level) {
        g_fg_level = lvl;
        g_fg_edges++;
        g_fg_last_change = h3_hs_timer_lo_us();
    }
}

/* Расчёт RPM раз в ~1 с: edges за окно, 4 перепада/оборот → RPM = dps*15 */
static void ths_fan_rpm_update(uint32_t now) {
    static uint32_t s_edge_last = 0;
    uint32_t dt = now - g_rpm_t;
    if (dt < 1000000u) return;

    uint32_t de = g_fg_edges - s_edge_last;
    uint32_t dps = (de * 1000000u) / dt;         /* перепадов в секунду */
    if (de == 0) {
        g_rpm = 0;
    } else {
        g_rpm = (int)(dps * 60u / 4u);           /* 4 перепада = 1 оборот */
        if (g_rpm < 0) g_rpm = 0;
        if (g_rpm > 40000) g_rpm = 40000;        /* защита от дребезга */
    }
    s_edge_last = g_fg_edges;
    g_rpm_t = now;
}

/* Один тик из fb_flush: чтение THS (раз в ~500 мс), вентилятор, RPM. */
void ths_fan_update(void) {
    if (!g_ths_ok) return;
    uint32_t now = h3_hs_timer_lo_us();

    /* FG перепады — каждый вызов (дёшево) */
    ths_fan_fg_poll();

    /* температура раз в ~500 мс */
    if (now - g_temp_t < 500000u) {
        ths_fan_rpm_update(now);
        return;
    }
    g_temp_t = now;
    g_temp_md = ths_read_millideg();

    /* вентилятор по гистерезису */
    if (!g_fan_on && g_temp_md >= FAN_ON_MD) {
        g_fan_on = 1;
        PA_DAT |= (1u << FAN_CTL); mb();
        printf("THS: %d.%dC -> fan ON\n", g_temp_md / 1000,
               (g_temp_md / 100) % 10);
    } else if (g_fan_on && g_temp_md <= FAN_OFF_MD) {
        g_fan_on = 0;
        PA_DAT &= ~(1u << FAN_CTL); mb();
        printf("THS: %d.%dC -> fan OFF\n", g_temp_md / 1000,
               (g_temp_md / 100) % 10);
    }

    ths_fan_rpm_update(now);
}

/* Оверлей в правом верхнем углу: вызывается из fb_flush ПОСЛЕ ths_fan_update,
 * но ДО clean D-cache — попадает в текущий кадр.
 * r0.415: показывается ВСЕГДА (пользователь не увидел оверлей при T<55°C —
 * порог убран, теперь видно текущую температуру в любом состоянии).
 * Цвет: <55°C серый, 55-59 жёлтый, 60-84 оранжевый, >=85 красный. */
void ths_fan_overlay(void) {
    if (!g_ths_ok) return;

    char buf[40];
    int n;
    uint32_t color;

    if (g_temp_md >= CRIT_MD)      color = 0x00FF2020;   /* красный */
    else if (g_temp_md >= FAN_ON_MD) color = 0x00FFAA00; /* оранж  */
    else if (g_temp_md >= OVERHEAT_MD) color = 0x00FFFF00;/* жёлтый */
    else                            color = 0x00AAAAAA;  /* серый (норма) */

    if (g_fan_on)
        n = snprintf(buf, sizeof(buf), "%d.%dC F%dr", g_temp_md / 1000,
                     (g_temp_md / 100) % 10, g_rpm);
    else
        n = snprintf(buf, sizeof(buf), "%d.%dC", g_temp_md / 1000,
                     (g_temp_md / 100) % 10);
    if (n <= 0) return;

    /* шрифт 8x8, scale 2 → 16 пикселей на символ; правый край FB_W=1024 */
    int x = 1024 - n * 16 - 8;
    int y = 8;
    fb_puts_s(x, y, buf, 2, color);
}

// r0.415: явный запрос текущей температуры (для UART/A-B диагностики).
int ths_fan_temp_c10(void) { return g_temp_md / 100; }