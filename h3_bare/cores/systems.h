#ifndef SYSTEMS_H
#define SYSTEMS_H

typedef enum {
    GROUP_PORTABLE,
    GROUP_CONSOLE,
    GROUP_ARCADE,
    GROUP_COMPUTER,
    GROUP_OTHER,
    GROUP_COUNT,
} system_group_t;

static const char* const group_names[GROUP_COUNT] = {
    "Portable",
    "Consoles",
    "Arcade",
    "Computers",
    "Other",
};

typedef enum {
    STATUS_PLANNED,
    STATUS_IN_PROGRESS,
    STATUS_READY,
} system_status_t;

typedef struct {
    const char *id;
    const char *name;
    const char *dir;      // основная папка в /roms/ (NULL = id)
    const char *alt_dir;  // альтернативная папка, если dir нет (NULL = нет)
    system_group_t group;
    system_status_t status;
    int builtin;          // 1 = работает без ROM на SD (BIOS вшит)
} system_entry_t;

#define SYS(id, name, group, status) { id, name, NULL, NULL, GROUP_##group, STATUS_##status, 0 }
#define SYS_DIR(id, name, dir, group, status) { id, name, dir, NULL, GROUP_##group, STATUS_##status, 0 }
#define SYS_ALT(id, name, dir, alt, group, status) { id, name, dir, alt, GROUP_##group, STATUS_##status, 0 }
#define SYS_BUILTIN(id, name, group, status) { id, name, NULL, NULL, GROUP_##group, STATUS_##status, 1 }

static const system_entry_t systems[] = {
    // -- Портативные консоли --
    SYS("gameboy",    "Game Boy / Game Boy Color",   PORTABLE, READY),
    SYS("gamegear",   "Sega Game Gear",              PORTABLE, READY),
    SYS("lynx",       "Atari Lynx",                  PORTABLE, READY),
    SYS("ngp",        "Neo Geo Pocket / Color",      PORTABLE, READY),

    // -- Консоли --
    SYS("a2600",      "Atari 2600",                  CONSOLE,  READY),
    SYS("a5200",      "Atari 5200",                  CONSOLE,  READY),
    SYS("a7800",      "Atari 7800",                  CONSOLE,  READY),
    SYS_ALT("sms",    "Sega Master System",        NULL, "sms_roms", CONSOLE, READY),
    SYS("coleco",     "ColecoVision",                CONSOLE,  PLANNED),
    SYS_ALT("nes",    "NES / Famicom (Dendy)",     NULL, "nes_roms", CONSOLE, READY),
    SYS("pce",        "PC Engine / TurboGrafx",      CONSOLE,  PLANNED),
    SYS("snes",       "SNES / Super Famicom",        CONSOLE,  READY),
    SYS("megadrive",  "Sega Mega Drive / Genesis",     CONSOLE,  READY),
    SYS_DIR("vectrex", "GCE Vectrex", "GCE Vectrex", CONSOLE, PLANNED),
    SYS_DIR("jaguar", "Atari Jaguar", "jaguar_roms", CONSOLE, PLANNED),

    // -- Аркадные автоматы --
    SYS("galaxian",   "Galaxian / Frogger / Dig Dug",ARCADE,   PLANNED),
    SYS("cps1",       "CPS-1 (Capcom)",               ARCADE,   PLANNED),
    SYS("cps2",       "CPS-2 (Capcom)",               ARCADE,   PLANNED),
    SYS("neogeo",     "Neo Geo MVS",                  ARCADE,   PLANNED),
    SYS("segasys",    "Sega System 1/2/16",           ARCADE,   PLANNED),
    SYS("toaplan",    "Toaplan 1",                    ARCADE,   PLANNED),

    // -- Компьютеры --
    SYS("zxspectrum", "ZX Spectrum 48k/128k",        COMPUTER, PLANNED),
    SYS("msx",        "MSX / MSX2",                  COMPUTER, PLANNED),
    SYS("radio86rk",  "Radio-86RK",                  COMPUTER, PLANNED),
    SYS("bk0010",     "BK-0010/0011M",               COMPUTER, PLANNED),
    SYS_BUILTIN("portfolio", "Atari Portfolio",     COMPUTER, READY),
    SYS("ms1504",     "MS 1504",                     COMPUTER, PLANNED),
};

#define NUM_SYSTEMS (sizeof(systems) / sizeof(systems[0]))

// папка системы: dir если задан, иначе id (альтернативная в alt_dir
// используется только при ПОИСКЕ папки на SD — для СОЗДАНИЯ она не годится)
static inline const char* system_rom_dir(int idx) {
    return systems[idx].dir ? systems[idx].dir : systems[idx].id;
}

#endif