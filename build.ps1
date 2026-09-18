# build.ps1 — полная пересборка pico-retro-new-gen (PowerShell, без make)
# Учитывает все ядра: MCUME, A7800, A5200, Portfolio, GB, Lynx, NGP, GBA,
# MSX, NES(FCEUmm), SNES(Snes9x), MD/SMS(GPGX), читы, sega_pad.
#
# ЗАПУСК:   .\build.ps1          (или двойной клик build_windows.bat)
# ТРЕБУЕТ:  arm-none-eabi тулчейн. Ищется автоматически:
#           1) в PATH (command -v / where)
#           2) в C:\ARM\gcc-arm-none-eabi-*\bin
#           3) в других типовых местах
# Если не найден — печатает понятную инструкцию и выходит.
$TOP = "$PSScriptRoot"
$BUILD = "$TOP\build"
$BIN = "$BUILD\h3_bare.bin"
$ELF = "$BUILD\h3_bare.elf"

# ---- Автоопределение тулчейна ----
$CC0 = $null
# 1) PATH
$p = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
if ($p) { $CC0 = Split-Path $p.Source }
# 2) C:\ARM\gcc-arm-none-eabi-*
if (-not $CC0) {
    $d = Get-ChildItem "C:\ARM" -Directory -Filter "gcc-arm-none-eabi*" -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
    if ($d) { $CC0 = Join-Path $d.FullName "bin" }
}
# 3) другие типовые пути
if (-not $CC0) {
    foreach ($cand in @("C:\Program Files (x86)\GNU Arm Embedded Toolchain\*\bin",
                        "C:\Program Files\GNU Arm Embedded Toolchain\*\bin",
                        "C:\gcc-arm-none-eabi\bin",
                        "C:\tools\gcc-arm-none-eabi\bin")) {
        $m = Get-ChildItem $cand -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($m) { $CC0 = $m.FullName; break }
    }
}
if (-not $CC0 -or -not (Test-Path (Join-Path $CC0 "arm-none-eabi-gcc.exe"))) {
    Write-Host ""
    Write-Host "ОШИБКА: ARM-тулчейн не найден." -ForegroundColor Red
    Write-Host "Скачай и распакуй: https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads" -ForegroundColor Yellow
    Write-Host "пример: gcc-arm-none-eabi-13.3-2024.08 (Windows x86_64)."
    Write-Host "Затем положи в C:\ARM\ (чтобы был C:\ARM\gcc-arm-none-eabi-...\bin\arm-none-eabi-gcc.exe)"
    Write-Host "или добавь папку bin в системный PATH."
    exit 1
}
$CC = Join-Path $CC0 "arm-none-eabi-gcc.exe"
$CXX = Join-Path $CC0 "arm-none-eabi-g++.exe"
$OBJCOPY = Join-Path $CC0 "arm-none-eabi-objcopy.exe"
Write-Host "Тулчейн: $CC0" -ForegroundColor Cyan

$CFLAGS = @("-mcpu=cortex-a7","-mfpu=neon","-mfloat-abi=softfp","-marm","-ffreestanding","-Wall","-Wextra","-O2","-DORANGE_PI_ONE","-DALLWINNER_BARE_METAL","-DNDEBUG")
$CXXFLAGS = $CFLAGS + @("-fno-exceptions","-fno-rtti","-fno-threadsafe-statics")
$INC = @("-I$TOP\h3_bare\include","-I$TOP\h3_bare\cores","-I$TOP\h3_bare\src","-I$TOP\h3_bare\platform\fb")

function ok { if ($LASTEXITCODE -ne 0) { Write-Host "FAIL: $($args[0])"; exit 1 } else { Write-Host "  $($args[0])" } }

Write-Host "Очистка build/..." -ForegroundColor Yellow
Remove-Item "$BUILD\*.o" -Force -ErrorAction SilentlyContinue

Write-Host "=== Assembler ==="
& $CC @CFLAGS "-xassembler-with-cpp" -c -o "$BUILD\startup.o" "$TOP\h3_bare\platform\startup.S"; ok "startup"

Write-Host "=== Core ==="
foreach ($f in @("menu","rom_browser","cheatdb","settings","sd","fat","usb_ohci","usb_kbd","sega_pad","fb_text","led","emu")) {
    & $CC @CFLAGS @INC -c -o "$BUILD\$f.o" "$TOP\h3_bare\cores\$f.c"; ok $f
}

Write-Host "=== MCUME ==="
& $CXX @CXXFLAGS @INC "-I$TOP\h3_bare\cores\mcume" -c -o "$BUILD\system_atari_h3.o" "$TOP\h3_bare\cores\system_atari_h3.cpp"; ok "a2600_host"
foreach ($f in @("Vcsemu","Vmachine","Raster","Table","Display","Collision","Tiasound","Options","Keyboard","Exmacro")) { & $CC @CFLAGS @INC "-I$TOP\h3_bare\cores\mcume" "-std=gnu89" "-O0" -c -o "$BUILD\mcume_$f.o" "$TOP\h3_bare\cores\mcume\$f.c"; ok "mcume_$f" }
foreach ($f in @("Cpu","Memory")) { & $CC @CFLAGS @INC "-I$TOP\h3_bare\cores\mcume" "-std=gnu89" "-O2" -c -o "$BUILD\mcume_$f.o" "$TOP\h3_bare\cores\mcume\$f.c"; ok "mcume_$f" }

Write-Host "=== A7800 ==="
& $CXX @CXXFLAGS @INC "-I$TOP\h3_bare\cores\a7800" -c -o "$BUILD\system_a7800_h3.o" "$TOP\h3_bare\cores\system_a7800_h3.cpp"; ok "a7800_host"
foreach ($f in @("ProSystem","Sally","Maria","Memory","Cartridge","Pokey","Riot","Tia","Region","Bios","Palette")) { & $CXX @CXXFLAGS @INC "-I$TOP\h3_bare\cores\a7800" -c -o "$BUILD\a7800_$f.o" "$TOP\h3_bare\cores\a7800\$f.cpp"; ok "a7800_$f" }

Write-Host "=== A5200 ==="
& $CXX @CXXFLAGS @INC "-I$TOP\h3_bare\cores\a5200" -c -o "$BUILD\system_a5200_h3.o" "$TOP\h3_bare\cores\system_a5200_h3.cpp"; ok "a5200_host"
& $CC @CFLAGS @INC "-I$TOP\h3_bare\cores\a5200" "-std=gnu11" -c -o "$BUILD\a5200_atari5200.o" "$TOP\h3_bare\cores\a5200\atari5200.c"; ok "a5200_atari5200"
foreach ($f in @("antic","cpu","crc32","gtia","pokey","pokeysnd")) { & $CC @CFLAGS @INC "-I$TOP\h3_bare\cores\a5200" "-std=gnu89" -c -o "$BUILD\a5200_$f.o" "$TOP\h3_bare\cores\a5200\$f.c"; ok "a5200_$f" }

Write-Host "=== Portfolio ==="
foreach ($f in @("system_portfolio","cpu","i8253","i8259")) { & $CXX @CXXFLAGS @INC "-I$TOP\h3_bare\cores\portfolio" -c -o "$BUILD\portfolio_$f.o" "$TOP\h3_bare\cores\portfolio\$f.cpp"; ok "portfolio_$f" }

Write-Host "=== Game Boy ==="
Set-Content -Path "$BUILD\gb_pri.h" -Value @'
#define PRIu64 "llu"
#define PRIx64 "llx"
#define PRId64 "lld"
'@
& $CXX @CXXFLAGS @INC -c -o "$BUILD\gameboy_host.o" "$TOP\h3_bare\cores\gameboy_host.cpp"; ok "gb_host"
& $CC @CFLAGS @INC -c -o "$BUILD\gameboy_stubs.o" "$TOP\h3_bare\cores\gameboy_stubs.c"; ok "gb_stubs"
foreach ($f in @("emulator","memory","joypad")) { & $CC @($CFLAGS + @("-std=gnu99","-include","$BUILD\gb_pri.h")) @INC -c -o "$BUILD\gb_$f.o" "$TOP\h3_bare\cores\gameboy\$f.c"; ok "gb_$f" }

Write-Host "=== Lynx ==="
$lynxInc = $INC + @("-I$TOP\h3_bare\cores\lynx","-I$TOP\h3_bare\cores\lynx\blip")
& $CXX @CXXFLAGS @lynxInc -c -o "$BUILD\lynx_host.o" "$TOP\h3_bare\cores\lynx_host.cpp"; ok "lynx_host"
foreach ($f in @("system","mikie","susie","cart","memmap","eeprom","rom","ram","lynxdec")) { & $CXX @CXXFLAGS @lynxInc -c -o "$BUILD\lynx_$f.o" "$TOP\h3_bare\cores\lynx\$f.cpp"; ok "lynx_$f" }
& $CXX @CXXFLAGS @lynxInc -c -o "$BUILD\lynx_blip_buffer.o" "$TOP\h3_bare\cores\lynx\blip\Blip_Buffer.cpp"
& $CXX @CXXFLAGS @lynxInc -c -o "$BUILD\lynx_blip_stereo.o" "$TOP\h3_bare\cores\lynx\blip\Stereo_Buffer.cpp"
ok "lynx_blip"

Write-Host "=== NGP ==="
$ngpInc = $INC + @("-I$TOP\h3_bare\cores\ngp")
foreach ($f in @("ngp_host","main","memory","graphics","tlcs900h","z80","flash","neopopsound","sound","ngpBios","input")) { & $CXX @CXXFLAGS @ngpInc -c -o "$BUILD\ngp_$f.o" "$TOP\h3_bare\cores\ngp\$f.cpp"; ok "ngp_$f" }

Write-Host "=== GBA ==="
$GBA_SP = "$TOP\h3_bare\cores\gba_sp"
$gbaInc = $INC + @("-I$GBA_SP")
$GBA_CFLAGS = $CFLAGS + @("-DINLINE=inline")
$GBA_RENAME = @("--redefine-sym=vram=gpsp_vram","--redefine-sym=reg=gpsp_reg","--redefine-sym=cheats=gpsp_cheats","--redefine-sym=init_memory=gpsp_init_memory","--redefine-sym=init_cpu=gpsp_init_cpu","--redefine-sym=load_bios=gpsp_load_bios")
& $CC @GBA_CFLAGS @gbaInc -c -o "$BUILD\gba_host.o" "$TOP\h3_bare\cores\gba_host.c"; ok "gba_host"
& $CC @GBA_CFLAGS @gbaInc -c -o "$BUILD\gba_compat.o" "$GBA_SP\gba_compat.c"; ok "gba_compat"
& $CC @GBA_CFLAGS "-I$GBA_SP" "-xassembler-with-cpp" -c -o "$BUILD\gba_bios_data.o" "$GBA_SP\bios_data.S"; ok "gba_bios"
foreach ($f in @("main","gba_memory","sound","gba_cc_lut","gbp","cheats","savestate","serial","serial_proto","rfu")) { & $CC @GBA_CFLAGS @gbaInc -c -o "$BUILD\gba_$f.o.tmp" "$GBA_SP\$f.c"; & $OBJCOPY @GBA_RENAME "$BUILD\gba_$f.o.tmp" "$BUILD\gba_$f.o"; Remove-Item "$BUILD\gba_$f.o.tmp" -ErrorAction SilentlyContinue; ok "gba_$f" }
foreach ($f in @("cpu","video")) { & $CXX @CXXFLAGS @GBA_CFLAGS @gbaInc -fno-exceptions -fno-rtti -c -o "$BUILD\gba_$f.o.tmp" "$GBA_SP\$f.cc"; & $OBJCOPY @GBA_RENAME "$BUILD\gba_$f.o.tmp" "$BUILD\gba_$f.o"; Remove-Item "$BUILD\gba_$f.o.tmp" -ErrorAction SilentlyContinue; ok "gba_$f" }

Write-Host "=== MSX ==="
$MSX = "$TOP\h3_bare\cores\msx"
$msxInc = $INC + @("-I$MSX","-I$MSX\host","-I$MSX\host\include","-I$MSX\fMSX","-I$MSX\Z80","-I$MSX\EMULib","-I$MSX\NukeYKT")
$MSX_CFLAGS = $CFLAGS + @("-DLSB_FIRST","-D__C99__","-DINLINE=inline") + $msxInc
$MSX_RENAME = @("--redefine-sym=CPU=msx_CPU","--redefine-sym=RAM=msx_RAM","--redefine-sym=LoadROM=msx_LoadROM","--redefine-sym=rfopen=msx_rfopen","--redefine-sym=rfclose=msx_rfclose","--redefine-sym=rfread=msx_rfread","--redefine-sym=rfwrite=msx_rfwrite","--redefine-sym=rfseek=msx_rfseek","--redefine-sym=rftell=msx_rftell","--redefine-sym=rfgets=msx_rfgets","--redefine-sym=rfeof=msx_rfeof","--redefine-sym=rfgetc=msx_rfgetc","--redefine-sym=rfputc=msx_rfputc","--redefine-sym=filestream_rewind=msx_filestream_rewind","--redefine-sym=sscanf=msx_sscanf","--redefine-sym=time=msx_time","--redefine-sym=localtime=msx_localtime","--redefine-sym=strlcpy=msx_strlcpy","--redefine-sym=fill_pathname_join=msx_fill_pathname_join","--redefine-sym=strcasestr=msx_strcasestr","--redefine-sym=chdir=msx_chdir","--redefine-sym=getcwd=msx_getcwd")
function cm($name, $path) { & $CC @MSX_CFLAGS @INC -c -o "$BUILD\$name.o.tmp" $path; & $OBJCOPY @MSX_RENAME "$BUILD\$name.o.tmp" "$BUILD\$name.o"; Remove-Item "$BUILD\$name.o.tmp" -ErrorAction SilentlyContinue; ok $name }
cm "msx_host" "$MSX\host\msx_host.c"
cm "msx_compat" "$MSX\host\msx_compat.c"
cm "msx_log" "$MSX\host\msx_log.c"
& $CC @CFLAGS -c -o "$BUILD\msx2_rom_data.o.tmp" "$MSX\host\msx2_rom_data.c"; & $OBJCOPY @MSX_RENAME "$BUILD\msx2_rom_data.o.tmp" "$BUILD\msx2_rom_data.o"; Remove-Item "$BUILD\msx2_rom_data.o.tmp" -ErrorAction SilentlyContinue; ok "msx2_rom"
& $CC @CFLAGS -c -o "$BUILD\msx2ext_rom_data.o.tmp" "$MSX\host\msx2ext_rom_data.c"; & $OBJCOPY @MSX_RENAME "$BUILD\msx2ext_rom_data.o.tmp" "$BUILD\msx2ext_rom_data.o"; Remove-Item "$BUILD\msx2ext_rom_data.o.tmp" -ErrorAction SilentlyContinue; ok "msx2ext_rom"
$fmsx = @(@{n="msx_MSX";p="$MSX\fMSX\MSX.c"},@{n="msx_V9938";p="$MSX\fMSX\V9938.c"},@{n="msx_Sound";p="$MSX\EMULib\Sound.c"},@{n="msx_SHA1";p="$MSX\EMULib\SHA1.c"},@{n="msx_Floppy";p="$MSX\EMULib\Floppy.c"},@{n="msx_FDIDisk";p="$MSX\EMULib\FDIDisk.c"},@{n="msx_MCF";p="$MSX\EMULib\MCF.c"},@{n="msx_Z80";p="$MSX\Z80\Z80.c"},@{n="msx_I8255";p="$MSX\EMULib\I8255.c"},@{n="msx_YM2413";p="$MSX\EMULib\YM2413.c"},@{n="msx_AY8910";p="$MSX\EMULib\AY8910.c"},@{n="msx_SCC";p="$MSX\EMULib\SCC.c"},@{n="msx_WD1793";p="$MSX\EMULib\WD1793.c"},@{n="msx_opll";p="$MSX\NukeYKT\opll.c"},@{n="msx_WrapNukeYKT";p="$MSX\NukeYKT\WrapNukeYKT.c"})
foreach ($m in $fmsx) { cm $m.n $m.p }

Write-Host "=== NES ==="
$FCEUMM = "$TOP\h3_bare\cores\fceumm"
$fceInc = $INC + @("-I$FCEUMM","-I$FCEUMM\inc","-I$FCEUMM\input","-I$FCEUMM\boards","-I$FCEUMM\palettes","-I$FCEUMM\fir")
$FCEUMM_CFLAGS = $CFLAGS + @("-DFRONTEND_SUPPORTS_RGB565","-DFCEU_VERSION_NUMERIC=9900")
& $CXX @CXXFLAGS @fceInc -c -o "$BUILD\fceumm_host.o" "$FCEUMM\nes_host_fceumm.cpp"; ok "fceumm_host"
foreach ($f in @("fceu","x6502","ppu","sound","cart","ines","input","fds","fds_apu","palette","video","file","general","state","crc32","md5","fceu-endian","fceu-memory","cheat","filter","libretro_compat","vsuni","unif")) { & $CC @FCEUMM_CFLAGS @fceInc -c -o "$BUILD\fceumm_$f.o" "$FCEUMM\$f.c"; ok "fceumm_$f" }
foreach ($f in Get-ChildItem "$FCEUMM\input\*.c") { $fn = [System.IO.Path]::GetFileNameWithoutExtension($f.Name); & $CC @FCEUMM_CFLAGS @fceInc -c -o "$BUILD\fceumm_in_$fn.o" $f.FullName; ok "fceumm_in_$fn" }
foreach ($f in Get-ChildItem "$FCEUMM\boards\*.c") { $fn = [System.IO.Path]::GetFileNameWithoutExtension($f.Name); & $CC @FCEUMM_CFLAGS @fceInc -c -o "$BUILD\fceumm_b_$fn.o" $f.FullName; ok "fceumm_b_$fn" }

Write-Host "=== SNES ==="
$SNES = "$TOP\h3_bare\cores\snes"
$snesInc = $INC + @("-I$SNES","-I$SNES\libretro-common\include")
$SNES_CFLAGS = $CFLAGS + @("-DLOAD_FROM_MEMORY","-DHAVE_NO_LANGEXTRA","-DLAGFIX","-Wno-incompatible-pointer-types")
& $CXX @CXXFLAGS @SNES_CFLAGS @snesInc -c -o "$BUILD\snes_host.o" "$TOP\h3_bare\cores\snes_host.cpp"; ok "snes_host"
& $CC @CFLAGS @snesInc -c -o "$BUILD\snes_compat.o" "$TOP\h3_bare\cores\snes_compat.c"; ok "snes_compat"
foreach ($f in @("c4","c4emu","cheats2","cheats","clip","cpu","cpuexec","cpuops","data","dma","dsp1","fxemu","fxinst","gfx","getset","globals","memmap","obc1","ppu","sa1","sa1cpu","sdd1","sdd1emu","seta010","seta011","seta018","seta","spc7110","spc7110dec","srtc","tile","apu","soundux","spc700")) { & $CC @SNES_CFLAGS @snesInc -c -o "$BUILD\snes_$f.o" "$SNES\$f.c"; ok "snes_$f" }
foreach ($f in Get-ChildItem "$BUILD\snes_*.o") { & $OBJCOPY "--redefine-sym=PPU=snes_PPU" $f.FullName "$($f.FullName).tmp" 2>$null; if (Test-Path "$($f.FullName).tmp") { Move-Item "$($f.FullName).tmp" $f.FullName -Force } }
ok "snes_PPU_rename"

Write-Host "=== GPGX ==="
$GPGX = "$TOP\h3_bare\cores\gpgx"
$gpgxInc = $INC + @("-I$GPGX\core","-I$GPGX\libretro_inc","-I$GPGX\core\m68k","-I$GPGX\core\z80","-I$GPGX\core\ntsc","-I$GPGX\core\sound","-I$GPGX\core\input_hw","-I$GPGX\core\cart_hw","-I$GPGX\core\cart_hw\svp","-I$GPGX\core\cd_hw")
$GPGX_CFLAGS = $CFLAGS + @("-DLSB_FIRST","-DBYTE_ORDER=LITTLE_ENDIAN","-DMAXROMSIZE=16777216","-DUSE_16BPP_RENDERING")
foreach ($f in @(@{n="gpgx_host";p="$GPGX\system_gpgx_h3.c"},@{n="gpgx_mathx";p="$GPGX\gpgx_math.c"},@{n="gpgx_missing";p="$GPGX\gpgx_missing.c"},@{n="gp_cheats";p="$TOP\h3_bare\cores\gp_cheats.c"})) { & $CC @GPGX_CFLAGS @gpgxInc -c -o "$BUILD\$($f.n).o" $f.p; ok $f.n }
foreach ($f in Get-ChildItem "$GPGX\core\*.c") { $fn = [System.IO.Path]::GetFileNameWithoutExtension($f.Name); & $CC @GPGX_CFLAGS @gpgxInc -c -o "$BUILD\gpgx_core_$fn.o" $f.FullName; ok "gpgx_core_$fn" }
foreach ($sd in @("z80","m68k","ntsc","sound","input_hw","cart_hw","cd_hw")) { foreach ($f in Get-ChildItem "$GPGX\core\$sd\*.c") { $fn = [System.IO.Path]::GetFileNameWithoutExtension($f.Name); & $CC @GPGX_CFLAGS @gpgxInc -c -o "$BUILD\gpgx_${sd}_$fn.o" $f.FullName; ok "gpgx_${sd}_$fn" } }
foreach ($f in Get-ChildItem "$GPGX\core\cart_hw\svp\*.c") { $fn = [System.IO.Path]::GetFileNameWithoutExtension($f.Name); & $CC @GPGX_CFLAGS @gpgxInc -c -o "$BUILD\gpgx_svp_$fn.o" $f.FullName; ok "gpgx_svp_$fn" }

Write-Host "=== Platform ==="
foreach ($f in @("uart","printf","libc_min","main")) { & $CC @CFLAGS @INC -c -o "$BUILD\$f.o" "$TOP\h3_bare\src\$f.c"; ok $f }
& $CXX @CXXFLAGS @INC -c -o "$BUILD\cxx_runtime.o" "$TOP\h3_bare\src\cxx_runtime.cpp"; ok "cxx_runtime"
foreach ($f in @("udelay","h3_hs_timer","h3_ccu","h3")) { & $CC @CFLAGS @INC -c -o "$BUILD\$f.o" "$TOP\h3_bare\platform\$f.c"; ok $f }
foreach ($f in @("h3_de2","h3_hdmi","dw_hdmi","h3_lcd")) { & $CC @CFLAGS @INC -c -o "$BUILD\$f.o" "$TOP\h3_bare\platform\fb\$f.c"; ok $f }

Write-Host "=== Link ==="
& $CXX "-T$TOP\h3_bare\platform\linker.ld" -nostdlib "-Wl,-gc-sections" -o "$ELF" "$BUILD\*.o" -lgcc -lc -lm -lgcc 2>&1
if ($LASTEXITCODE -ne 0) { Write-Host "LINK FAILED"; exit 1 }
Write-Host "LINK OK"

& $OBJCOPY "-O" "binary" "$ELF" "$BIN"
Copy-Item "$BIN" "$TOP\h3_bare.bin" -Force
$sz = (Get-Item "$BIN").Length
Write-Host "=== DONE: $sz bytes ==="