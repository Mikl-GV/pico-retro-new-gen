#ifndef SYSTEMS_H
#define SYSTEMS_H

typedef enum {
    GROUP_PORTABLE,  // Портативные консоли
    GROUP_CONSOLE,   // Консоли
    GROUP_COMPUTER,  // Компьютеры
    GROUP_COUNT,
} system_group_t;

static const char* const group_names[GROUP_COUNT] = {
    "Портативные консоли",
    "Консоли",
    "Компьютеры",
};

typedef enum {
    STATUS_PLANNED,
    STATUS_IN_PROGRESS,
    STATUS_READY,
} system_status_t;

typedef struct {
    const char *id;
    const char *name;
    system_group_t group;
    system_status_t status;
} system_entry_t;

#define SYS(id, name, group, status) { id, name, GROUP_##group, STATUS_##status }

static const system_entry_t systems[] = {
    // -- Портативные консоли --
    SYS("gameboy",    "Game Boy / GBC",            PORTABLE, PLANNED),
    SYS("gamegear",   "Sega Game Gear",            PORTABLE, PLANNED),

    // -- Консоли --
    SYS("a2600",      "Atari 2600",                CONSOLE,  READY),
    SYS("a5200",      "Atari 5200",                CONSOLE,  READY),
    SYS("a7800",      "Atari 7800",                CONSOLE,  READY),
    SYS("sms",        "Sega Master System",        CONSOLE,  PLANNED),
    SYS("coleco",     "ColecoVision",              CONSOLE,  PLANNED),
    SYS("nes",        "NES / Famicom (Dendy)",     CONSOLE,  PLANNED),
    SYS("galaxian",   "Galaxian / Frogger / Dig Dug", CONSOLE, PLANNED),
    SYS("cps1",       "CPS-1 (Capcom)",            CONSOLE,  PLANNED),
    SYS("cps2",       "CPS-2 (Capcom)",            CONSOLE,  PLANNED),
    SYS("neogeo",     "Neo Geo MVS",               CONSOLE,  PLANNED),
    SYS("toaplan",    "Toaplan 1",                 CONSOLE,  PLANNED),
    SYS("segasys",    "Sega System 1/2/16",        CONSOLE,  PLANNED),
    SYS("megadrive",  "Sega Mega Drive",           CONSOLE,  PLANNED),
    SYS("snes",       "SNES (Super Nintendo)",     CONSOLE,  PLANNED),
    SYS("pce",        "PC Engine",                 CONSOLE,  PLANNED),

    // -- Компьютеры --
    SYS("zxspectrum", "ZX Spectrum 48k/128k",      COMPUTER, PLANNED),
    SYS("msx",        "MSX / MSX2",                COMPUTER, PLANNED),
    SYS("radio86rk",  "Радио-86РК",                COMPUTER, PLANNED),
    SYS("bk0010",     "БК-0010/0011М",             COMPUTER, PLANNED),
};

#define NUM_SYSTEMS (sizeof(systems) / sizeof(systems[0]))

#endif