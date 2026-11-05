# Аудит pico-retro-new-gen (h3_bare) — R0: первый ответ агента (шаблон §13)

Дата: 2026-11-03
Версия прошивки: **r1008** (`h3_bare/src/main.c:431,439` — `g_fw_version`/`build:`)
Статус: черновик открытия аудита. Правки в код НЕ вносились (п.10/§11 промта — только после утверждения дельт).
Тулчейн: `C:\ARM\gcc-arm-none-eabi-15.2.1\bin\arm-none-eabi-gcc.exe` (15.2.1) — объявлен в промте, ещё не запускался (сборка не имитируется).

---

## 1. Границы аудита

**Аудируется (только исходники, текущее дерево, без git-истории):**

| Область | Путь | Объём |
|---|---|---|
| Платформа | `h3_bare/platform/` | 19 файлов, 9 757 строк |
| Рантайм/точка входа | `h3_bare/src/` | 5 файлов, 1 291 строка |
| Регистровые карты | `h3_bare/include/` | 9 файлов, 1 932 строки |
| Host-слой | `h3_bare/cores/*.c/*.cpp/*.h` (без вендор-каталогов) | 60 файлов, ≈16 068 строк; большие C-файлы ядра host — 11 770 строк |
| Вендор-хосты | `msx/host/`, `ms1504/`, `portfolio/`, `fceumm/nes_host_fceumm.cpp`, `ngp/` | в составе S8 |
| Ядра (vendored) | `h3_bare/cores/<core>/**` | ~2.16 млн строк, 5 769 файлов — только граница портирования |
| Сборка | `Makefile` (1276 строк), `build.sh`, `build.ps1`, `cores/*_rename.sh`, `platform/linker.ld` | — |

**Исключено** (не открывать, не цитировать, не коммитить): `roms/`, `emulator-refs/`, `.git/`, `build/`, `*.o`, `*.bin`, PDF как код, `503III/`, `MS1504/`, `pic/`, `tools/ftp_client/`, `emulation_master.xlsx`, `top_lists.xlsx`, `*.lnk`.

**Запреты соблюдаю:** история git не используется; правки — только после утверждения; `cores/sega_pad.c` — только статический разбор с пометкой `REQUIRES_HARDWARE_VALIDATION` (шапка файла «НЕ ЛАЗИТЬ БЕЗ СТЕНДА»).

**Замечание к §2 промта:** файла `docs/AUDIT_2026-10-29_r848_FIXPLAN.md` в текущем дереве нет (в `docs/` присутствует только `AUDIT_2026-11-01_SOUND_FULL.md`). Сверка «закрытых проблем» будет вестись по существующему SOUND-аудиту.

---

## 2. Фактическая карта слоёв по коду + расхождения с docs/ARCHITECTURE.md

### 2.1 Слои (по коду, не по docs)

```
startup.S (VBAR=0x40000000, banked-стеки, NEON, TTBR0→0x4A40C000, BSS=0,
           копия L1 U-Boot + DCCIMVAC, cpu1_entry/cpu2_entry)
  └─ main.c: uart→led→h3_hs_timer→h3_de2_init(1024×600@51.2М)→fb→usb_kbd_init→
     usb_hc_init/usb_msd (источник FAT: USB-flash или SD)→fat_init→remap_load→
     mmu_mark_uncached(.coherent)→clean+invalidate .coherent→
     h3_cpu_start(2, cpu2_entry)→i2s_init→i2s_test_tone→sega_pad_init→
     h3_cpu_start(1, cpu1_entry)→[меню: menu_run/settings/help/about]→
     rom_browser_run / emu_run_* 
  ├─ emu.c: emu_prepare (gb_heap_reset|keep, newlib_heap_reset, cheats_reset,
  │  emu_period_us=16667, i2s_ring_reset, stats/active, emu_clear_fb)
  │  → emu_run_* → emu_throttle (адаптивно от i2s_ring_fill) → emu_scale(320,240)
  ├─ i2s.c: i2s_init (PLL 24.576, gate+RST3 0x2D0 bit12, mux, делители, FIFO_CTL,
  │  TXIM=0, TXTL=64) → кольцо 4096 пар в SRAM A1 (0x1000/0x3000), индексы
  │  SRAM 0x98/0x9C, флаги 0xA0..0xBC, счётчики 0xAC..0xC0;
  │  cpu2_audio_entry — вечный активный цикл i2s_poll_fill (burst 24, guard
  │  TXE_CNT>=8, DC-блокер α=2^-shift, dsb/dmb)
  ├─ tft_drv.c — CPU1: ILI9486 SPI0 + TSC2046I, почта SRAM 0x34..0x8C
  ├─ usb_ohci/usb_kbd/usb_msd/usb_hc — OHCI ED/TD/HCCA в .coherent (uncached)
  ├─ fat/sd/rom_browser — FAT32 через blk_read
  ├─ wifi_net/wifi_layer/wpa_crypto/net_tcp/net_ftp — SDIO WiFi + TCP/FTP
  └─ c64_host/c64_fs/a8_host/a8_fs/cpc_host/cpc_fs/fuse_host/pce_host/bk_host/… — host-слои ядер (S8)
```

### 2.2 Расхождения код vs docs/ARCHITECTURE.md (проверенные)

| Утверждение docs/ARCHITECTURE.md | Код | Статус |
|---|---|---|
| «Кольцо звука — в SRAM A1 (r957): L=0x1000, R=0x3000, 4096 пар» | `i2s.c:157-160` — так; но `linker.ld:81-86` ПО-ПРЕЖНЕМУ резервирует `.ring_buf 0x4A418000` c ASSERT, и ни один объект не кладёт данные в `.ring_buf` (grep: секция не используется); `startup.S:487-497` зануляет «кольцо 32000 пар» в пустой секции | **DOC_LIE (P3, класс S0)**: секция-фантом: ASSERT `≤0x4A500000` и zero-init работают вхолостую; объём секции 0; комментарий «32000 пар» противоречит факту 4096 пар (r957) |
| «sbrk-арена (newlib) 0x4A400000+1МБ..0x4F000000» (док.: стр.444,396) | `libc_min.c:191-199`: SBRK_START = `_gb_heap_end` = 0x4A700000+24МБ = **0x4BF00000**; SBRK_LIMIT = **0x4EF00000** | **опровергнуто** (обе строки). Доп.: `linker.ld:99-100` — TODO «sbrk … 0x4A500000 — пересмотреть позже» устарел |
| «Куча эмуляторов 0x4A700000..0x4BF00000», «арена меню 0x4F000000» | `linker.ld:92-103` | подтверждено кодом |
| `_hend` — конец образа/BSS | `linker.ld:62` (сразу после `.noinit_fbn`) | подтверждено |
| Стеки core0: SVC 0x60000000, IRQ 0x5FF03000, FIQ 0x5FF02000, ABT 0x5FF01000, UND 0x5FF00000 | `startup.S:352-380` | подтверждено |
| Стеки CPU1: 0x5FDFD000..0x5FE01000 | `startup.S:608-636` (SVC 0x5FE01000, FIQ 0x5FE00000, IRQ 0x5FDFF000, ABT 0x5FDFE000, UND 0x5FDFD000) | подтверждено |
| MMU остаётся от U-Boot, .coherent → uncached | `startup.S:392-417` (копия L1 в 0x4A40C000, DCCIMVAC, TTBR0), `h3.c:38-74` | подтверждено |
| CPU2 — вечный цикл долива (не wfi) | `i2s.c:1172-1210` | подтверждено |
| «.coherent ≈ 9.3 КБ» (док.:397) | будет проверено по `nm` в S2 (цифра из старого билда) | не проверялось |

---

## 3. Карта памяти и стеков (по linker.ld/startup.S, с арифметикой)

| Зона | Адрес | Размер (арифметика) | Владелец | Примечание |
|---|---|---|---|---|
| Образ | 0x40000000 | до `_hend` | все | .text→.init_array→.rodata→.extab/exidx→.data→.bss→.noinit_fbn |
| `.noinit_fbn` | сразу после .bss | 96 МБ (NOLOAD) | FBNeo пул | не обнуляется |
| `.libh3_coherent` | 0x4A400000 (NOLOAD) | ASSERT `_coherent_end ≤ 0x4A40C000` | core0+CPU2 | uncached; L1-таблица 0x4A40C000..0x4A410000 |
| `.ring_buf` | 0x4A418000 (NOLOAD) | ASSERT ≤ 0x4A500000 | — | **пустая секция-фантом** (см. 2.2) |
| `.gb_heap` | 0x4A700000 | 24 МБ → 0x4BF00000 | все ядра | NOLOAD; `gb_heap_reset()` |
| sbrk | 0x4BF00000 (=_gb_heap_end) | до 0x4EF00000 (SBRK_LIMIT, ≈19 МБ) | newlib malloc | `libc_min.c:194-199` |
| `_menu_arena` | 0x4F000000 | 1 МБ резерв (граница sbrk) | меню | — |
| ROM_BUF | 0x50000000 | 24 МБ → 0x51800000 | браузер ROM | аркады переиспользуют как zip-буфер |
| `.vice_bss` | 0x52000000 | 31 МБ → 0x53F00000 | VICE | NOLOAD, zero-init startup.S |
| zip-зона FBNeo | 0x5C000000..0x5F000000 | 48 МБ | аркады | стык встык с `.a8_bss` |
| `.a8_bss` | 0x5F000000 | ≈1.84 МБ → 0x5F1D7310 | Atari800 | NOLOAD, zero-init |
| FB_BACK (dbl-buf) | 0x5F400000 | 2.46 МБ → 0x5F660000 | bk/vecx | за `CONFIG_HDMI_DOUBLE_BUF=0` |
| EMU_FB | 0x5F800000 | 320×240×2 = 150 КБ | все эмуляторы | → 0x5F825800 |
| HDMI FB | 0x5F900000 | 1024×600×4 = 2.46 МБ → 0x5FB58000 | DE2 UI | — |

**Стеки:**
- core0: FIQ 0x5FF02000, IRQ 0x5FF03000, ABT 0x5FF01000, UND 0x5FF00000, SVC 0x60000000 (вниз).
- CPU1: SVC 0x5FE01000, FIQ 0x5FE00000, IRQ 0x5FDFF000, ABT 0x5FDFE000, UND 0x5FDFD000; свободно от конца HDMI FB (0x5FB58000) ≈ 2.6 МБ.
- CPU2: SVC 0x5FC01000, FIQ 0x5FC00000, IRQ 0x5FBFF000, ABT 0x5FBFE000, UND 0x5FBFD000.

**Проверка непересечения (арифметика):**
- .coherent конец (≤0x4A40C000) < L1 (0x4A40C000..0x4A410000) < .ring_buf (пусто) < sbrk 0x4BF00000; свободно 0x4A500000..0x4BF00000 (≈9.5 МБ).
- sbrk конец 0x4EF00000 < _menu_arena 0x4F000000 (зазор 1 МБ) < ROM_BUF 0x50000000.
- ROM_BUF конец 0x51800000 < .vice_bss 0x52000000 (зазор 0x800000 = 8 МБ) < zip 0x5C000000.
  - **DOC_LIE**: комментарий `linker.ld:116-121` «0x51800000..0x52000000 свободно ~0.5 МБ — мало» — фактически зазор 8 МБ.
- .a8_bss конец 0x5F1D7310 < EMU_FB 0x5F800000; EMU_FB конец 0x5F825800 < HDMI FB 0x5F900000.

**Фактические опровержения гипотезы п.3 промта:**
- «ring_buf 0x4A418000 (кольцо звука)» — опровергнуто на практике: кольцо в SRAM A1 (i2s.c:157-160), секция пуста (состояние до r957).
- «Индексы кольца/флаги в .coherent» — опровергнуто: SRAM A1 (i2s.c:137-146).

---

## 4. P0-гипотезы (≤10; статус: гипотезы до прохождения слоёв)

**H1. SRAM A1 — единая карта занятости отсутствует.** `i2s.c:137-146,157-160,200-203,674-675`; `tft_drv.c:197-215`; `startup.S:63-65,303-316,582-587`; `h3_smp.c:24-32`; `main.c:584,620-621`. Инвариант: SRAM A1 = 24 КБ; карта размазана по 6 файлам, пересечения проверяются вручную. Последствие: тихая коллизия при добавлении флага → «звук рвётся»/«CPU2 завис». Проверка: статическая сводка всех SRAM-адресов. Пока пересечений не доказано (кольцо 0x1000..0x5000, почта 0x18..0xC0, диаг 0x880..0x8A8 — разведены).

**H2. `i2s_push_block` при n ≥ ёмкости кольца.** `i2s.c:412-448`: `want` клампится к 3200, но цикл `while (written < n)` не клампится — при большом n и живом CPU2 прокрутка полного кольца за вызов; `g_drop_cnt += n - written` может раздуть статистику. Последствие: статистика/чтение за концом буфера у вызывающего (см. H3). Проверка: статический анализ всех вызовов `i2s_push_block`.

**H3. Trim-ветка `_sbrk(incr<=0)`** `libc_min.c:207-221` — предварительный разбор: возвращает `cur`, при incr<0 уводит `g_brk` ниже старта только с клампом; sign-extension `incr` в uintptr_t в ветке `>0` не используется. Вероятно корректно; проверка вызовов free()-путей newlib в S2.

**H4. `emu_scale`/`emu_scale_int` границы.** `emu.c:61-117,123-174`: `dst_y` может быть отрицательным при `src_h*mul > 600` (mul = min(1024/src_w, 600/src_h) — для внутренних кадров ≤600 не отрицателен; для Lynx 160×102 mul=5 → dst_h=510, dst_y=45). Проверка всех вызовов в S6.

**H5.** Внутренний `static int sx_tab[FB_W]` — только core0, гонки нет. Снято.

**H6. `.coherent` против ASSERT 0x4A40C000.** `usb_ohci.c:107-338` + linker.ld:73. ARCHITECTURE: «≈9.3 КБ». Проверка: сборка + nm/size (S0/S2). Переполнение → ошибка линковки (не тихая).

**H7. `mmu_mark_uncached` покрывает 1 МБ (0x4A400000..0x4A500000) — uncached навсегда.** `h3.c:38-74`, `main.c:525`. sbrk (0x4BF00000) не попадает; L1-таблица (0x4A40C000) uncached — корректно. Сводится к P3/perf (1 МБ не кэшируется).

**H8. `emu_throttle` арифметика elapsed/debt.** `emu.c:274-358`: `now - emu_ts0 + (uint32_t)emu_debt` — отрицательный debt → огромный elapsed → ветка «переработка»; комментарий r944 утверждает, что баг закрыт. Проверка арифметики в S6/S7.

**H9. `i2s_ring_wait` при мёртвом CPU2** — есть таймаут 50 мс + rd-стоп 25 мс (`i2s.c:343-367`). Снято как P0.

**H10. Гонка `TFT_HELP_EPOCH`/`TFT_SET_*` на старте CPU1.** `main.c:591` (`tft_help_show(NULL)` до готовности CPU1); `tft_drv.c:203-215`. Поведение определено (CPU1 читает после старта), но гонку проверить в S3.

Статически проверяемы: H1-H4, H6-H8, H10. Только стенд: реальная когерентность SRAM/DRAM (r928/r957), тайминги.

---

## 5. Порядок работ S0..S9 и что нужно от владельца

1. **S0+S1+S2** — сборка (фактический `make clean && make -j8` в MSYS2, linkcheck, nm/size/readelf), старт/платформа (MMU/кэши, VBAR CPU1/2, регистры против даташита V1.2), память (бюджет + закрытие H1-H3, H6-H7). **Остановка: утверждение дельт.**
2. **S3+S4+S5** — конкурентность (граф разделяемых ресурсов), ввод, хранилище/сеть. **Остановка.**
3. **S6+S7+S9** — видео/меню/UI, аудио, наблюдаемость. **Остановка.**
4. **S8** группами: gba_sp/gpgx/snes/cps1/vice → fuse/atari800/cap32/pce_fast/fceumm → msx/gearcoleco/lynx/ngp/bk → ms1504/portfolio/a5200/a7800/vecx/mcume/gameboy. **Остановка после группы.**

Артефакты: `MEMORY_MAP.md` (после S0-S2), `SHARED_RESOURCES.md` (после S3), `PORTING_CONTRACTS.md` (после S8).

**От владельца нужно:**
- подтвердить отсутствие/ненужность `docs/AUDIT_2026-10-29_r848_FIXPLAN.md`;
- разрешение на `make clean && make -j8` (меняет только `build/`) и чтение `build/h3_bare.elf` nm/objdump/readelf;
- для S7 — доступ к UART-логу стенда (AUDIO-END и т.д.) для формулировки NEEDS_RUNTIME-проверок;
- для S9 — фиксированы ли `-DNDEBUG`/отсутствие assert.

---

## 6. Вопросы владельцу (без ответов аудит неполон)

1. Версия на стенде — именно r1008 (свежий билд)? NEEDS_RUNTIME-выводы привязаны к коду.
2. `g_emu_keep_heap` (A8): осознанный компромисс («растёт до 24 МБ между запусками») — подтверждаете?
3. CPU1 (TFT) работает всегда или только при подключённом TFT?
4. USB-клавиатура и Sega-пад одновременно — штатная конфигурация стенда?
5. WiFi/FTP реально используется на стенде (модуль RTL8189 есть)?
6. «Аркады и C64 параллельно не работают» — подтверждаете, что одновременный запуск невозможен через меню (влияет на переиспользование .vice_bss/zip-зоны)?
7. `--allow-multiple-definition`: есть ли список «легальных» дублей кроме whitelist-а linkcheck?
8. Какие машины считаются эталонными для проверки самодостаточности сборки (правило AGENTS.md)?

---

## Приложение. Ссылки на ключевые строки

- `h3_bare/src/main.c:431,439` — версия r1008
- `h3_bare/src/main.c:525` — mmu_mark_uncached(.coherent)
- `h3_bare/platform/linker.ld:62-127` — _hend/.coherent/.ring_buf/.gb_heap/_menu_arena/.a8_bss/.vice_bss
- `h3_bare/platform/startup.S:352-497` — стеки, BSS=0, zero-init .a8_bss/.vice_bss/.coherent/.ring_buf
- `h3_bare/src/libc_min.c:191-221` — sbrk (0x4BF00000..0x4EF00000)
- `h3_bare/cores/i2s.c:137-160,200-203,412-448,572-669,1165-1210` — SRAM-кольцо, CPU2
- `h3_bare/cores/emu.c:199-221,274-358` — emu_prepare, throttle