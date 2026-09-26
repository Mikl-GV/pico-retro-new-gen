# Makefile — параллельная сборка мультисистемного эмулятора H3 (замена build.sh).
# Использование:
#   make            — собрать build/h3_bare.bin (+ копию в корень h3_bare.bin)
#   make -j$(nproc) — параллельно, на многоядерной машине ~15 сек
#   make clean      — удалить build/
#   make sd         — собрать SD-образ build/h3_bare.img
#   make fel        — залить через sunxi-fel
#   make help       — справка
#
# Поведение идентично старому build.sh, но объекты считаются один раз:
# файл перекомпилируется только если изменился исходник/заголовки/флаги.

TOP      := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD    := $(TOP)/build
BIN      := $(BUILD)/h3_bare.bin
ELF      := $(BUILD)/h3_bare.elf
IMG      := $(BUILD)/h3_bare.img

# Дефолтная цель — сборка прошивки (не первый попавшийся каталог)
.DEFAULT_GOAL := all

# ---- Тулчейн ----
PREFIX := $(if $(shell command -v arm-none-eabi-gcc 2>/dev/null),arm-none-eabi-,arm-linux-gnueabihf-)
CC   := $(PREFIX)gcc
CXX  := $(PREFIX)g++
AS   := $(PREFIX)gcc
LD   := $(PREFIX)g++
OBJCOPY := $(PREFIX)objcopy

# ---- Общие флаги ----
CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm
CFLAGS += -ffreestanding -Wall -Wextra -O2 -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG
INCLUDES := -I$(TOP)h3_bare/include -I$(TOP)h3_bare/cores \
	-I$(TOP)h3_bare/cores/gpgx/core -I$(TOP)h3_bare/cores/gpgx/libretro_inc \
	-I$(TOP)h3_bare/cores/gpgx/core/z80 -I$(TOP)h3_bare/cores/gpgx/core/m68k \
	-I$(TOP)h3_bare/cores/gpgx/core/ntsc -I$(TOP)h3_bare/cores/gpgx/core/sound \
	-I$(TOP)h3_bare/cores/gpgx/core/input_hw -I$(TOP)h3_bare/cores/gpgx/core/cart_hw \
	-I$(TOP)h3_bare/cores/gpgx/core/cart_hw/svp -I$(TOP)h3_bare/cores/gpgx/core/cd_hw \
	-I$(TOP)h3_bare/cores/fceumm -I$(TOP)h3_bare/cores/fceumm/inc \
	-I$(TOP)h3_bare/cores/fceumm/input -I$(TOP)h3_bare/cores/fceumm/boards \
	-I$(TOP)h3_bare/cores/fceumm/palettes -I$(TOP)h3_bare/cores/fceumm/fir \
	-I$(TOP)h3_bare/cores/mcume -I$(TOP)h3_bare/cores/a7800 -I$(TOP)h3_bare/cores/a5200 \
	-I$(TOP)h3_bare/cores/gameboy -I$(TOP)h3_bare/cores/portfolio -I$(TOP)h3_bare/cores/lynx \
	-I$(TOP)h3_bare/cores/ngp -I$(TOP)h3_bare/cores/gba_sp \
	-I$(TOP)h3_bare/src -I$(TOP)h3_bare/platform/fb
SNES_INCLUDES := -I$(TOP)h3_bare/cores/snes -I$(TOP)h3_bare/cores/snes/libretro-common/include $(INCLUDES)
CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics

# Пер-ядровые флаги
FCEUMM_CFLAGS := $(CFLAGS) -DFRONTEND_SUPPORTS_RGB565 -DFCEU_VERSION_NUMERIC=9900
GBFLAGS       := $(CFLAGS) -std=gnu99 -DPRIu64=\"llu\" -DPRIx64=\"llx\" -DPRId64=\"lld\"
SNES_CFLAGS   := $(CFLAGS) -DLOAD_FROM_MEMORY -DHAVE_NO_LANGEXTRA -DLAGFIX -Wno-incompatible-pointer-types
GPGX_CFLAGS   := $(CFLAGS) -DLSB_FIRST -DBYTE_ORDER=LITTLE_ENDIAN -DMAXROMSIZE=16777216 -DUSE_16BPP_RENDERING -DFRONTEND_SUPPORTS_RGB565

# ---- Авто-генерация списков объектов ----
OBJ  := $(BUILD)/startup.o
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,menu rom_browser settings sd fat usb_ohci usb_kbd fb_text led emu cheatdb sega_pad remap i2s tft_drv))
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,system_atari_h3 system_a7800_h3 system_a5200_h3 gameboy_host gameboy_stubs lynx_host snes_host snes_compat gpgx_host gpgx_mathx gpgx_missing gp_cheats))
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,gba_host gba_compat gba_main gba_gba_memory gba_sound gba_gba_cc_lut gba_gbp gba_cheats gba_cpu gba_video gba_savestate gba_serial gba_serial_proto gba_rfu gba_bios_data))
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,portfolio_system portfolio_cpu portfolio_i8253 portfolio_i8259))
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,uart printf libc_min main cxx_runtime udelay h3_hs_timer h3_ccu h3 h3_smp h3_de2 h3_hdmi dw_hdmi h3_lcd))

# MCUME
MCUME := $(TOP)h3_bare/cores/mcume
OBJ  += $(foreach fn,Vcsemu Vmachine Raster Table Display Collision Tiasound Options Keyboard Exmacro,$(BUILD)/mcume_$(fn).o)
OBJ  += $(foreach fn,Cpu Memory,$(BUILD)/mcume_$(fn).o)

# A7800
A7800 := $(TOP)h3_bare/cores/a7800
OBJ  += $(foreach fn,ProSystem Sally Maria Memory Cartridge Pokey Riot Tia Region Bios Palette,$(BUILD)/a7800_$(fn).o)

# A5200
A5200 := $(TOP)h3_bare/cores/a5200
OBJ  += $(addprefix $(BUILD)/,a5200_atari5200.o)
OBJ  += $(foreach fn,antic cpu crc32 gtia pokey pokeysnd,$(BUILD)/a5200_$(fn).o)

# Game Boy
GB := $(TOP)h3_bare/cores/gameboy
OBJ  += $(foreach fn,emulator memory joypad,$(BUILD)/gb_$(fn).o)

# Lynx
LYNX := $(TOP)h3_bare/cores/lynx
OBJ  += $(foreach fn,system mikie susie cart memmap eeprom rom ram lynxdec,$(BUILD)/lynx_$(fn).o)
OBJ  += $(addprefix $(BUILD)/,lynx_blip_buffer.o lynx_blip_stereo.o)

# NGP
NGP := $(TOP)h3_bare/cores/ngp
OBJ  += $(addprefix $(BUILD)/,ngp_host.o ngp_main.o ngp_memory.o ngp_graphics.o ngp_tlcs900h.o ngp_z80.o ngp_flash.o ngp_neopopsound.o ngp_sound.o ngp_ngpBios.o ngp_input.o)

# Vectrex (vecx)
VECX := $(TOP)h3_bare/cores/vecx
VECX_INC := -I$(VECX)
# e6809.c/vecx.c ждут INLINE (как в libretro Makefile.common: -DINLINE=inline)
VECX_CFLAGS := $(CFLAGS) -DINLINE=inline
OBJ  += $(addprefix $(BUILD)/,vecx_host.o vecx_e6809.o vecx_vecx.o vecx_vecx_psg.o)
$(BUILD)/vecx_host.o: $(TOP)h3_bare/cores/vecx_host.c | $(BUILD)
	$(CC) $(VECX_CFLAGS) $(VECX_INC) $(INCLUDES) -c -o $@ $<
$(BUILD)/vecx_e6809.o: $(VECX)/e6809.c | $(BUILD)
	$(CC) $(VECX_CFLAGS) $(VECX_INC) -c -o $@ $<
$(BUILD)/vecx_vecx.o: $(VECX)/vecx.c | $(BUILD)
	$(CC) $(VECX_CFLAGS) $(VECX_INC) -c -o $@ $<
$(BUILD)/vecx_vecx_psg.o: $(VECX)/vecx_psg.c | $(BUILD)
	$(CC) $(VECX_CFLAGS) $(VECX_INC) -c -o $@ $<

# Gearcoleco (ColecoVision)
# Ядро Gearcoleco — C++ с std::string/vector (только эпизодически: пути/
# SaveState/breakpoints — не hot-path). Компилируется БЕЗ -ffreestanding,
# чтобы newlib дал C++ STL. Линкуется через -lstdc++ (см. линковку).
# miniz тянет fopen/stat — отключаем MINIZ_NO_STDIO (ROM подаём буфером,
# zip не используем); MINIZ_NO_TIME — убрать utime.
COL := $(TOP)h3_bare/cores/gearcoleco
COL_SRC := $(COL)/src
COL_INC := -I$(COL_SRC)
COL_CXXFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm
COL_CXXFLAGS += -Wall -Wextra -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics
# Дизассемблер/трейс-логирование Gearcoleco ОТКЛЮЧЕНО: LogInstructionEvent()
# вызывается на КАЖДУЮ инструкцию и выделяет record из bump-кучи
# (GetOrCreateDisassemblerRecord) — пул 24 МБ за секунды исчерпывается и
# Coleco «зависает»/падает. Для игры трейс не нужен.
COL_CXXFLAGS += -DGEARCOLECO_DISABLE_DISASSEMBLER
# Файлы ядра (все, кроме miniz — он C)
COL_CORESRC := GearcolecoCore Memory Processor TMS9918A Audio AY8910 Input ColecoVisionIOPorts opcodes opcodes_cb opcodes_ed TraceLogger VgmRecorder Adam AdamMedia AdamNet F18A F18A_enhancements F18AGPU Cartridge Mapper
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,coleco_host coleco_compat))
OBJ  += $(addprefix $(BUILD)/,$(foreach fn,$(COL_CORESRC),gc_$(fn).o))
OBJ  += $(addprefix $(BUILD)/,gc_Blip_Buffer.o gc_Effects_Buffer.o gc_Multi_Buffer.o gc_Sms_Apu.o gc_miniz.o)
$(BUILD)/coleco_host.o: $(TOP)h3_bare/cores/coleco_host.cpp | $(BUILD)
	$(CXX) $(COL_CXXFLAGS) $(COL_INC) $(INCLUDES) -c -o $@ $<
$(BUILD)/coleco_compat.o: $(TOP)h3_bare/cores/coleco_compat.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gc_%.o: $(TOP)h3_bare/cores/gearcoleco/src/%.cpp | $(BUILD)
	$(CXX) $(COL_CXXFLAGS) $(COL_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/gc_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/gc_%.o: $(TOP)h3_bare/cores/gearcoleco/src/audio/%.cpp | $(BUILD)
	$(CXX) $(COL_CXXFLAGS) $(COL_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/gc_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/gc_miniz.o: $(TOP)h3_bare/cores/gearcoleco/src/miniz.c | $(BUILD)
	$(CC) $(CFLAGS) -DMINIZ_NO_STDIO -DMINIZ_NO_TIME $(COL_INC) -c -o $@ $<

# FCEUmm
FCEUMM := $(TOP)h3_bare/cores/fceumm
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,fceumm_host fceumm_fceu fceumm_x6502 fceumm_ppu fceumm_sound fceumm_cart fceumm_ines fceumm_input fceumm_fds fceumm_fds_apu fceumm_palette fceumm_video fceumm_file fceumm_general fceumm_state fceumm_crc32 fceumm_md5 fceumm_fceu-endian fceumm_fceu-memory fceumm_cheat fceumm_filter fceumm_libretro_compat fceumm_vsuni fceumm_unif))
OBJ  += $(patsubst $(FCEUMM)/input/%.c,$(BUILD)/fceumm_in_%.o,$(wildcard $(FCEUMM)/input/*.c))
OBJ  += $(patsubst $(FCEUMM)/boards/%.c,$(BUILD)/fceumm_b_%.o,$(wildcard $(FCEUMM)/boards/*.c))

# SNES (Snes9x 2005)
SNES := $(TOP)h3_bare/cores/snes
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,snes_c4 snes_c4emu snes_cheats2 snes_cheats snes_clip snes_cpu snes_cpuexec snes_cpuops snes_data snes_dma snes_dsp1 snes_fxemu snes_fxinst snes_gfx snes_getset snes_globals snes_memmap snes_obc1 snes_ppu snes_sa1 snes_sa1cpu snes_sdd1 snes_sdd1emu snes_seta010 snes_seta011 snes_seta018 snes_seta snes_spc7110 snes_spc7110dec snes_srtc snes_tile snes_apu snes_soundux snes_spc700))

# GPGX (Mega Drive / SMS)
GPGX := $(TOP)h3_bare/cores/gpgx
OBJ  += $(foreach f,$(wildcard $(GPGX)/core/*.c),$(BUILD)/gpgx_core_$(notdir $(f:.c=.o)))
OBJ  += $(foreach d,z80 m68k ntsc sound input_hw cart_hw cd_hw,$(foreach f,$(wildcard $(GPGX)/core/$(d)/*.c),$(BUILD)/gpgx_$(d)_$(notdir $(f:.c=.o))))
OBJ  += $(foreach f,$(wildcard $(GPGX)/core/cart_hw/svp/*.c),$(BUILD)/gpgx_svp_$(notdir $(f:.c=.o)))

# MSX (fMSX)
MSX := $(TOP)h3_bare/cores/msx
MSX_INC := -I$(MSX) -I$(MSX)/host -I$(MSX)/host/include -I$(MSX)/fMSX -I$(MSX)/Z80 -I$(MSX)/EMULib -I$(MSX)/NukeYKT
# fMSX: C-only, использует свой sscanf/strcasestr в msx_compat
MSX_CFLAGS := $(CFLAGS) -DLSB_FIRST -D__C99__ -DINLINE=inline $(MSX_INC)
# objcopy-переименование конфликтующих символов MSX (как GBA_RENAME/PPU для SNES),
# чтобы не сталкиваться с CPU/RAM (Snes9x), LoadROM (Snes9x), rf* (GPGX),
# sscanf/time/localtime/strlcpy/fill_pathname_join (FCEUmm/GBA compat).
MSX_RENAME := $(OBJCOPY) \
	--redefine-sym CPU=msx_CPU \
	--redefine-sym RAM=msx_RAM \
	--redefine-sym LoadROM=msx_LoadROM \
	--redefine-sym rfopen=msx_rfopen \
	--redefine-sym rfclose=msx_rfclose \
	--redefine-sym rfread=msx_rfread \
	--redefine-sym rfwrite=msx_rfwrite \
	--redefine-sym rfseek=msx_rfseek \
	--redefine-sym rftell=msx_rftell \
	--redefine-sym rfgets=msx_rfgets \
	--redefine-sym rfeof=msx_rfeof \
	--redefine-sym rfgetc=msx_rfgetc \
	--redefine-sym rfputc=msx_rfputc \
	--redefine-sym filestream_rewind=msx_filestream_rewind \
	--redefine-sym sscanf=msx_sscanf \
	--redefine-sym time=msx_time \
	--redefine-sym localtime=msx_localtime \
	--redefine-sym strlcpy=msx_strlcpy \
	--redefine-sym fill_pathname_join=msx_fill_pathname_join \
	--redefine-sym strcasestr=msx_strcasestr \
	--redefine-sym chdir=msx_chdir \
	--redefine-sym getcwd=msx_getcwd
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,msx_host msx_compat msx_log msx2_rom_data msx2ext_rom_data))
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,msx_MSX msx_V9938 msx_Sound msx_SHA1 msx_Floppy msx_FDIDisk msx_MCF msx_Z80 msx_I8255 msx_YM2413 msx_AY8910 msx_SCC msx_WD1793 msx_opll msx_WrapNukeYKT))

$(BUILD)/msx_host.o: $(MSX)/host/msx_host.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_compat.o: $(MSX)/host/msx_compat.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_log.o: $(MSX)/host/msx_log.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx2_rom_data.o: $(MSX)/host/msx2_rom_data.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx2ext_rom_data.o: $(MSX)/host/msx2ext_rom_data.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_MSX.o: $(MSX)/fMSX/MSX.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_V9938.o: $(MSX)/fMSX/V9938.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_Sound.o: $(MSX)/EMULib/Sound.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_SHA1.o: $(MSX)/EMULib/SHA1.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_Floppy.o: $(MSX)/EMULib/Floppy.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_FDIDisk.o: $(MSX)/EMULib/FDIDisk.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_MCF.o: $(MSX)/EMULib/MCF.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_Z80.o: $(MSX)/Z80/Z80.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_I8255.o: $(MSX)/EMULib/I8255.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_YM2413.o: $(MSX)/EMULib/YM2413.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_AY8910.o: $(MSX)/EMULib/AY8910.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_SCC.o: $(MSX)/EMULib/SCC.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_WD1793.o: $(MSX)/EMULib/WD1793.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_opll.o: $(MSX)/NukeYKT/opll.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_WrapNukeYKT.o: $(MSX)/NukeYKT/WrapNukeYKT.c | $(BUILD)
	$(CC) $(MSX_CFLAGS) $(INCLUDES) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp

# ---- Правила компиляции ----
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/startup.o: $(TOP)h3_bare/platform/startup.S | $(BUILD)
	$(AS) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

$(BUILD)/%.o: $(TOP)h3_bare/src/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD)/%.o: $(TOP)h3_bare/src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD)/%.o: $(TOP)h3_bare/platform/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD)/%.o: $(TOP)h3_bare/platform/fb/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# ---- cores/*.c, cores/*.cpp (общие правила по каталогам) ----
$(BUILD)/menu.o: $(TOP)h3_bare/cores/menu.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/rom_browser.o: $(TOP)h3_bare/cores/rom_browser.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/cheatdb.o: $(TOP)h3_bare/cores/cheatdb.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/sega_pad.o: $(TOP)h3_bare/cores/sega_pad.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/settings.o: $(TOP)h3_bare/cores/settings.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/sd.o: $(TOP)h3_bare/cores/sd.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/fat.o: $(TOP)h3_bare/cores/fat.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/usb_ohci.o: $(TOP)h3_bare/cores/usb_ohci.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/usb_kbd.o: $(TOP)h3_bare/cores/usb_kbd.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/fb_text.o: $(TOP)h3_bare/cores/fb_text.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/led.o: $(TOP)h3_bare/cores/led.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/emu.o: $(TOP)h3_bare/cores/emu.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/remap.o: $(TOP)h3_bare/cores/remap.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/i2s.o: $(TOP)h3_bare/cores/i2s.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/tft_drv.o: $(TOP)h3_bare/cores/tft_drv.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<   # r58: вернул -O2 (r57 -O0 тормозил дисплей)

$(BUILD)/system_atari_h3.o: $(TOP)h3_bare/cores/system_atari_h3.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/system_a7800_h3.o: $(TOP)h3_bare/cores/system_a7800_h3.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/system_a5200_h3.o: $(TOP)h3_bare/cores/system_a5200_h3.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gameboy_host.o: $(TOP)h3_bare/cores/gameboy_host.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gameboy_stubs.o: $(TOP)h3_bare/cores/gameboy_stubs.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/lynx_host.o: $(TOP)h3_bare/cores/lynx_host.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/snes_host.o: $(TOP)h3_bare/cores/snes_host.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(SNES_CFLAGS) $(SNES_INCLUDES) -c -o $@ $<
$(BUILD)/snes_compat.o: $(TOP)h3_bare/cores/snes_compat.c | $(BUILD)
	$(CC) $(CFLAGS) $(SNES_INCLUDES) -c -o $@ $<
$(BUILD)/fceumm_host.o: $(TOP)h3_bare/cores/fceumm/nes_host_fceumm.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_host.o: $(TOP)h3_bare/cores/gpgx/system_gpgx_h3.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_mathx.o: $(TOP)h3_bare/cores/gpgx/gpgx_math.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_missing.o: $(TOP)h3_bare/cores/gpgx/gpgx_missing.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gp_cheats.o: $(TOP)h3_bare/cores/gp_cheats.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD)/mcume_%.o: $(TOP)h3_bare/cores/mcume/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -std=gnu89 -O0 -c -o $@ $<
$(BUILD)/mcume_Cpu.o: $(TOP)h3_bare/cores/mcume/Cpu.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -std=gnu89 -O2 -c -o $@ $<
$(BUILD)/mcume_Memory.o: $(TOP)h3_bare/cores/mcume/Memory.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -std=gnu89 -O2 -c -o $@ $<

$(BUILD)/a7800_%.o: $(TOP)h3_bare/cores/a7800/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD)/a5200_atari5200.o: $(TOP)h3_bare/cores/a5200/atari5200.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -std=gnu11 -c -o $@ $<
$(BUILD)/a5200_%.o: $(TOP)h3_bare/cores/a5200/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -std=gnu89 -c -o $@ $<

$(BUILD)/portfolio_system.o: $(TOP)h3_bare/cores/portfolio/system_portfolio.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/portfolio_%.o: $(TOP)h3_bare/cores/portfolio/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD)/gb_%.o: $(TOP)h3_bare/cores/gameboy/%.c | $(BUILD)
	$(CC) $(GBFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD)/lynx_%.o: $(TOP)h3_bare/cores/lynx/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/lynx_blip_buffer.o: $(TOP)h3_bare/cores/lynx/blip/Blip_Buffer.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fhosted -c -o $@ $<
$(BUILD)/lynx_blip_stereo.o: $(TOP)h3_bare/cores/lynx/blip/Stereo_Buffer.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fhosted -c -o $@ $<

$(BUILD)/ngp_host.o: $(TOP)h3_bare/cores/ngp/ngp_host.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_main.o: $(TOP)h3_bare/cores/ngp/main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_memory.o: $(TOP)h3_bare/cores/ngp/memory.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_graphics.o: $(TOP)h3_bare/cores/ngp/graphics.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_tlcs900h.o: $(TOP)h3_bare/cores/ngp/tlcs900h.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_z80.o: $(TOP)h3_bare/cores/ngp/z80.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_flash.o: $(TOP)h3_bare/cores/ngp/flash.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_neopopsound.o: $(TOP)h3_bare/cores/ngp/neopopsound.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_sound.o: $(TOP)h3_bare/cores/ngp/sound.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_ngpBios.o: $(TOP)h3_bare/cores/ngp/ngpBios.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_input.o: $(TOP)h3_bare/cores/ngp/input.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

# ---- GBA (gpSP) ----
GBA_CFLAGS := $(CFLAGS) -DINLINE=inline
GBA_INC    := $(INCLUDES)
# Имена, конфликтующие с другими ядрами (fceumm/gpgx), переименовываем
# ОДИНАКОВО во всех gpsp-объектах (и определения, и ссылки).
GBA_RENAME := --redefine-sym vram=gpsp_vram \
              --redefine-sym reg=gpsp_reg \
              --redefine-sym cheats=gpsp_cheats \
              --redefine-sym init_memory=gpsp_init_memory \
              --redefine-sym init_cpu=gpsp_init_cpu \
              --redefine-sym load_bios=gpsp_load_bios

$(BUILD)/gba_host.o: $(TOP)h3_bare/cores/gba_host.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@ $<
$(BUILD)/gba_compat.o: $(TOP)h3_bare/cores/gba_sp/gba_compat.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@ $<
$(BUILD)/gba_bios_data.o: $(TOP)h3_bare/cores/gba_sp/bios_data.S | $(BUILD)
	$(AS) $(GBA_CFLAGS) -I$(TOP)h3_bare/cores/gba_sp -x assembler-with-cpp -c -o $@ $<

# --- ядро gpsp: компилим, затем применяем GBA_RENAME ко всем .o ---
$(BUILD)/gba_main.o: $(TOP)h3_bare/cores/gba_sp/main.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_gba_memory.o: $(TOP)h3_bare/cores/gba_sp/gba_memory.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_sound.o: $(TOP)h3_bare/cores/gba_sp/sound.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_gba_cc_lut.o: $(TOP)h3_bare/cores/gba_sp/gba_cc_lut.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_gbp.o: $(TOP)h3_bare/cores/gba_sp/gbp.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_cheats.o: $(TOP)h3_bare/cores/gba_sp/cheats.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_savestate.o: $(TOP)h3_bare/cores/gba_sp/savestate.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_serial.o: $(TOP)h3_bare/cores/gba_sp/serial.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_serial_proto.o: $(TOP)h3_bare/cores/gba_sp/serial_proto.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_rfu.o: $(TOP)h3_bare/cores/gba_sp/rfu.c | $(BUILD)
	$(CC) $(GBA_CFLAGS) $(GBA_INC) -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_cpu.o: $(TOP)h3_bare/cores/gba_sp/cpu.cc | $(BUILD)
	$(CXX) $(CXXFLAGS) $(GBA_CFLAGS) $(GBA_INC) -fno-exceptions -fno-rtti -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/gba_video.o: $(TOP)h3_bare/cores/gba_sp/video.cc | $(BUILD)
	$(CXX) $(CXXFLAGS) $(GBA_CFLAGS) $(GBA_INC) -fno-exceptions -fno-rtti -c -o $@.tmp $<
	$(OBJCOPY) $(GBA_RENAME) $@.tmp $@; rm -f $@.tmp

$(BUILD)/fceumm_%.o: $(TOP)h3_bare/cores/fceumm/%.c | $(BUILD)
	$(CC) $(FCEUMM_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/fceumm_in_%.o: $(TOP)h3_bare/cores/fceumm/input/%.c | $(BUILD)
	$(CC) $(FCEUMM_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/fceumm_b_%.o: $(TOP)h3_bare/cores/fceumm/boards/%.c | $(BUILD)
	$(CC) $(FCEUMM_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/fceumm_libretro_compat.o: $(TOP)h3_bare/cores/fceumm/libretro_compat.c | $(BUILD)
	$(CC) $(FCEUMM_CFLAGS) $(INCLUDES) -c -o $@ $<

# SNES: PPU дублируется между FCEUmm и Snes9x — переименовываем во ВСЕХ snes_*.o
# (build.sh применял objcopy --redefine-sym PPU=snes_PPU ко всем объектам SNES),
# иначе другие файлы ядра остаются со ссылками на общий PPU.
$(BUILD)/snes_%.o: $(TOP)h3_bare/cores/snes/%.c | $(BUILD)
	$(CC) $(SNES_CFLAGS) $(SNES_INCLUDES) -c -o $@.tmp $<
	$(OBJCOPY) --redefine-sym PPU=snes_PPU $@.tmp $@
	rm -f $@.tmp

# ---- GPGX (Mega Drive / SMS) — паттерн-правила по подкаталогам ----
$(BUILD)/gpgx_core_%.o: $(TOP)h3_bare/cores/gpgx/core/%.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_z80_%.o: $(TOP)h3_bare/cores/gpgx/core/z80/%.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_m68k_%.o: $(TOP)h3_bare/cores/gpgx/core/m68k/%.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_ntsc_%.o: $(TOP)h3_bare/cores/gpgx/core/ntsc/%.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_sound_%.o: $(TOP)h3_bare/cores/gpgx/core/sound/%.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_input_hw_%.o: $(TOP)h3_bare/cores/gpgx/core/input_hw/%.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_cart_hw_%.o: $(TOP)h3_bare/cores/gpgx/core/cart_hw/%.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_cd_hw_%.o: $(TOP)h3_bare/cores/gpgx/core/cd_hw/%.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/gpgx_svp_%.o: $(TOP)h3_bare/cores/gpgx/core/cart_hw/svp/%.c | $(BUILD)
	$(CC) $(GPGX_CFLAGS) $(INCLUDES) -c -o $@ $<

# ---- Линковка ----
# На Linux объекты передаются напрямую (cmdline лимит не проблема).
# Под Windows (MSYS2) команда длиннее лимита (~32K): линкуем через
# response-файл linker.rsp (cygpath -m даёт Windows-пути для линкера).
$(ELF): $(OBJ) $(TOP)h3_bare/platform/linker.ld
	@printf '%s\n' $(foreach o,$(OBJ),$(subst /,\/,$(shell cygpath -m $(o)))) > $(BUILD)/linker.rsp
	printf -- '-lstdc++ -lgcc -lc -lm -lgcc\n' >> $(BUILD)/linker.rsp
	$(LD) -T $(TOP)h3_bare/platform/linker.ld -nostdlib -Wl,-gc-sections \
	    -Wl,--allow-multiple-definition \
	    -o $@ @$(BUILD)/linker.rsp

$(BIN): $(ELF)
	$(OBJCOPY) -O binary $(ELF) $@
	cp -f $@ $(TOP)h3_bare.bin
	@echo "--- h3_bare.bin: $$(stat -c%s $@) байт (build/ и корень) ---"

.PHONY: all clean sd fel help
all: $(BIN)

clean:
	rm -rf $(BUILD) $(TOP)h3_bare.bin

# ---- SD-образ (опционально) ----
sd: $(BIN)
	@mkdir -p $(BUILD)/u-boot
	@if [ ! -f $(BUILD)/u-boot/u-boot-sunxi-with-spl.bin ]; then \
	    echo "Файл build/u-boot/u-boot-sunxi-with-spl.bin не найден."; \
	    echo "Соберите U-Boot вручную (см. docs/BUILD.md)."; \
	    exit 1; \
	fi
	@printf 'fatload mmc 0 0x40000000 h3_bare.bin\ngo 0x40000000\n' > $(BUILD)/boot.cmd
	@mkimage -A arm -T script -C none -n "pico-retro" -d $(BUILD)/boot.cmd $(BUILD)/boot.scr
	@dd if=/dev/zero bs=1M count=64 of=$(IMG) 2>/dev/null
	@dd if=$(BUILD)/u-boot/u-boot-sunxi-with-spl.bin of=$(IMG) bs=1k seek=8 conv=notrunc 2>/dev/null
	@dd if=/dev/zero bs=1M count=48 of=$(BUILD)/fatpart.bin 2>/dev/null
	@mkfs.vfat -F 32 -n H3_RETRO $(BUILD)/fatpart.bin >/dev/null 2>&1
	@export MTOOLS_SKIP_CHECK=1; \
	    mcopy -i $(BUILD)/fatpart.bin $(BUILD)/boot.scr ::boot.scr; \
	    mcopy -i $(BUILD)/fatpart.bin $(BIN) ::h3_bare.bin; \
	    mmd -i $(BUILD)/fatpart.bin ::roms; \
	    for d in $(TOP)roms/*/; do mmd -i $(BUILD)/fatpart.bin "::roms/$$(basename $$d)"; done; \
	    for f in $(TOP)roms/*/*; do [ -f "$$f" ] && mcopy -i $(BUILD)/fatpart.bin "$$f" "::roms/$$(basename $$(dirname $$f))/$$(basename $$f)"; done
	@dd if=$(BUILD)/fatpart.bin of=$(IMG) bs=1M seek=16 conv=notrunc 2>/dev/null
	@python3 -c 'import struct,sys; img=open(sys.argv[1],"r+b"); img.seek(446); img.write(b"\x00"*64); img.seek(446); img.write(b"\x80\x01\x01\x00\x0c\xfe\xff\xff"+struct.pack("<II",32768,98304)); img.seek(510); img.write(b"\x55\xaa"); img.close()' $(IMG)
	@rm -f $(BUILD)/fatpart.bin
	@echo "--- h3_bare.img: $$(stat -c%s $(IMG)) байт ---"

fel: $(BIN)
	sudo sunxi-fel write 0x40000000 $(BIN) execute 0x40000000

help:
	@echo "make            — собрать build/h3_bare.bin (+ копия в корень)"
	@echo "make -j\$$(nproc) — параллельная сборка (все ядра)"
	@echo "make clean      — удалить build/"
	@echo "make sd         — SD-образ (нужен U-Boot SPL)"
	@echo "make fel        — заливка через sunxi-fel"