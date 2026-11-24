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
    SYS("sg1000",     "Sega SG-1000",                CONSOLE,  READY),   // r1190: ядро gpgx, SYSTEM_SG
    SYS("gba",        "Game Boy Advance",            PORTABLE, READY),
    SYS("lynx",       "Atari Lynx",                  PORTABLE, READY),
    SYS("ngp",        "Neo Geo Pocket / Color",      PORTABLE, READY),
    // r1544: Game & Watch / Электроника (murmulator SM5xx). Каталог на SD —
    // /roms/gw/RUS (IM-XX, Электроника) и /roms/gw/ENG (G&W). Вход через
    // пункт "Game & Watch" раскрывает подменю-селектор IM-XX / G&W
    // (см. src/main.c, обработка id "gw").
    SYS_DIR("gw",     "Game & Watch",                NULL,     PORTABLE, READY),

    // -- Консоли --
    SYS("a2600",      "Atari 2600",                  CONSOLE,  READY),
    SYS("a5200",      "Atari 5200",                  CONSOLE,  READY),
    SYS("a7800",      "Atari 7800",                  CONSOLE,  READY),
    SYS_ALT("sms",    "Sega Master System",        NULL, "sms_roms", CONSOLE, READY),
    SYS("coleco",     "ColecoVision",                CONSOLE,  READY),
    SYS_ALT("nes",    "NES / Famicom (Dendy)",     NULL, "nes_roms", CONSOLE, READY),
    SYS("pce",        "PC Engine / TurboGrafx",      CONSOLE,  READY),
    SYS("snes",       "SNES / Super Famicom",        CONSOLE,  READY),
    SYS("megadrive",  "Sega Mega Drive / Genesis",     CONSOLE,  READY),
    // r1147: Sega 32X (дополнение к Mega Drive) — READY. Ядро — PicoDrive
    // (h3_bare/cores/picodrive, КАРТРИДЖНЫЙ 32X-путь; CD/SVP/имгарды собраны,
    // но для 32X-рома не активируются). dir задан явно (не id "32x"), чтобы имя
    // папки на SD = sega32x, как создано. Ромы — /roms/sega32x/*.bin|gen|md|32x.
    SYS_DIR("32x", "Sega 32X", "sega32x", CONSOLE, READY),
    SYS_DIR("vectrex", "GCE Vectrex", "GCE Vectrex", CONSOLE, READY),

    // -- Аркадные автоматы --
    SYS("fbneo",      "FB Neo (Arcade)",              ARCADE,   READY),
    SYS("cps1",       "CPS-1 (Capcom)",               ARCADE,   READY),
    SYS("cps2",       "CPS-2 (Capcom)",               ARCADE,   READY),
    SYS("neogeo",     "Neo Geo MVS",                  ARCADE,   READY),
    SYS("segasys",    "Sega System 16",                ARCADE,   READY),
    SYS("toaplan",    "Toaplan 1 (68K games)",        ARCADE,   READY),
    // r1610: Namco (Galaxian/Pac-Man/Rally X/Gaplus/Mappy/System 1/86/SS2),
    // папка /roms/namco на общем FBNeo-хосте (run_cps).
    SYS("namco",      "Namco (Arcade)",               ARCADE,   READY),
    // r1623: Taito (Rastan/Rainbow Islands/Volfied/Bubble Bobble/Arkanoid),
    // папка /roms/taito на том же FBNeo-хосте (run_cps).
    SYS("taito",      "Taito (Arcade)",               ARCADE,   READY),
    // r1626: Data East (DEC0: Robocop/Bad Dudes/Heavy Barrel + Karnov),
    // папка /roms/dataeast на том же FBNeo-хосте (run_cps).
    SYS("dataeast",   "Data East (Arcade)",           ARCADE,   READY),
    // r1627: Psikyo PS3 (Strikers 1945, Tengai), папка /roms/psikyo.
    SYS("psikyo",     "Psikyo (Arcade)",              ARCADE,   READY),
    SYS("cave",       "Cave (68K) STGs",              ARCADE,   READY),
    // -- Компьютеры --
    SYS("zxspectrum", "ZX Spectrum 48k/128k",        COMPUTER, READY),
    SYS_BUILTIN("msx",  "MSX / Yamaha YIS-503III",  COMPUTER, READY),
    SYS_ALT("atari800", "Atari 8-bit (400/800/XL/XE)", "atari800", "atari8", COMPUTER, READY),
    // r1185: Радио-86РК — READY (порт RK8266), папка /roms/radio86rk.
    SYS_DIR("radio86rk", "Radio-86RK", "radio86rk", COMPUTER, READY),
    SYS_DIR("atarist", "Atari ST", "atarist", COMPUTER, READY),
    SYS_BUILTIN("bk0010", "BK-0010/0011M",       COMPUTER, READY),
    SYS_BUILTIN("portfolio", "Atari Portfolio",  COMPUTER, READY),
    SYS_BUILTIN("ms1504",  "MS 1504",                     COMPUTER, READY),
    // r1412: IBM PC/XT-286 — ядро pico-286 (h3_bare/cores/pxt, чистый C).
    // Папка /roms/pxt. BIOS и VBIOS вшиты (includes/bios.h, const-массив);
    // диски — RAM-образы (флоппи A: и HDD C:) из аркадного пула, INT13 через
    // disks-h3.c.inl. Видео CGA/HGC/EGA/VGA 640x480, звук
    // PC-spkr/Covox/DSS/SB/Sn76489/CMS (AdLib/OPL на H3 отключён).
    SYS("pxt",        "PC/XT-286",                 COMPUTER, READY),
    SYS("cpc",        "Amstrad CPC",              COMPUTER, READY),
    // r1284: Commodore 64 — ядро Frodo (h3_bare/cores/frodo, PAL-only: VIC-II
    // 312 строк/50 Гц зашиты const в VIC.h, runtime-выбора региона нет).
    // Вшитый синий BASIC V2 (KERNAL/Basic/Char вшлиты через .incbin). Диски
    // 1541 (.d64/.t64) через ImageDrive, файлы -> fs_layer. Папка по id —
    // /roms/c64. Правило владельца: BIOS/ROM вшиты, с SD не грузятся.
    SYS("c64",        "Commodore 64",            COMPUTER, READY),
    // r1243: Commodore Amiga — ядро UAE4All (h3_bare/cores/uae4all,
    // uae4all_host.c); PUAE из сборки выведен. Запускает ADF-образы с SD
    // (/roms/amiga — папка по id); Kickstart 1.3 вшит через .incbin
    // (правило владельца: BIOS с SD не грузится). Дир по id = "amiga".
    SYS("amiga",      "Commodore Amiga",         COMPUTER, READY),
    // r1297: Агат-9 — ядро Agat Emulator (h3_bare/cores/agat), 6502 1 МГц,
    // кадр 640x400, палитра 16 цветов. ROM-монитор и знакогенератор вшиты
    // (.incbin в agat_roms.S), с SD не грузятся. Папка по id — /roms/agat.
    // r1299: один пункт — встроенного BASIC у Агат-9 нет (только монитор
    // в ПЗУ), подменю выбора прошивки не нужно.
    SYS("agat",       "Agat-9",                  COMPUTER, READY),
    // r1360: Apple II+ — то же ядро Agat Emulator (6502 1.0205 МГц, NTSC
    // 60 Гц), ПЗУ Applesoft/монитора, знакогенератор и ПЗУ контроллера
    // Disk II вшиты (.incbin в agat_roms.S), с SD не грузятся. Папка по
    // id — /roms/apple2; образы .dsk/.do/.po (143360), .nib и .woz.
    SYS("apple2",     "Apple II+",               COMPUTER, READY),
    // Enterprise 64/128: ядро ep128emu (h3_bare/cores/ep128emu, GPL2),
    // host-слой ep_host.cpp. Выбор 64/128 — подменю (main.c ep_model_dialog).
    // BIOS (EXOS/BASIC) вшиты в ядро, с SD не грузятся. Дир по id — /roms/enterprise.
    SYS("enterprise", "Enterprise 64/128",       COMPUTER, READY),
};

#define NUM_SYSTEMS (sizeof(systems) / sizeof(systems[0]))

// папка системы: dir если задан, иначе id (альтернативная в alt_dir
// используется только при ПОИСКЕ папки на SD — для СОЗДАНИЯ она не годится)
static inline const char* system_rom_dir(int idx) {
    return systems[idx].dir ? systems[idx].dir : systems[idx].id;
}

#endif