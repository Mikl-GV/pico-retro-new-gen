# Makefile — параллельная сборка мультисистемного эмулятора H3 (замена build.sh).
# Использование:
#   make            — собрать build/h3_bare.bin (+ копию в корень h3_bare.bin)
#   make -j$(nproc) — параллельно, на многоядерной машине ~15 сек
#   make clean      — удалить build/
#   make sd         — собрать SD-образ build/h3_bare.img
#   make fel        — залить через sunxi-fel
#   make help       — справка
#
# Поведение идентично старому build.sh. Зависимости от заголовков НЕ
# отслеживаются (нет -MMD/-include *.d): после правки ЛЮБОГО .h нужен
# make clean && make, иначе объекты останутся со старыми константами.

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
OBJ  += $(BUILD)/coleco_bios.o


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

# ---- PC Engine / TurboGrafx (Beetle PCE Fast / mednafen_pce_fast, HuCard) ----
# Враппер (libretro.c) + движок vendored в h3_bare/cores/pce_fast/.
# Host-слой (pce_host.c) — наш thin libretro-frontend (видео/ввод, без звука).
# CD не поддерживается (pce_stubs.c глушит CD/libretro-common символы).
# Коллизия log_cb с MSX снята objcopy-переименованием в pce_log_cb.
PCE      := $(TOP)h3_bare/cores/pce_fast
PCE_INC  := -I$(PCE) -I$(PCE)/libretro_inc \
	-I$(PCE)/mednafen -I$(PCE)/mednafen/include -I$(PCE)/mednafen/hw_sound \
	-I$(PCE)/mednafen/hw_cpu -I$(PCE)/mednafen/hw_misc -I$(PCE)/mednafen/pce_fast
PCE_CFLAGS := $(CFLAGS) -DINLINE=inline -DMEDNAFEN_VERSION_NUMERIC=931 -DSTDC_HEADERS \
	-D__STDC_LIMIT_MACROS -D__LIBRETRO__ -D_LOW_ACCURACY_ -DFRONTEND_SUPPORTS_RGB565 \
	'-DPRId64="lld"' '-DPRIu64="llu"' '-DPRIx64="llx"' '-DPRIX64="llX"'
PCE_TOP  := general file settings state mempatcher okiadpcm cdstream mednafen-endian
PCE_PF   := huc6280 input psg vdc
PCE_OBJ  := $(addprefix $(BUILD)/pce_m_,$(addsuffix .o,$(PCE_TOP)))
PCE_OBJ  += $(addprefix $(BUILD)/pce_pf_,$(addsuffix .o,$(PCE_PF)))
PCE_OBJ  += $(BUILD)/pce_ac.o $(BUILD)/pce_blip.o $(BUILD)/pce_wrap.o $(BUILD)/pce_host.o $(BUILD)/pce_stubs.o
OBJ      += $(PCE_OBJ)
$(BUILD)/pce_m_%.o: $(PCE)/mednafen/%.c | $(BUILD)
	$(CC) $(PCE_CFLAGS) $(PCE_INC) $(INCLUDES) -c -o $@ $<
$(BUILD)/pce_pf_%.o: $(PCE)/mednafen/pce_fast/%.c | $(BUILD)
	$(CC) $(PCE_CFLAGS) $(PCE_INC) $(INCLUDES) -c -o $@ $<
$(BUILD)/pce_ac.o: $(PCE)/mednafen/hw_misc/arcade_card/arcade_card.c | $(BUILD)
	$(CC) $(PCE_CFLAGS) $(PCE_INC) $(INCLUDES) -c -o $@ $<
$(BUILD)/pce_blip.o: $(PCE)/mednafen/sound/Blip_Buffer.c | $(BUILD)
	$(CC) $(PCE_CFLAGS) $(PCE_INC) $(INCLUDES) -c -o $@ $<
$(BUILD)/pce_wrap.o: $(PCE)/libretro.c | $(BUILD)
	$(CC) $(PCE_CFLAGS) $(PCE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(OBJCOPY) --redefine-sym log_cb=pce_log_cb $@.tmp $@; rm -f $@.tmp
$(BUILD)/pce_host.o: $(TOP)h3_bare/cores/pce_host.c | $(BUILD)
	$(CC) $(PCE_CFLAGS) $(PCE_INC) $(INCLUDES) -c -o $@ $<
$(BUILD)/pce_stubs.o: $(TOP)h3_bare/cores/pce_stubs.c | $(BUILD)
	$(CC) $(PCE_CFLAGS) $(PCE_INC) $(INCLUDES) -c -o $@ $<
.PHONY: pce-obj
pce-obj: $(PCE_OBJ)

# ---- ZX Spectrum (Fuse / fuse-libretro) ----
# Vendored в h3_bare/cores/fuse/ (fuse + libspectrum + vendored libretro-common).
# libretro-API и libretro-common символы collide с PCE — objcopy переименовывает
# их в fuse_* (fuse_rename.sh) согласованно во всех fuse-объектах.
FUSE_ROOT := $(TOP)h3_bare/cores/fuse
FUSE_REL := bzip2/blocksort.c bzip2/bzlib.c bzip2/compress.c bzip2/crctable.c bzip2/decompress.c \
 bzip2/huffman.c bzip2/randtable.c deps/libretro-common/compat/compat_posix_string.c \
 deps/libretro-common/compat/compat_strcasestr.c deps/libretro-common/compat/compat_strl.c \
 deps/libretro-common/compat/fopen_utf8.c deps/libretro-common/encodings/encoding_utf.c \
 deps/libretro-common/file/file_path.c deps/libretro-common/file/file_path_io.c \
 deps/libretro-common/lists/string_list.c deps/libretro-common/streams/file_stream.c \
 deps/libretro-common/string/stdstring.c deps/libretro-common/time/rtime.c \
 deps/libretro-common/vfs/vfs_implementation.c fuse/debugger/breakpoint.c fuse/debugger/command.c \
 fuse/debugger/commandl.c fuse/debugger/commandy.c fuse/debugger/debugger.c \
 fuse/debugger/disassemble.c fuse/debugger/event.c fuse/debugger/expression.c \
 fuse/debugger/system_variable.c fuse/debugger/variable.c fuse/display.c fuse/event.c \
 fuse/infrastructure/startup_manager.c fuse/input.c fuse/keyboard.c fuse/loader.c fuse/machine.c \
 fuse/machines/machines_periph.c fuse/machines/pentagon1024.c fuse/machines/pentagon512.c \
 fuse/machines/pentagon.c fuse/machines/scorpion.c fuse/machines/spec128.c fuse/machines/spec16.c \
 fuse/machines/spec48.c fuse/machines/spec48_ntsc.c fuse/machines/specplus2a.c \
 fuse/machines/specplus2.c fuse/machines/specplus3.c fuse/machines/specplus3e.c \
 fuse/machines/spec_se.c fuse/machines/tc2048.c fuse/machines/tc2068.c fuse/machines/ts2068.c \
 fuse/memory_pages.c fuse/mempool.c fuse/module.c fuse/periph.c fuse/peripherals/ay.c \
 fuse/peripherals/covox.c fuse/peripherals/dck.c fuse/peripherals/disk/beta.c \
 fuse/peripherals/disk/crc.c fuse/peripherals/disk/didaktik.c fuse/peripherals/disk/disciple.c \
 fuse/peripherals/disk/disk.c fuse/peripherals/disk/fdd.c fuse/peripherals/disk/opus.c \
 fuse/peripherals/disk/plusd.c fuse/peripherals/disk/trdos.c fuse/peripherals/disk/upd_fdc.c \
 fuse/peripherals/disk/wd_fdc.c fuse/peripherals/flash/am29f010.c fuse/peripherals/fuller.c \
 fuse/peripherals/ide/divide.c fuse/peripherals/ide/divmmc.c fuse/peripherals/ide/divxxx.c \
 fuse/peripherals/ide/ide.c fuse/peripherals/ide/simpleide.c fuse/peripherals/ide/zxatasp.c \
 fuse/peripherals/ide/zxcf.c fuse/peripherals/ide/zxmmc.c fuse/peripherals/if1.c \
 fuse/peripherals/if2.c fuse/peripherals/joystick.c fuse/peripherals/kempmouse.c \
 fuse/peripherals/melodik.c fuse/peripherals/multiface.c fuse/peripherals/printer.c \
 fuse/peripherals/scld.c fuse/peripherals/sound/sp0256.c fuse/peripherals/sound/uspeech.c \
 fuse/peripherals/speccyboot.c fuse/peripherals/specdrum.c fuse/peripherals/spectranet.c \
 fuse/peripherals/ttx2000s.c fuse/peripherals/ula.c fuse/peripherals/usource.c \
 fuse/phantom_typist.c fuse/pokefinder/pokefinder.c fuse/pokefinder/pokemem.c fuse/psg.c \
 fuse/rectangle.c fuse/rzx.c fuse/slt.c fuse/sound/blipbuffer.c fuse/sound.c fuse/spectrum.c \
 fuse/svg.c fuse/tape.c fuse/timer/native.c fuse/timer/timer.c fuse/uidisplay.c fuse/uimedia.c \
 fuse/unittests/unittests.c fuse/z80/z80.c fuse/z80/z80_debugger_variables.c fuse/z80/z80_ops.c \
 libspectrum/buffer.c libspectrum/bzip2.c libspectrum/creator.c libspectrum/crypto.c \
 libspectrum/csw.c libspectrum/dck.c libspectrum/ide.c libspectrum/libspectrum.c \
 libspectrum/memory.c libspectrum/microdrive.c libspectrum/mmc.c libspectrum/myglib/garray.c \
 libspectrum/myglib/ghash.c libspectrum/myglib/gslist.c libspectrum/plusd.c libspectrum/pzx_read.c \
 libspectrum/rzx.c libspectrum/sna.c libspectrum/snap_accessors.c libspectrum/snapshot.c \
 libspectrum/snp.c libspectrum/sp.c libspectrum/symbol_table.c libspectrum/szx.c libspectrum/tap.c \
 libspectrum/tape_accessors.c libspectrum/tape_block.c libspectrum/tape.c libspectrum/tape_set.c \
 libspectrum/timings.c libspectrum/tzx_read.c libspectrum/tzx_write.c libspectrum/utilities.c \
 libspectrum/warajevo_read.c libspectrum/wav.c libspectrum/z80.c libspectrum/z80em.c \
 libspectrum/zip.c libspectrum/zlib.c libspectrum/zxs.c src/compat/dir.c src/compat/display.c \
 src/compat/fat.c src/compat/file.c src/compat/joystick.c src/compat/keyboard.c src/compat/mouse.c \
 src/compat/osname.c src/compat/paths.c src/compat/sound.c src/compat/timer.c src/compat/ui.c \
 src/coreopt.c src/fuse/fuse.c src/fuse/options_enumerate.c src/fuse/settings.c src/fuse/snapshot.c \
 src/fuse/ui.c src/fuse/utils.c src/libretro.c src/missing.c src/version.c zlib/adler32.c \
 zlib/compress.c zlib/crc32.c zlib/deflate.c zlib/gzclose.c zlib/gzlib.c zlib/gzread.c \
 zlib/gzwrite.c zlib/infback.c zlib/inffast.c zlib/inflate.c zlib/inftrees.c zlib/trees.c \
 zlib/uncompr.c zlib/zutil.c
FUSE_OBJ  := $(foreach f,$(FUSE_REL),$(BUILD)/fz_$(subst /,_,$(f)).o)
FUSE_OBJ  += $(BUILD)/fuse_host.o $(BUILD)/fuse_stubs.o
OBJ       += $(FUSE_OBJ)
FUSE_INC  := -I$(FUSE_ROOT) -I$(FUSE_ROOT)/fuse -I$(FUSE_ROOT)/libspectrum \
    -I$(FUSE_ROOT)/src -I$(FUSE_ROOT)/deps/libretro-common/include \
    -I$(FUSE_ROOT)/zlib -I$(FUSE_ROOT)/bzip2 -I$(FUSE_ROOT)/shim
FUSEFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm -ffreestanding \
    -Wall -O1 -fno-strict-aliasing -fwrapv -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
    -D__GNU_LIBRARY__ '-DPRId64="lld"' '-DPRIu64="llu"' '-DPRIx64="llx"' \
    '-DPRIX64="llX"' '-DPRIuPTR="u"'
include $(TOP)h3_bare/cores/fuse/fuse_rules.mk
$(BUILD)/fuse_host.o: $(TOP)h3_bare/cores/fuse_host.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@ $<
$(BUILD)/fuse_stubs.o: $(TOP)h3_bare/cores/fuse_stubs.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@ $<

# ---- BK-0010/0011M (libretro-bk / BK-Terak-Emu, PDP-11) ----
# Вшитые ROM — bk_roms.c (генерируется из открытого набора). API ядра plain
# retro_* конфликтует с PCE → bk_rename.sh переименовывает в bk_retro_*.
BK_ROOT := $(TOP)h3_bare/cores/bk
BK_REL := access.c boot.c branch.c covox.c double.c ea.c itab.c libretro.c service.c timer.c \
 tape.c disk.c mouse.c single.c weird.c tty.c io.c timing.c sound.c terakdisk.c synth.c \
 emu2149.c main.c scr-libretro.c scr.c tty-libretro.c joystick.c bk_roms.c
BK_OBJ := $(foreach f,$(BK_REL),$(BUILD)/bk_$(f).o)
OBJ += $(BK_OBJ) $(BUILD)/bk_host.o
BK_INC := -I$(BK_ROOT)
BKFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm -ffreestanding \
    -Wall -O2 -fno-strict-aliasing -fwrapv -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG -DLIBRETRO -DINLINE=inline
include $(TOP)h3_bare/cores/bk/bk_rules.mk
$(BUILD)/bk_host.o: $(TOP)h3_bare/cores/bk_host.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@ $<

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

# ---- CPS-1 (FinalBurn Neo, Capcom Play System 1) ----
# Vendored в h3_bare/cores/cps1/ (не libretro-ядро, а «нативный» FBNeo:
# host сам выбирает драйвер и крутит BurnDrvFrame). Собственные флаги
# из VENDOR.md: -D__fastcall= -DLSB_FIRST=1 -DEMU_M68K -DFBNEO_DEBUG.
# fm.c/ay8910.c/ym2151.c компилируются КАК C (gcc) — иначе имена C++
# и линковка рассыпается. m68kops.c/h генерируются нативным m68kmake.
# Конфликтующие с другими ядрами символы (m68k*/YM2612*) переименовываются
# cps1_rename.sh (см. заметки про BK/PCE).
CPS1 := $(TOP)h3_bare/cores/cps1
CPS1_ZLIB := $(TOP)h3_bare/cores/fuse/zlib
CPS1_INC := -I$(CPS1)/src -I$(CPS1)/src/burn -I$(CPS1)/src/burn/devices \
	-I$(CPS1)/src/burn/snd -I$(CPS1)/src/burn/drv/capcom \
	-I$(CPS1)/src/cpu -I$(CPS1)/src/cpu/m68k -I$(CPS1)/src/cpu/z80 -I$(CPS1)/src/intf/cd \
	-I$(BUILD) -I$(CPS1_ZLIB) $(INCLUDES)
CPS1_CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm -ffreestanding \
	-Wall -Wextra -O2 -fno-strict-aliasing \
	-DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
	-D__fastcall= -DLSB_FIRST=1 -DEMU_M68K -DFBNEO_DEBUG
# C++-файлы вендора БЕЗ -ffreestanding (как Gearcoleco): newlib freestanding
# не даёт <cmath>/tr1/free, на что опирается FBNeo.
CPS1_CXXFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm \
	-Wall -Wextra -O2 -fno-strict-aliasing \
	-DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
	-D__fastcall= -DLSB_FIRST=1 -DEMU_M68K -DFBNEO_DEBUG \
	-fno-exceptions -fno-rtti -fno-threadsafe-statics

CPS1_BURN  := burn burn_bitmap burn_gun burn_led burn_memory burn_pal burn_sha1 burn_shift burn_sound cheat hiscore load tilemap_generic tiles_generic timer
CPS1_CAP   := cps cps2_crpt cps_config cps_draw cps_mem cps_obj cps_pal cps_run cps_rw cps_scr cpsr cpsrd cpst ctv d_cps1 d_cps2 fcrash_snd kabuki ps ps_m ps_z qs qs_c qs_z sf2mdt_snd
CPS1_DEV   := eeprom i2ceeprom timekpr resnet nmk112 watchdog
CPS1_SND   := burn_ym2151 burn_ym2203 msm5205 msm6295 samples
CPS1_SNDC  := ay8910 fm ym2151 fmopl
CPS1_Z80   := z80 z80ctc z80daisy z80pio

OBJ += $(addprefix $(BUILD)/c1b_,$(addsuffix .o,$(CPS1_BURN)))
OBJ += $(addprefix $(BUILD)/c1c_,$(addsuffix .o,$(CPS1_CAP)))
OBJ += $(addprefix $(BUILD)/c1d_,$(addsuffix .o,$(CPS1_DEV)))
OBJ += $(addprefix $(BUILD)/c1s_,$(addsuffix .o,$(CPS1_SND)))
OBJ += $(addprefix $(BUILD)/c1sc_,$(addsuffix .o,$(CPS1_SNDC)))
OBJ += $(addprefix $(BUILD)/c1z_,$(addsuffix .o,$(CPS1_Z80)))
OBJ += $(BUILD)/c1m_m68kcpu.o $(BUILD)/c1m_m68kdasm.o $(BUILD)/c1m_m68kops.o
OBJ += $(BUILD)/c1i_m68000_intf.o $(BUILD)/c1i_z80_intf.o
OBJ += $(BUILD)/c1x_stubs.o $(BUILD)/c1x_netg.o $(BUILD)/c1x_neostubs.o $(BUILD)/cps1_host.o

# Только объекты CPS-1 (для отладки цели cps1-obj)
CPS1_OBJ := $(addprefix $(BUILD)/c1b_,$(addsuffix .o,$(CPS1_BURN))) \
	$(addprefix $(BUILD)/c1c_,$(addsuffix .o,$(CPS1_CAP))) \
	$(addprefix $(BUILD)/c1d_,$(addsuffix .o,$(CPS1_DEV))) \
	$(addprefix $(BUILD)/c1s_,$(addsuffix .o,$(CPS1_SND))) \
	$(addprefix $(BUILD)/c1sc_,$(addsuffix .o,$(CPS1_SNDC))) \
	$(addprefix $(BUILD)/c1z_,$(addsuffix .o,$(CPS1_Z80))) \
	$(BUILD)/c1m_m68kcpu.o $(BUILD)/c1m_m68kdasm.o $(BUILD)/c1m_m68kops.o \
	$(BUILD)/c1i_m68000_intf.o $(BUILD)/c1i_z80_intf.o \
	$(BUILD)/c1x_stubs.o $(BUILD)/c1x_netg.o $(BUILD)/c1x_neostubs.o $(BUILD)/cps1_host.o

HOSTCC ?= gcc
$(BUILD)/m68kmake: $(CPS1)/src/cpu/m68k/m68kmake.c | $(BUILD)
	$(HOSTCC) -O2 -I$(CPS1)/src/cpu/m68k -o $@ $<
$(BUILD)/m68kops.c $(BUILD)/m68kops.h: $(BUILD)/m68kmake $(CPS1)/src/cpu/m68k/m68k_in.c
	$(BUILD)/m68kmake $(BUILD)/ $(CPS1)/src/cpu/m68k/m68k_in.c

# Паттерн-правила: C++ core-файлы -> c1*.o + cps1_rename.sh (objcopy)
$(BUILD)/c1b_%.o: $(CPS1)/src/burn/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1c_%.o: $(CPS1)/src/burn/drv/capcom/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1d_%.o: $(CPS1)/src/burn/devices/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1s_%.o: $(CPS1)/src/burn/snd/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# C-файлы звука (fm.c — ОБЯЗАТЕЛЬНО как C, иначе YM* имена C++)
$(BUILD)/c1sc_%.o: $(CPS1)/src/burn/snd/%.c | $(BUILD)
$(BUILD)/c1sc_ay8910.o: $(CPS1)/src/burn/snd/ay8910.c | $(BUILD)
	$(CC) $(CPS1_CFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1sc_fm.o: $(CPS1)/src/burn/snd/fm.c | $(BUILD)
	$(CC) $(CPS1_CFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1sc_ym2151.o: $(CPS1)/src/burn/snd/ym2151.c | $(BUILD)
	$(CC) $(CPS1_CFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1sc_fmopl.o: $(CPS1)/src/burn/snd/fmopl.c | $(BUILD)
	$(CC) $(CPS1_CFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@

# добор звука из FBNeo (r0.305): ym3812 (Toaplan), upd7759 (NeoGeo), msm5232
# не в текущем линке (msm5232 требует C++ tr1); правило c1sd_ не нужно.
# Musashi CPU (C): m68kcpu/m68kdasm включают m68kops.h, ждём генерации.
$(BUILD)/c1m_%.o: $(CPS1)/src/cpu/m68k/%.c | $(BUILD)
	$(CC) $(CPS1_CFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1m_m68kcpu.o: $(BUILD)/m68kops.h
$(BUILD)/c1m_m68kdasm.o: $(BUILD)/m68kops.h
$(BUILD)/c1m_m68kops.o: $(BUILD)/m68kops.c | $(BUILD)
	$(CC) $(CPS1_CFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# Z80 (C++)
$(BUILD)/c1z_%.o: $(CPS1)/src/cpu/z80/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# CPU-интерфейсы (m68000_intf/z80_intf)
$(BUILD)/c1i_%.o: $(CPS1)/src/cpu/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# glue-стабы
$(BUILD)/c1x_stubs.o: $(CPS1)/src/cps1_stubs.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1x_netg.o: $(CPS1)/src/cps1_netg.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1x_neostubs.o: $(CPS1)/src/neostubs.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# host-слой (C++: с burnint.h и joyprocess, как вендор; без переименований)

# NEOGEO / Cave / Toaplan (добор из FBNeo, r0.305): те же флаги CPS, свои
# каталоги и префиксы объектов (c1n_/c1v_/c1t_).
CPS1_NEO := $(notdir $(wildcard $(CPS1)/src/burn/drv/neogeo/*.cpp))
CPS1_CAV := $(notdir $(wildcard $(CPS1)/src/burn/drv/cave/*.cpp))
OBJ += $(addprefix $(BUILD)/c1n_,$(patsubst %.cpp,%.o,$(CPS1_NEO)))
# Cave отключён (требует sh4/NEC CPU) — add later.
# Toaplan (r0.330): подключены 68K/Z80-игры (truxton2, batrider, bgaregga,
# snowbro2, tekipaki, pipibibs, enmadaio, mahoudai, shippumd, kbash2, bbakraid,
# sstriker и клоны). NEC V25/V30-игры (batsugun, twincobr, wardner…) — позже.
CPS1_TOA  := toaplan toaplan1 toa_gp9001 toa_palette toa_extratext \
             d_batrider d_battleg d_bbakraid d_enmadaio d_kbash2 \
             d_mahoudai d_pipibibs d_shippumd d_snowbro2 d_tekipaki d_truxton2
OBJ += $(addprefix $(BUILD)/c1t_,$(addsuffix .o,$(CPS1_TOA)))
$(BUILD)/c1n_%.o: $(CPS1)/src/burn/drv/neogeo/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1v_%.o: $(CPS1)/src/burn/drv/cave/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1t_%.o: $(CPS1)/src/burn/drv/toaplan/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# Звук toaplan: YM3812 (OPL2, pipibibs/tekipaki), YMZ280B (PCM, bbakraid).
# Файлы в snd/; c1se_ — отдельный префикс от игровых c1t_.
CPS1_SND3 := burn_ym3812 ymz280b
OBJ += $(addprefix $(BUILD)/c1se_,$(addsuffix .o,$(CPS1_SND3)))
$(BUILD)/c1se_%.o: $(CPS1)/src/burn/snd/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/cps1_host.o: $(TOP)h3_bare/cores/cps1_host.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@ $<
.PHONY: cps1-obj
cps1-obj: $(CPS1_OBJ)

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
# Coleco OS-7 BIOS (вшитый, сборка из bios_data.S)
$(BUILD)/coleco_bios.o: $(TOP)h3_bare/cores/gearcoleco/bios_data.S | $(BUILD)
	$(AS) $(CFLAGS) -I$(TOP)h3_bare/cores/gearcoleco -x assembler-with-cpp -c -o $@ $<
