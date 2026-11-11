# Makefile — параллельная сборка мультисистемного эмулятора H3 (замена build.sh).
# Использование:
#   make            — собрать build/h3_bare.bin (+ копию в корень h3_bare.bin)
#   make -j$(nproc) — параллельно, на многоядерной машине ~15 сек
#   make clean      — удалить build/
#   make sd         — собрать SD-образ build/h3_bare.img
#   make fel        — залить через sunxi-fel
#   make help       — справка
#
# r1155 (E10): зависимости от заголовков отслеживаются (-MMD -MP, файлы
# build/*.d подключаются в конце). После правки .h достаточно make; clean
# нужен для смены тулчейна/флагов или после правки самого Makefile.

TOP      := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
BUILD    := $(patsubst %/,%,$(TOP))/build
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
# r1155 (E10): DEPFLAGS добавляется к каждому набору флагов компиляции;
# .d-файлы складываются в build/ и подключаются одним -include в конце.
DEPFLAGS := -MMD -MP
CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm
CFLAGS += -ffreestanding -Wall -Wextra -O2 -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG
CFLAGS += $(DEPFLAGS)
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
# SNES (Snes9x2005): LAGFIX — ОПЦИЯ, дефолт ВЫКЛ (как в эталоне libretro-snes9x2005).
# При LAGFIX-выходе (finishedFrame) пропускаются S9xPackStatus()/S9xAPUPackStatus()
# — статусы не пакетируются, неточность SPC700-звука/HDMA-таймингов.
# Не-LAGFIX ветка выдаёт ровно один кадр за S9xMainLoop() (выход по SCAN_KEYS_FLAG
# на последнем сканлайне), host на это и рассчитан (snes_host.cpp).
# Включить: make SNES_LAGFIX=1
SNES_LAGFIX ?= 0
SNES_CFLAGS := $(CFLAGS) -DLOAD_FROM_MEMORY -DHAVE_NO_LANGEXTRA -Wno-incompatible-pointer-types
ifeq ($(SNES_LAGFIX),1)
SNES_CFLAGS += -DLAGFIX
endif
GPGX_CFLAGS   := $(CFLAGS) -DLSB_FIRST -DBYTE_ORDER=LITTLE_ENDIAN -DMAXROMSIZE=16777216 -DUSE_16BPP_RENDERING -DFRONTEND_SUPPORTS_RGB565

# ---- Авто-генерация списков объектов ----
OBJ  := $(BUILD)/startup.o
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,menu rom_browser settings sd fat usb_ohci usb_kbd usb_msd usb_hc fb_text led emu cheatdb sega_pad btn_pad remap i2s tft_drv wifi_net wifi_layer wpa_crypto net_tcp net_ftp))
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,system_atari_h3 system_a7800_h3 system_a5200_h3 gameboy_host gameboy_stubs lynx_host snes_host snes_compat gpgx_host gpgx_mathx gpgx_missing gp_cheats))
OBJ  += $(BUILD)/gba_mgba_host.o
OBJ  += $(BUILD)/gba_bios_data.o
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,mgba_gba_audio mgba_gba_bios mgba_gba_cheats mgba_gba_core mgba_gba_dma mgba_gba_gba mgba_gba_hle-bios mgba_gba_input mgba_gba_io mgba_gba_memory mgba_gba_overrides mgba_gba_savedata mgba_gba_serialize mgba_gba_sharkport mgba_gba_sio mgba_gba_timer mgba_gba_video mgba_gba_cart_ereader mgba_gba_cart_gpio mgba_gba_cart_matrix mgba_gba_cart_unlicensed mgba_gba_cart_vfame mgba_gba_cheats_codebreaker mgba_gba_cheats_gameshark mgba_gba_cheats_parv3 mgba_gba_renderers_cache-set mgba_gba_renderers_common mgba_gba_renderers_gl mgba_gba_renderers_software-bg mgba_gba_renderers_software-mode0 mgba_gba_renderers_software-obj mgba_gba_renderers_video-software mgba_gba_sio_gbp mgba_arm_arm mgba_arm_decoder-arm mgba_arm_decoder-thumb mgba_arm_decoder mgba_arm_isa-arm mgba_arm_isa-thumb mgba_core_bitmap-cache mgba_core_cache-set mgba_core_cheats mgba_core_config mgba_core_core mgba_core_directories mgba_core_input mgba_core_interface mgba_core_library mgba_core_lockstep mgba_core_log mgba_core_map-cache mgba_core_mem-search mgba_core_rewind mgba_core_serialize mgba_core_sync mgba_core_thread mgba_core_tile-cache mgba_core_timing mgba_util_audio-buffer mgba_util_audio-resampler mgba_util_circle-buffer mgba_util_configuration mgba_util_convolve mgba_util_crc32 mgba_util_elf-read mgba_util_formatting mgba_util_gbk-table mgba_util_geometry mgba_util_gui mgba_util_hash mgba_util_image mgba_util_interpolator mgba_util_md5 mgba_util_memory mgba_util_patch-fast mgba_util_patch-ips mgba_util_patch-ups mgba_util_patch mgba_util_ring-fifo mgba_util_sfo mgba_util_sha1 mgba_util_string mgba_util_table mgba_util_text-codec mgba_util_vector mgba_util_vfs mgba_util_vfs_vfs-fifo mgba_util_vfs_vfs-lzma mgba_util_vfs_vfs-mem mgba_gb_audio mgba_third-party_inih_ini))
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,portfolio_system portfolio_cpu portfolio_i8253 portfolio_i8259))
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,uart printf libc_min main cxx_runtime udelay h3_hs_timer h3_ccu h3 h3_smp h3_de2 h3_hdmi dw_hdmi h3_lcd gic))
OBJ  += $(BUILD)/coleco_bios.o
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,h3_sdc fw_blob))


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
OBJ  += $(addprefix $(BUILD)/,ngp_host.o ngp_main.o ngp_memory.o ngp_graphics.o ngp_tlcs900h.o ngp_z80.o ngp_flash.o ngp_neopopsound.o ngp_sound.o ngp_ngpBios.o ngp_input.o ngp_neopop_blip.o)

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
COL_CXXFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm $(DEPFLAGS)
COL_CXXFLAGS += -Wall -Wextra -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics
# Дизассемблер/трейс-логирование Gearcoleco ОТКЛЮЧЕНО: LogInstructionEvent()
# вызывается на КАЖДУЮ инструкцию и выделяет record из bump-кучи
# (GetOrCreateDisassemblerRecord) — пул 24 МБ за секунды исчерпывается и
# Coleco «зависает»/падает. Для игры трейс не нужен.
COL_CXXFLAGS += -DGEARCOLECO_DISABLE_DISASSEMBLER
# r1083: -DPERFORMANCE — батч RunFor(75) вместо потактного RunFor(1) в
# RunToVBlank. Без него весь кадр NTSC (~60K тактов) крутится по ОДНОЙ
# инструкции Z80 на итерацию outer-цикла, на каждую вызываются
# Video::Tick+Audio::Tick+Memory::Tick → накладные на кадр на порядок выше
# (игра «тормозила»). Семантика потактовых NMI/VDP инвариантна (Tick ставит
# флаг RequestNMI, обработка сдвигается на ≤75 тактов). Как на PS2/PSP-эталоне.
COL_CXXFLAGS += -DPERFORMANCE
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
OBJ  += $(addprefix $(BUILD)/,$(addsuffix .o,msx_host msx_compat msx_log msx2_rom_data msx2ext_rom_data msx2p_rom_data msx2pext_rom_data msx_rom_data msx503_cpm_data msx503_net_data msx_carts_sha_data))
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
$(BUILD)/msx2p_rom_data.o: $(MSX)/host/msx2p_rom_data.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx2pext_rom_data.o: $(MSX)/host/msx2pext_rom_data.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_rom_data.o: $(MSX)/host/msx_rom_data.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx503_cpm_data.o: $(MSX)/host/msx503_cpm_data.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx503_net_data.o: $(MSX)/host/msx503_net_data.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@.tmp $<
	$(MSX_RENAME) $@.tmp $@; rm -f $@.tmp
$(BUILD)/msx_carts_sha_data.o: $(MSX)/host/msx_carts_sha_data.c | $(BUILD)
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

# ---- МС1504 (Fake86: 8086/PC-XT, КР1834ВМ86) ----
# Vendored fake86_src; SDL-файлы (main/render/input/parsecl/timing/hostfs/…)
# НЕ собираются. Вместо них: ms1504_host.c (наш host) + ms1504_shim.c
# (заглушки SDL/OSD/хостфс). SDL-free override-заголовки лежат В fake86_src/
# (render.h/hostfs.h/mutex.h без SDL): `#include "hostfs.h"` резолвится в
# каталог включающего файла, поэтому override обязан жить рядом с исходником,
# отдельный -I не перебивает. video.c пропатчен (rgb/initcga без SDL).
MS1504 := $(TOP)h3_bare/cores/ms1504
MS1504_INC := -I$(MS1504)/fake86_src -I$(MS1504)
MS1504_CFLAGS := $(CFLAGS) $(MS1504_INC)
MS1504_BONES  := cpu bios i8237 i8253 i8259 video ports disk bindata sermouse com shim host bios_data boot_ima font_comb
OBJ += $(addprefix $(BUILD)/ms1504_,$(addsuffix .o,$(MS1504_BONES)))

$(BUILD)/ms1504_%.o: $(MS1504)/fake86_src/%.c | $(BUILD)
	$(CC) $(MS1504_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ms1504_bios_data.o: $(MS1504)/ms1504_bios_data.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD)/ms1504_boot_ima.o: $(MS1504)/ms1504_boot_ima.c | $(BUILD)
	$(CC) $(MS1504_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ms1504_shim.o: $(MS1504)/ms1504_shim.c | $(BUILD)
	$(CC) $(MS1504_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ms1504_com.o: $(MS1504)/ms1504_com.c | $(BUILD)
	$(CC) $(MS1504_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ms1504_font_comb.o: $(MS1504)/ms1504_font_comb.c | $(BUILD)
	$(CC) $(MS1504_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ms1504_host.o: $(MS1504)/ms1504_host.c | $(BUILD)
	$(CC) $(MS1504_CFLAGS) $(INCLUDES) -c -o $@ $<
# После сборки ВСЕХ объектов набора один батч-переименователь добавляет
# префикс m15_ ко всем глобальным символам (согласованно: определения И
# ссылки во всех объектах). Снимает коллизии с другими ядрами
# (--allow-multiple-definition), напр. timing из BK.
MS1504_OBJS := $(addprefix $(BUILD)/ms1504_,$(addsuffix .o,$(MS1504_BONES)))
$(BUILD)/ms1504_renamed.stamp: $(MS1504_OBJS)
	$(TOP)h3_bare/cores/ms1504_rename.sh $(MS1504_OBJS)
	touch $@

# ---- Радио-86РК (порт RK8266: i8080 + ВГ75/ВВ55 + вшитый монитор) ----
# r1185: CPU — i8080.c (Alexander Demin, GPL-2+), машина/порты — rk86_machine.c,
# рендер — rk86_video.c, образы монитора 2К и знакогенератора — rk86_data.c.
# Переименование не нужно: внешние символы только rk86_*/i8080_*, остальное static.
RK86 := $(TOP)h3_bare/cores/rk86
RK86_INC := -I$(RK86)
RK86_CFLAGS := $(CFLAGS) $(RK86_INC)
RK86_BONES := i8080 rk86_machine rk86_video rk86_data
OBJ += $(addprefix $(BUILD)/rk86_,$(addsuffix .o,$(RK86_BONES)))
OBJ += $(BUILD)/rk86_host.o

$(BUILD)/rk86_i8080.o: $(RK86)/i8080.c | $(BUILD)
	$(CC) $(RK86_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/rk86_rk86_machine.o: $(RK86)/rk86_machine.c | $(BUILD)
	$(CC) $(RK86_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/rk86_rk86_video.o: $(RK86)/rk86_video.c | $(BUILD)
	$(CC) $(RK86_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/rk86_rk86_data.o: $(RK86)/rk86_data.c | $(BUILD)
	$(CC) $(RK86_CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/rk86_host.o: $(TOP)h3_bare/cores/rk86_host.c | $(BUILD)
	$(CC) $(RK86_CFLAGS) $(INCLUDES) -c -o $@ $<


# ---- Atari 8-bit (400/800/XL/XE): libretro-core atari800 (вендор) ----
# r778: ядро в прошивке (host a8_host.c + файловый слой a8_fs.c). ROM-буфер
# регистрируется в a8_fs; системные OS-ROM вшиты (EMUOS_ALTIRRA).
A8 := $(TOP)h3_bare/cores/atari800
A8_INC := -I$(A8) -I$(A8)/shim -I$(A8)/atari800/src -I$(A8)/libretro \
	-I$(A8)/libretro/libretro-common/include -I$(A8)/deps/zlib
A8_CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm -Wall -Wextra $(DEPFLAGS) \
	-O2 -fno-strict-aliasing -fwrapv -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
	$(A8_INC) -D__LIBRETRO__ -D__STDC_FORMAT_MACROS \
	-Wno-unused -Wno-unused-parameter -Wno-parentheses -Wno-sign-compare \
	-Wno-array-bounds
# ВАЖНО: БЕЗ -ffreestanding — newlib в freestanding не определяет PRId64,
# а libretro-common требует его («inttypes.h is being screwy»).
# Категории (свои префиксы — избегаем дублей имён, напр. crc32 в ядре и zlib)
A8C := afile antic atari binload cartridge cassette compfile cfg cpu crc32 devices cartridge_info esc gtia img_tape log memory monitor pbi pia pokey pokeysnd mzpokeysnd remez rtime sio sysrom util sound pbi_proto80 af80 input statesav ui_basic ui artifact colours colours_ntsc colours_pal colours_external screen cycle_map pbi_mio pbi_bb pbi_scsi ide xep80 xep80_fonts file_export filter_ntsc atari_ntsc
A8L := platform carts_hash libretro-core core-mapper graph vkbd retro_strings retro_utils retro_disk_control retro_vfs retro_files
A8M := memory_stream compat_strl compat_strcasestr fopen_utf8 encoding_utf file_path file_path_io stdstring rtime
A8R := altirraos_xl altirraos_800 altirra_basic altirra_5200_os altirra_5200_charset
A8Z := adler32 crc32 inflate inftrees deflate trees zutil inffast gzread gzclose gzlib gzwrite
A8_OBJS := $(addprefix $(BUILD)/a8c_,$(addsuffix .o,$(A8C))) \
	$(addprefix $(BUILD)/a8l_,$(addsuffix .o,$(A8L))) \
	$(addprefix $(BUILD)/a8m_,$(addsuffix .o,$(A8M))) \
	$(addprefix $(BUILD)/a8r_,$(addsuffix .o,$(A8R))) \
	$(addprefix $(BUILD)/a8z_,$(addsuffix .o,$(A8Z)))
# r778: ядро + host + файловый слой идут в прошивку.
# r1013: ЕДИНЫЙ файловый слой прошивки вынесен в cores/fs_layer.c (из
# atari800/a8_fs.c) — его глобальные fopen/fread/stat/open/read видят ВСЕ
# ядра. a8_fs.o остаётся тонким мостом a8_fs_register->fs_register.
OBJ += $(A8_OBJS) $(BUILD)/a8_host.o $(BUILD)/a8_fs.o $(BUILD)/fs_layer.o

$(BUILD)/a8c_%.o: $(A8)/atari800/src/%.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8c_atari_ntsc.o: $(A8)/atari800/src/atari_ntsc/atari_ntsc.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8l_%.o: $(A8)/libretro/%.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
# r782: cmdline.c включается (#include) в libretro-core.c — обязаны
# пересобирать libretro-core.o при правке cmdline.c (Makefile не
# трекает зависимости от включённых файлов).
$(BUILD)/a8l_libretro-core.o: $(A8)/libretro/cmdline.c
$(BUILD)/a8m_%.o: $(A8)/libretro/libretro-common/streams/%.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8m_compat_strl.o: $(A8)/libretro/libretro-common/compat/compat_strl.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8m_compat_strcasestr.o: $(A8)/libretro/libretro-common/compat/compat_strcasestr.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8m_fopen_utf8.o: $(A8)/libretro/libretro-common/compat/fopen_utf8.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8m_encoding_utf.o: $(A8)/libretro/libretro-common/encodings/encoding_utf.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8m_file_path.o: $(A8)/libretro/libretro-common/file/file_path.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8m_file_path_io.o: $(A8)/libretro/libretro-common/file/file_path_io.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8m_stdstring.o: $(A8)/libretro/libretro-common/string/stdstring.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8m_rtime.o: $(A8)/libretro/libretro-common/time/rtime.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8r_%.o: $(A8)/atari800/src/roms/%.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
$(BUILD)/a8z_%.o: $(A8)/deps/zlib/%.c | $(BUILD)
	$(CC) $(A8_CFLAGS) -c -o $@ $<
# Батч-ренейм глобалов ядра в a8_* (как ms1504): после сборки, перед линком.
# Host/файловый слой (a8_host.o/a8_fs.o) НЕ входят в набор — они держат
# внешние имена (emu_run_atari800, fopen/fread/...) и зовут a8_retro_*.
# r778: BSS ядра atari800 вынесен в .a8_bss (NOLOAD, 0x52000000) — иначе
# ~1.9 МБ BSS сдвигают _hend/.coherent/L1-таблицу (ASSERT-ошибка линковки).
# Переименование секции — на каждом объекте ДО батч-ренейма символов.
$(BUILD)/a8_renamed.stamp: $(A8_OBJS)
	for o in $(A8_OBJS); do arm-none-eabi-objcopy --rename-section .bss=.a8_bss "$$o" "$$o" || exit 1; done
	$(TOP)h3_bare/cores/a8_rename.sh $(A8_OBJS)
	touch $@
# r778: host-слой и файловый слой atari800 — как у других ядер (не вендор):
$(BUILD)/a8_host.o: $(A8)/a8_host.c | $(BUILD)
	$(CC) $(A8_CFLAGS) $(INCLUDES) -I$(A8)/libretro/libretro-common/include -c -o $@ $<
$(BUILD)/a8_fs.o: $(A8)/a8_fs.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -I$(A8)/libretro/libretro-common/include -c -o $@ $<

# r1013: единый файловый слой прошивки (cores/fs_layer.c), общий для всех ядер.
$(BUILD)/fs_layer.o: $(TOP)h3_bare/cores/fs_layer.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -I$(A8)/libretro/libretro-common/include -c -o $@ $<

# ---- Amstrad CPC: libretro-cap32 (Caprice32), ядро в h3_bare/cores/cap32 ----
# r963: вшитый BIOS всех моделей (rom/464.h 6128.h 664.h 6128p.h); host cpc_host.c +
# файловый слой cpc_fs.c (rfopen поверх ROM_BUF). Состав — из Makefile.common вендора.
# Префиксы объектов cpc_/cpcL_/cpcM_ — избегаем дублей имён (crc32, file_stream, ...).
CPC := $(TOP)h3_bare/cores/cap32
CPC_INC := -I$(CPC) -I$(CPC)/cap32 -I$(CPC)/libretro \
	-I$(CPC)/libretro/microui -I$(CPC)/libretro-common/include
CPC_CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm -Wall -Wextra $(DEPFLAGS) \
	-O2 -fno-strict-aliasing -fwrapv -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
	$(CPC_INC) -D__LIBRETRO__ -D__STDC_FORMAT_MACROS -DINLINE="inline" -DHAVE_CONFIG_H \
	-Wno-unused -Wno-unused-parameter -Wno-parentheses -Wno-sign-compare -Wno-array-bounds \
	-Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-int-conversion
# Ядро (категория cpc_, прямые файлы в cap32/)
CPCC := cap32 slots crtc fdc psg tape cart asic z80 kbdauto \
	lightgun_gunstick lightgun_phaser lightgun_lightgun
# libretro-обвязка (категория cpcL_); libretro-core — главный host (retro_*)
CPCL := libretro-core microui_microui db_database dsk_loader dsk_format dsk_amsdos_catalog \
	gfx_software gfx_video gfx_video8bpp gfx_video16bpp gfx_video24bpp \
	assets_ui_keyboard_bg_crop assets_ui_keyboard_bg assets_ui_keyboard_en \
	assets_ui_keyboard_es assets_ui_keyboard_fr assets_font \
	retro_strings retro_utils retro_disk_control retro_events retro_snd \
	retro_render retro_ui retro_gun retro_keyboard
# libretro-common (категория cpcM_); vfs/file_stream/retro_dirent НЕ нужны —
# файловый слой cpc_fs.c покрывает rf*/filestream_* поверх памяти (newlib
# freestanding не даёт DIR/opendir для vfs_implementation).
CPCLM := file_path file_path_io stdstring compat_strl fopen_utf8 encoding_utf rtime memalign
CPC_OBJS := $(addprefix $(BUILD)/cpc_,$(addsuffix .o,$(CPCC))) \
	$(addprefix $(BUILD)/cpcL_,$(addsuffix .o,$(CPCL))) \
	$(addprefix $(BUILD)/cpcM_,$(addsuffix .o,$(CPCLM)))
OBJ += $(CPC_OBJS) $(BUILD)/cpc_host.o $(BUILD)/cpc_fs.o

$(BUILD)/cpc_%.o: $(CPC)/cap32/%.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpc_lightgun_gunstick.o: $(CPC)/cap32/lightgun/gunstick.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpc_lightgun_phaser.o: $(CPC)/cap32/lightgun/phaser.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpc_lightgun_lightgun.o: $(CPC)/cap32/lightgun/lightgun.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_microui_microui.o: $(CPC)/libretro/microui/microui.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_db_database.o: $(CPC)/libretro/db/database.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_dsk_loader.o: $(CPC)/libretro/dsk/loader.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_dsk_format.o: $(CPC)/libretro/dsk/format.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_dsk_amsdos_catalog.o: $(CPC)/libretro/dsk/amsdos_catalog.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_gfx_software.o: $(CPC)/libretro/gfx/software.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_gfx_video.o: $(CPC)/libretro/gfx/video.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_gfx_video8bpp.o: $(CPC)/libretro/gfx/video8bpp.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_gfx_video16bpp.o: $(CPC)/libretro/gfx/video16bpp.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_gfx_video24bpp.o: $(CPC)/libretro/gfx/video24bpp.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_assets_ui_keyboard_bg_crop.o: $(CPC)/libretro/assets/ui_keyboard_bg_crop.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_assets_ui_keyboard_bg.o: $(CPC)/libretro/assets/ui_keyboard_bg.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_assets_ui_keyboard_en.o: $(CPC)/libretro/assets/ui_keyboard_en.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_assets_ui_keyboard_es.o: $(CPC)/libretro/assets/ui_keyboard_es.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_assets_ui_keyboard_fr.o: $(CPC)/libretro/assets/ui_keyboard_fr.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_assets_font.o: $(CPC)/libretro/assets/font.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_retro_strings.o: $(CPC)/libretro/retro_strings.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_retro_utils.o: $(CPC)/libretro/retro_utils.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_retro_disk_control.o: $(CPC)/libretro/retro_disk_control.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_retro_events.o: $(CPC)/libretro/retro_events.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_retro_snd.o: $(CPC)/libretro/retro_snd.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_retro_render.o: $(CPC)/libretro/retro_render.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_retro_ui.o: $(CPC)/libretro/retro_ui.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_retro_gun.o: $(CPC)/libretro/retro_gun.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_retro_keyboard.o: $(CPC)/libretro/retro_keyboard.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcL_libretro-core.o: $(CPC)/libretro/libretro-core.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_file_path.o: $(CPC)/libretro-common/file/file_path.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_file_path_io.o: $(CPC)/libretro-common/file/file_path_io.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_retro_dirent.o: $(CPC)/libretro-common/file/retro_dirent.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_file_stream.o: $(CPC)/libretro-common/streams/file_stream.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_file_stream_transforms.o: $(CPC)/libretro-common/streams/file_stream_transforms.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_vfs_implementation.o: $(CPC)/libretro-common/vfs/vfs_implementation.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_stdstring.o: $(CPC)/libretro-common/string/stdstring.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_compat_strl.o: $(CPC)/libretro-common/compat/compat_strl.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_fopen_utf8.o: $(CPC)/libretro-common/compat/fopen_utf8.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_encoding_utf.o: $(CPC)/libretro-common/encodings/encoding_utf.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_rtime.o: $(CPC)/libretro-common/time/rtime.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpcM_memalign.o: $(CPC)/libretro-common/memmap/memalign.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) -c -o $@ $<
$(BUILD)/cpc_host.o: $(CPC)/cpc_host.c | $(BUILD)
	$(CC) $(CPC_CFLAGS) $(INCLUDES) -I$(CPC)/libretro-common/include -c -o $@ $<
$(BUILD)/cpc_fs.o: $(CPC)/cpc_fs.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -I$(CPC)/libretro-common/include -c -o $@ $<
# Батч-ренейм глобалов ядра в cpc_* (как a8): после сборки, перед линком.
# cpc_fs.o ВХОДИТ в набор: он определяет rf*/filestream-API (cpc_rfopen и голые
# rfeof/rferror/...), поэтому rf* попадают в CANON и голые ссылки ядра
# (fopen→rfopen по file_stream_transforms.h) переименовываются в cpc_* — иначе
# они резолвятся в чужие стабы gpgx_missing.o (rfopen=NULL → DSK Error).
# cpc_host.o НЕ входит — держит внешние имена (emu_run_cpc) и зовёт cpc_retro_*.
$(BUILD)/cpc_renamed.stamp: $(CPC_OBJS) $(BUILD)/cpc_fs.o
	$(TOP)h3_bare/cores/cpc_rename.sh $(CPC_OBJS) $(BUILD)/cpc_fs.o
	touch $@

# ---- Commodore 64/128 (VICE / vice-libretro), ядро в h3_bare/cores/vice ----
# r974: vendored libretro/vice-libretro (VICE 3.10). EMUTYPE=x128 — один бинарь
# покрывает C64 и C128 (VICII+VDC, resid); BIOS вшиты через USE_EMBEDDED
# (include/embedded/*.h: c64 basic/kernal/chargen, c128 basiclo/hi/kernal/chargen).
# retrodep/ — готовый libretro-архслой (видео/звук/ввод/архитектура); sdl/gtk3
# из arch удалены. freestanding-newlib ошибался на <dirent.h> — shim/dirent.h
# (как fuse/atari800). Префиксы объектов vz_ — избегаем дублей имён
# (main, log, lib, crc32, file_path, filestream_*, retro_* с другими ядрами).
VICE_ROOT := $(TOP)h3_bare/cores/vice
VICE_INC  := -I$(VICE_ROOT)/shim -I$(VICE_ROOT)/vice/src -I$(VICE_ROOT) \
	-I$(VICE_ROOT)/libretro -I$(VICE_ROOT)/include -I$(VICE_ROOT)/include/embedded \
	-I$(VICE_ROOT)/vice/src/core/rtc -I$(VICE_ROOT)/vice/src/lib \
	-I$(VICE_ROOT)/vice/src/printerdrv -I$(VICE_ROOT)/retrodep \
	-I$(VICE_ROOT)/libretro-common/include -I$(VICE_ROOT)/deps/libz \
	-I$(VICE_ROOT)/vice/src/sid -I$(VICE_ROOT)/vice/src/arch/shared \
	-I$(VICE_ROOT)/vice/src/c64 -I$(VICE_ROOT)/vice/src/c64/cart \
	-I$(VICE_ROOT)/vice/src/c64dtv -I$(VICE_ROOT)/vice/src/c128 -I$(VICE_ROOT)/vice/src/core \
	-I$(VICE_ROOT)/vice/src/datasette -I$(VICE_ROOT)/vice/src/diag \
	-I$(VICE_ROOT)/vice/src/drive -I$(VICE_ROOT)/vice/src/drive/iec \
	-I$(VICE_ROOT)/vice/src/drive/iec/c64exp -I$(VICE_ROOT)/vice/src/drive/ieee \
	-I$(VICE_ROOT)/vice/src/drive/tcbm -I$(VICE_ROOT)/vice/src/fsdevice \
	-I$(VICE_ROOT)/vice/src/imagecontents -I$(VICE_ROOT)/vice/src/joyport \
	-I$(VICE_ROOT)/vice/src/lib/p64 -I$(VICE_ROOT)/vice/src/monitor \
	-I$(VICE_ROOT)/vice/src/raster -I$(VICE_ROOT)/vice/src/resid \
	-I$(VICE_ROOT)/vice/src/residfp \
	-I$(VICE_ROOT)/vice/src/residfp/builders/residfp-builder/residfp \
	-I$(VICE_ROOT)/vice/src/rs232drv -I$(VICE_ROOT)/vice/src/rtc \
	-I$(VICE_ROOT)/vice/src/samplerdrv -I$(VICE_ROOT)/vice/src/tape \
	-I$(VICE_ROOT)/vice/src/tapeport -I$(VICE_ROOT)/vice/src/userport \
	-I$(VICE_ROOT)/vice/src/vdc -I$(VICE_ROOT)/vice/src/vdrive \
	-I$(VICE_ROOT)/vice/src/vicii -I$(VICE_ROOT)/vice/src/video
VICEFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm -Wall -Wextra \
	-O2 -fno-strict-aliasing -fwrapv -DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
	-DHAVE_CONFIG_H -D__LIBRETRO__ -DUSE_LIBRETRO_VFS -DCORE_NAME=\"x128\" -D__X128__ \
	-D__STDC_FORMAT_MACROS -DHAVE_7ZIP -D_7ZIP_ST \
	-Wno-format -Wno-format-security -Wno-unused -Wno-unused-parameter \
	-Wno-parentheses -Wno-sign-compare -Wno-array-bounds \
	-Wno-incompatible-pointer-types -Wno-implicit-function-declaration -Wno-int-conversion
# Состав — из Makefile.common/Makefile.x128 вендора (полный список в
# vice_sources.mk, генерируется из dry-run make EMUTYPE=x128; цели в
# vice_rules.mk строятся по тому же правилу имён).
include $(VICE_ROOT)/vice_sources.mk
include $(VICE_ROOT)/vice_rules.mk
VICE_OBJS := $(foreach f,$(VICE_REL),$(BUILD)/vz_$(subst /,_,$(subst .,_,$(f))).o)
OBJ += $(VICE_OBJS) $(BUILD)/c64_host.o $(BUILD)/c64_fs.o $(BUILD)/c64_stubs.o

$(BUILD)/c64_host.o: $(TOP)h3_bare/cores/c64_host.c | $(BUILD)
	$(CC) $(VICEFLAGS) $(VICE_INC) $(INCLUDES) -c -o $@ $<
$(BUILD)/c64_fs.o: $(TOP)h3_bare/cores/c64_fs.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -I$(VICE_ROOT)/libretro-common/include -c -o $@ $<
$(BUILD)/c64_stubs.o: $(TOP)h3_bare/cores/c64_stubs.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
# Батч-ренейм глобалов набора в vice_* (как a8/cpc): после сборки, перед линком.
# r974: BSS ядра VICE (~31 МБ, из них 21 МБ — таблица ReSID filter8580new) вынесен
# в .vice_bss (NOLOAD, 0x52000000) — иначе он переполняет общую .bss и плавающий
# пул .noinit_fbn наезжает на uncached-зону 0x4A400000 (ASSERT-ошибка линковки).
# Переименование секции — на КАЖДОМ объекте ДО батч-ренейма символов (как a8).
# Host-слой (c64_host.o/c64_fs.o/c64_stubs.o) НЕ входит — держит внешние имена
# (emu_run_c64, c64_fs_register, fopen/fread) и зовёт vice_retro_*.
$(BUILD)/vice_renamed.stamp: $(VICE_OBJS)
	for o in $(VICE_OBJS); do arm-none-eabi-objcopy --rename-section .bss=.vice_bss "$$o" "$$o" || exit 1; done
	$(TOP)h3_bare/cores/vice_rename.sh $(VICE_OBJS)
	touch $@

# ---- Amiga (PUAE / libretro-uae, WinUAE 5.3.1) ----
# Vendored в h3_bare/cores/puae/ (ядро sources/src + libretro + retrodep).
# Сборка под Cortex-A7 (classic_armv7_a7 в вендорном Makefile = NESC/SNESC/C64
# mini — тот же CPU, что H3): -Ofast -flto -marm -mfpu=neon-vfpv4.
# Состав — из вендорного Makefile.common: puae_sources.mk (генерируется
# puae_gen_sources.sh), правила — puae_rules.mk (puae_gen_rules.sh).
# Глобалы набора (retro_*/main/log/crc32/gui/sound/parser...) конфликтуют с
# другими ядрами — puae_rename.sh переименовывает в puae_* (как vice_*).
# Host-слой puae_host.o НЕ входит в набор: зовёт puae_retro_*.
PUAE_ROOT := $(TOP)h3_bare/cores/puae
include $(PUAE_ROOT)/puae_sources.mk
include $(PUAE_ROOT)/puae_rules.mk
# PUAE_OBJS/PUAE_ZOBJS из sources.mk — голые имена; префикс $(BUILD)/ (как VICE).
PUAE_OBJS_ALL := $(foreach f,$(PUAE_OBJS) $(PUAE_ZOBJS),$(BUILD)/$(f)) $(PUAE_LRC_OBJS)
OBJ += $(PUAE_OBJS_ALL) $(BUILD)/puae_host.o $(BUILD)/puae_kickstart_data.o
PUAE_INC := -I$(PUAE_ROOT) -I$(PUAE_ROOT)/sources/src -I$(PUAE_ROOT)/sources/src/include \
	-I$(PUAE_ROOT)/libretro -I$(PUAE_ROOT)/retrodep \
	-I$(PUAE_ROOT)/libretro-common/include -I$(PUAE_ROOT)/deps/libz -I$(PUAE_ROOT)/deps/7zip
PUAE_CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm -ffreestanding \
	-std=gnu99 -Wall -Wextra -O2 -fno-strict-aliasing -fwrapv \
	-DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
	-D__LIBRETRO__ -D__STDC_FORMAT_MACROS -D_7ZIP_ST \
	-Wno-unused -Wno-unused-parameter -Wno-sign-compare -Wno-parentheses \
	-Wno-incompatible-pointer-types -Wno-implicit-function-declaration
$(BUILD)/puae_host.o: $(TOP)h3_bare/cores/puae_host.c | $(BUILD)
	$(CC) $(PUAE_CFLAGS) $(PUAE_INC) $(INCLUDES) -c -o $@ $<
# Вшитый Kickstart 1.3 rev 34.5 (A500, 256 КБ): .incbin bios/kick34005.A500.
# Путь инклюда относителен kickstart_data.S — ассемблер ищет его по -I$(PUAE_ROOT).
# r1232: в том же kickstart_data.S лежит диск Workbench 1.3 (bios/wb13.adf).
$(BUILD)/puae_kickstart_data.o: $(TOP)h3_bare/cores/puae/kickstart_data.S $(TOP)h3_bare/cores/puae/bios/kick34005.A500 $(TOP)h3_bare/cores/puae/bios/wb13.adf | $(BUILD)
	$(AS) $(PUAE_CFLAGS) -I$(PUAE_ROOT) -x assembler-with-cpp -c -o $@ $<
$(BUILD)/puae_renamed.stamp: $(PUAE_OBJS_ALL) $(TOP)h3_bare/cores/puae_rename.sh
	$(TOP)h3_bare/cores/puae_rename.sh $(PUAE_OBJS_ALL)
	touch $@

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
	-I$(CPS1)/src/cpu/nec -I$(CPS1)/src/cpu/z180 -I$(CPS1)/src/cpu/m6805 -I$(CPS1)/src/cpu/tms32010 \
	-I$(CPS1)/src/cpu/i8051 -I$(CPS1)/src/cpu/i8039 \
	-I$(CPS1)/src/burn/drv/sega \
	-I$(BUILD) -I$(CPS1_ZLIB) $(INCLUDES)
CPS1_CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm -ffreestanding $(DEPFLAGS) \
	-Wall -Wextra -O2 -fno-strict-aliasing \
	-DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
	-D__fastcall= -DLSB_FIRST=1 -DEMU_M68K -DFBNEO_DEBUG
# C++-файлы вендора БЕЗ -ffreestanding (как Gearcoleco): newlib freestanding
# не даёт <cmath>/tr1/free, на что опирается FBNeo.
CPS1_CXXFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm $(DEPFLAGS) \
	-Wall -Wextra -O2 -fno-strict-aliasing \
	-DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
	-D__fastcall= -DLSB_FIRST=1 -DEMU_M68K -DFBNEO_DEBUG \
	-fno-exceptions -fno-rtti -fno-threadsafe-statics

CPS1_BURN  := burn burn_bitmap burn_gun burn_led burn_memory burn_pal burn_sha1 burn_shift burn_sound cheat hiscore load tilemap_generic tiles_generic timer
CPS1_CAP   := cps cps2_crpt cps_config cps_draw cps_mem cps_obj cps_pal cps_run cps_rw cps_scr cpsr cpsrd cpst ctv d_cps1 d_cps2 fcrash_snd kabuki ps ps_m ps_z qs qs_c qs_z sf2mdt_snd
CPS1_DEV   := eeprom i2ceeprom timekpr resnet nmk112 watchdog taito_m68705 mc8123 8255ppi
CPS1_SND   := burn_ym2151 burn_ym2203 msm5205 msm6295 samples dac segapcm upd7759 burn_ym2413 burn_ym2612 rf5c68
CPS1_SNDC  := fm ym2151 fmopl ym2413 ym2612
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
$(BUILD)/c1sc_ym2413.o: $(CPS1)/src/burn/snd/ym2413.c | $(BUILD)
	$(CC) $(CPS1_CFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1sc_ym2612.o: $(CPS1)/src/burn/snd/ym2612.c | $(BUILD)
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
OBJ += $(addprefix $(BUILD)/c1n_,$(patsubst %.cpp,%.o,$(CPS1_NEO)))
# Cave (r0.377): ранняя эра на 68K (Donpachi/DoDonPachi/Esp.Ra.De/Guwange/Feveron).
# SH-4 (d_cv1k и CV-1000) и поздно-Cave НЕ включаем.
CPS1_CAV := cave cave_palette cave_sprite cave_tile \
            d_donpachi d_dodonpachi d_esprade d_guwange d_feversos \
            d_uopoko d_korokoro d_gaia d_sailormn d_pwrinst2 d_mazinger \
            d_hotdogst d_metmqstr
OBJ += $(addprefix $(BUILD)/c1v_,$(addsuffix .o,$(CPS1_CAV)))
# Toaplan (r0.330): подключены 68K/Z80-игры (truxton2, batrider, bgaregga,
# snowbro2, tekipaki, pipibibs, enmadaio, mahoudai, shippumd, kbash2, bbakraid,
# sstriker, клоны) и NEC V25/V30-игры (batsugun, twincobr, wardner…, см. c1nec_).
CPS1_TOA  := toaplan toaplan1 toa_gp9001 toa_palette toa_extratext \
             d_batrider d_battleg d_bbakraid d_enmadaio d_kbash2 \
             d_mahoudai d_pipibibs d_shippumd d_snowbro2 d_tekipaki d_truxton2 \
             d_batsugun d_dogyuun d_fixeight d_kbash d_vfive \
             d_toaplan1 d_twincobr d_wardner d_ghox d_slapfght
OBJ += $(addprefix $(BUILD)/c1t_,$(addsuffix .o,$(CPS1_TOA)))
# NEC CPU (r0.337): ядро + инструкции; API Vez*.
OBJ += $(BUILD)/c1nec_nec_intf.o $(BUILD)/c1nec_nec.o $(BUILD)/c1nec_v25.o
$(BUILD)/c1n_%.o: $(CPS1)/src/burn/drv/neogeo/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1v_%.o: $(CPS1)/src/burn/drv/cave/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1t_%.o: $(CPS1)/src/burn/drv/toaplan/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# Sega System 16 (r0.383, V2 семейств): 16A/16B драйвер + график/ран/FD1094.
CPS1_SEG  := d_sys16a d_sys16b d_sys18 sys16_gfx sys16_run sys16_fd1094 genesis_vid fd1094 fd1094_intf sega_315_5195 fd1089 sega_stubs
OBJ += $(addprefix $(BUILD)/c1sg_,$(addsuffix .o,$(CPS1_SEG)))
$(BUILD)/c1sg_%.o: $(CPS1)/src/burn/drv/sega/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# Звук toaplan: YM3812 (OPL2, pipibibs/tekipaki), YMZ280B (PCM, bbakraid).
# Файлы в snd/; c1se_ — отдельный префикс от игровых c1t_.
CPS1_SND3 := burn_ym3812 ymz280b
OBJ += $(addprefix $(BUILD)/c1se_,$(addsuffix .o,$(CPS1_SND3)))
$(BUILD)/c1se_%.o: $(CPS1)/src/burn/snd/%.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# NEC V20/V25/V30/V33 — Toaplan б/з nec (r0.337): batsugun, dogyuun, fixeight,
# kbash, vfive. API nek-intf.: Vez*. Ядро — nec.cpp/v25.cpp (C++) + несинструкции
# necinstr/v25instr/v25sfr (.c); o`.c`-файлы #include внутрь nec.cpp/v25.cpp —
# отдельными единицами компиляции НЕ собирать (см. docs, аудит r0.337).
$(BUILD)/c1nec_nec_intf.o: $(CPS1)/src/cpu/nec_intf.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1nec_nec.o: $(CPS1)/src/cpu/nec/nec.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1nec_v25.o: $(CPS1)/src/cpu/nec/v25.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
# Прочие CPU Toaplan (r0.338): TMS32010 (twincobr/wardner/toaplan1-сеты),
# Z180 (ghox), M6805 (slapfght). z180 — те же op-файлы #include в z180.cpp,
# m6805 — 6805ops.c #include в m6805.cpp; тайто-протектор — корезис taito_m68705.
OBJ += $(BUILD)/c1cpu_tms32010.o $(BUILD)/c1cpu_z180_intf.o $(BUILD)/c1cpu_z180.o \
       $(BUILD)/c1cpu_m6805_intf.o $(BUILD)/c1cpu_m6805.o $(BUILD)/c1cpu_mcs51.o \
       $(BUILD)/c1cpu_i8039.o
$(BUILD)/c1cpu_tms32010.o: $(CPS1)/src/cpu/tms32010/tms32010.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1cpu_z180_intf.o: $(CPS1)/src/cpu/z180_intf.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1cpu_z180.o: $(CPS1)/src/cpu/z180/z180.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1cpu_m6805_intf.o: $(CPS1)/src/cpu/m6805_intf.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1cpu_m6805.o: $(CPS1)/src/cpu/m6805/m6805.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1cpu_mcs51.o: $(CPS1)/src/cpu/i8051/mcs51.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/c1cpu_i8039.o: $(CPS1)/src/cpu/i8039/i8039.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/cps1_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/cps1_host.o: $(TOP)h3_bare/cores/cps1_host.cpp | $(BUILD)
	$(CXX) $(CPS1_CXXFLAGS) $(CPS1_INC) -c -o $@ $<
.PHONY: cps1-obj
cps1-obj: $(CPS1_OBJ)

# ---- PicoDrive (Sega 32X) ----
# Вендор ядра — h3_bare/cores/picodrive (pico/ + cpu/{musashi,cz80,sh2}).
# Голое ядро БЕЗ libretro-обвязки: CPU 68000 -> Musashi (EMU_M68K),
# Z80 -> cz80 (_USE_CZ80), SH2 (32X) -> интерпретатор cpu/sh2 + mame/sh2pico.
# Карман CD/карты (pico/cd) и картриджи carthw НЕ входят в линк — для 32X-рома
# они не нужны, а имена Pico* всё равно присутствуют в других ядрах.
# rename-скрипт picodrive_rename.sh (objcopy --redefine-sym -> pd_*) снимает
# коллизии с gpgx (Musashi m68k*, звук YM2612/SN76496), cps1 (m68k),
# msx/ngp (opll/OPLL*) и прочими под --allow-multiple-definition.
PICO := $(TOP)h3_bare/cores/picodrive
PICO_INC := -I$(PICO) -I$(PICO)/pico -I$(PICO)/cpu/musashi -I$(PICO)/cpu/cz80 \
	-I$(PICO)/zlib -I$(PICO)/platform/common
PICO_CFLAGS := -mcpu=cortex-a7 -mfpu=neon -mfloat-abi=softfp -marm -ffreestanding $(DEPFLAGS) \
	-Wall -Wextra -O2 \
	-DORANGE_PI_ONE -DALLWINNER_BARE_METAL -DNDEBUG \
	-DEMU_M68K -D_USE_CZ80 -DLSB_FIRST -DBYTE_ORDER=LITTLE_ENDIAN \
	-DFRONTEND_SUPPORTS_RGB565 -DUSE_16BPP_RENDERING \
	-Wno-incompatible-pointer-types -Wno-implicit-function-declaration

# SH2 DRC (динкомпилятор): включён по умолчанию, как в эталонном PicoDrive
# (defaultConfig.s_PicoOpt |= POPT_EN_DRC). Без DRC 32X на A7 исполняется
# интерпретатором и не держит 60 fps. Выключить: make clean && make PICO_DRC=0
PICO_DRC ?= 1
ifeq ($(PICO_DRC),1)
PICO_CFLAGS += -DDRC_SH2
PICO_DRC_OBJ := $(BUILD)/pico_sh2_compiler.o $(BUILD)/pico_drc_cmn.o
else
PICO_DRC_OBJ :=
endif

PICO_CORE := pico cart memory state sek z80if videoport draw draw2 mode4 misc eeprom patch debug media sms carthw_cfg
PICO_32X  := 32x memory draw sh2soc pwm
PICO_CD   := mcd memory sek cdc cdd cd_image cd_parse gfx gfx_dma misc pcm megasd
PICO_PICO := pico memory xpcm
PICO_CARTHW := carthw eeprom_spi
PICO_SOUND := sound resampler sn76496 ym2612 ym2413 vgm mix
PICO_OBJ := $(addprefix $(BUILD)/pico_core_,$(addsuffix .o,$(PICO_CORE))) \
	$(addprefix $(BUILD)/pico_32x_,$(addsuffix .o,$(PICO_32X))) \
	$(addprefix $(BUILD)/pico_cd_,$(addsuffix .o,$(PICO_CD))) \
	$(addprefix $(BUILD)/pico_pico_,$(addsuffix .o,$(PICO_PICO))) \
	$(addprefix $(BUILD)/pico_carthw_,$(addsuffix .o,$(PICO_CARTHW))) \
	$(addprefix $(BUILD)/pico_sound_,$(addsuffix .o,$(PICO_SOUND))) \
	$(BUILD)/pico_carthw_svp.o $(BUILD)/pico_carthw_svp_memory.o $(BUILD)/pico_carthw_svp_ssp16.o \
	$(BUILD)/pico_unzip.o \
	$(BUILD)/pico_m68kcpu.o $(BUILD)/pico_m68kops.o \
	$(BUILD)/pico_cz80.o \
	$(BUILD)/pico_sh2_sh2.o $(BUILD)/pico_sh2_sh2dasm.o $(BUILD)/pico_sh2_sh2pico.o \
	$(PICO_DRC_OBJ) \
	$(BUILD)/pico_host.o
OBJ += $(PICO_OBJ)

# Паттерн-правила по каталогам + rename-скрипт (как cps1)
$(BUILD)/pico_core_%.o: $(PICO)/pico/%.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_32x_%.o: $(PICO)/pico/32x/%.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_cd_%.o: $(PICO)/pico/cd/%.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_pico_%.o: $(PICO)/pico/pico/%.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_carthw_%.o: $(PICO)/pico/carthw/%.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_carthw_svp.o: $(PICO)/pico/carthw/svp/svp.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_carthw_svp_memory.o: $(PICO)/pico/carthw/svp/memory.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_carthw_svp_ssp16.o: $(PICO)/pico/carthw/svp/ssp16.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_sound_%.o: $(PICO)/pico/sound/%.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_zlib_%.o: $(PICO)/zlib/%.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_unzip.o: $(PICO)/unzip/unzip.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_m68kcpu.o: $(PICO)/cpu/musashi/m68kcpu.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_m68kops.o: $(PICO)/cpu/musashi/m68kops.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_cz80.o: $(PICO)/cpu/cz80/cz80.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_sh2_sh2.o: $(PICO)/cpu/sh2/sh2.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_sh2_sh2dasm.o: $(PICO)/cpu/sh2/mame/sh2dasm.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_sh2_sh2pico.o: $(PICO)/cpu/sh2/mame/sh2pico.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
$(BUILD)/pico_host.o: $(PICO)/32x_host.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -I$(TOP)h3_bare/src -I$(TOP)h3_bare/platform/fb -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@

# SH2 DRC (только при PICO_DRC=1): компилятор SH2 + общий DRC-слой.
# emit_arm.c отдельно НЕ компилируется — его #include-ит cpu/sh2/compiler.c:481.
# r1172: он же — обязательная зависимость объекта. Без неё правки эмиттера
# (is_offset_24, дальний переход) не пересобирались: make видел только
# compiler.c и оставлял старый pico_sh2_compiler.o с прежним кодом.
$(BUILD)/pico_sh2_compiler.o: $(PICO)/cpu/sh2/compiler.c \
                               $(PICO)/cpu/drc/emit_arm.c \
                               $(PICO)/cpu/drc/cmn.h | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/pico_drc_cmn.o: $(PICO)/cpu/drc/cmn.c | $(BUILD)
	$(CC) $(PICO_CFLAGS) $(PICO_INC) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/picodrive_rename.sh $@.tmp && mv $@.tmp $@
.PHONY: pico-obj
pico-obj: $(PICO_OBJ)

# ---- Правила компиляции ----
$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/startup.o: $(TOP)h3_bare/platform/startup.S | $(BUILD)
	$(AS) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

# r795: прошивка WiFi-чипа RTL8189 (вшивается через .incbin в fw_blob.S)
$(BUILD)/fw_blob.o: $(TOP)h3_bare/platform/fw_blob.S | $(BUILD)
	$(AS) $(CFLAGS) -x assembler-with-cpp -c -o $@ $<

$(BUILD)/%.o: $(TOP)h3_bare/src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -Wno-array-bounds $(INCLUDES) -c -o $@ $<

$(BUILD)/%.o: $(TOP)h3_bare/src/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(BUILD)/%.o: $(TOP)h3_bare/platform/%.c | $(BUILD)
	$(CC) $(CFLAGS) -Wno-array-bounds $(INCLUDES) -c -o $@ $<

$(BUILD)/%.o: $(TOP)h3_bare/platform/fb/%.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

# ---- cores/*.c, cores/*.cpp (общие правила по каталогам) ----
$(BUILD)/menu.o: $(TOP)h3_bare/cores/menu.c | $(BUILD)
	$(CC) $(CFLAGS) -Wno-array-bounds $(INCLUDES) -c -o $@ $<
$(BUILD)/rom_browser.o: $(TOP)h3_bare/cores/rom_browser.c | $(BUILD)
	$(CC) $(CFLAGS) -Wno-array-bounds $(INCLUDES) -c -o $@ $<
$(BUILD)/cheatdb.o: $(TOP)h3_bare/cores/cheatdb.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/sega_pad.o: $(TOP)h3_bare/cores/sega_pad.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/btn_pad.o: $(TOP)h3_bare/cores/btn_pad.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/settings.o: $(TOP)h3_bare/cores/settings.c | $(BUILD)
	$(CC) $(CFLAGS) -Wno-array-bounds $(INCLUDES) -c -o $@ $<
$(BUILD)/sd.o: $(TOP)h3_bare/cores/sd.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/fat.o: $(TOP)h3_bare/cores/fat.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/usb_ohci.o: $(TOP)h3_bare/cores/usb_ohci.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/usb_kbd.o: $(TOP)h3_bare/cores/usb_kbd.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/usb_msd.o: $(TOP)h3_bare/cores/usb_msd.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/usb_hc.o: $(TOP)h3_bare/cores/usb_hc.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/wifi_net.o: $(TOP)h3_bare/cores/wifi_net.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/wifi_layer.o: $(TOP)h3_bare/cores/wifi_layer.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/wpa_crypto.o: $(TOP)h3_bare/cores/wpa_crypto.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/net_tcp.o: $(TOP)h3_bare/cores/net_tcp.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/net_ftp.o: $(TOP)h3_bare/cores/net_ftp.c | $(BUILD)
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
	$(CC) $(CFLAGS) -Wno-array-bounds $(INCLUDES) -c -o $@ $<   # r58: вернул -O2 (r57 -O0 тормозил дисплей)

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
	$(CC) $(CFLAGS) $(INCLUDES) -std=gnu11 -DHAS_SND=1 -c -o $@ $<
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
$(BUILD)/ngp_neopopsound.o: $(TOP)h3_bare/cores/ngp/neopopsound.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_sound.o: $(TOP)h3_bare/cores/ngp/sound.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_ngpBios.o: $(TOP)h3_bare/cores/ngp/ngpBios.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
$(BUILD)/ngp_input.o: $(TOP)h3_bare/cores/ngp/input.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<
# NGP blip-путь (band-limited, как эталон libretro/race): Blip_Buffer из
# PCE mednafen (та же C-версия 0.4.1). Символы Blip_Buffer_* коллизируют с PCE,
# но линковка идёт с --allow-multiple-definition; gc-sections уберёт неиспользуемые.
$(BUILD)/ngp_neopop_blip.o: $(TOP)h3_bare/cores/ngp/neopop_blip.c | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) -DINLINE=inline -I$(TOP)h3_bare/cores/pce_fast/mednafen/include/blip -c -o $@ $<

# r725: sound_init/sound_update в NGP (RACE) имеют ДРУГУЮ сигнатуру, чем в GPGX
# (см. ngp_rename.sh). После сборки ВСЕХ ngp-объектов батч-переименователь
# переименовывает их в ngp_sound_init/ngp_sound_update (согласованно: определения
# neopopsound и ссылки ngp_host) — убирает коллизию с Genesis Plus GX.
NGP_OBJS := $(addprefix $(BUILD)/,ngp_host.o ngp_main.o ngp_memory.o ngp_graphics.o ngp_tlcs900h.o ngp_z80.o ngp_flash.o ngp_neopopsound.o ngp_sound.o ngp_ngpBios.o ngp_input.o ngp_neopop_blip.o)
$(BUILD)/ngp_renamed.stamp: $(NGP_OBJS)
	$(TOP)h3_bare/cores/ngp_rename.sh $(NGP_OBJS)
	touch $@

# ---- GBA ядро mGBA: переименование коллизий crc32/strlcpy (r1132) ----
MGBA_OBJS := $(filter $(addprefix $(BUILD)/,mgba_%),$(OBJ))
$(BUILD)/mgba_renamed.stamp: $(MGBA_OBJS)
	$(TOP)h3_bare/cores/mgba_rename.sh $(MGBA_OBJS)
	touch $@

# ---- GBA ядро mGBA: изолированная компиляция mGBA-core (gba+arm+core+util+gb+inih).
# r1132: полная замена gpsp. У mGBA-core почти все глобалы под уникальными
# префиксами (GBA*/mCore*/mTiming*), поэтому GBA_RENAME не нужен; единственные
# коллизии с другими ядрами — crc32 и strlcpy (mgba_util_*), переименовываются
# скриптом mgba_rename.sh (см. mgba_renamed.stamp). malloc/calloc/free берётся
# из gameboy_stubs.c (bump-аллокатор).
MGBA_DIR := $(TOP)h3_bare/cores/gba_mgba
# r1136: оптимизация СКОРОСТИ интерпретатора mGBA только флагами компилятора
# (логика ядра не тронута). -O3 поверх -O2 из $(CFLAGS) (последний -O-уровень
# побеждает) + -fomit-frame-pointer: для ARMTDMI-декодера/_shift*/load memory это
# меньше прологов и агрессивнее инлайнинг → быстрее диспетчер _armTable/_thumbTable.
# NDEBUG уже отключил assert'ы; frame-pointer не нужен (отладка ядра не ведётся).
MGBA_SPEED := -O3 -fomit-frame-pointer
MGBA_CFLAGS := $(CFLAGS) -DNDEBUG -DMINIMAL_CORE -DM_CORE_GBA -DCOLOR_16_BIT -DCOLOR_5_6_5 -D_BSD_SOURCE -D_DEFAULT_SOURCE -DHAVE_LOCALE $(MGBA_SPEED) -fno-strict-aliasing
MGBA_INC := -I$(MGBA_DIR) -I$(MGBA_DIR)/include $(INCLUDES)

$(BUILD)/gba_mgba_host.o: $(TOP)h3_bare/cores/gba_mgba_host.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/gba_bios_data.o: $(TOP)h3_bare/cores/gba_sp/bios_data.S | $(BUILD)
	$(AS) $(MGBA_CFLAGS) -I$(TOP)h3_bare/cores/gba_sp -x assembler-with-cpp -c -o $@ $<

$(BUILD)/mgba_gba_audio.o: $(MGBA_DIR)/gba/audio.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_bios.o: $(MGBA_DIR)/gba/bios.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_cheats.o: $(MGBA_DIR)/gba/cheats.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_core.o: $(MGBA_DIR)/gba/core.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_dma.o: $(MGBA_DIR)/gba/dma.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_gba.o: $(MGBA_DIR)/gba/gba.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_hle-bios.o: $(MGBA_DIR)/gba/hle-bios.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_input.o: $(MGBA_DIR)/gba/input.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_io.o: $(MGBA_DIR)/gba/io.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_memory.o: $(MGBA_DIR)/gba/memory.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_overrides.o: $(MGBA_DIR)/gba/overrides.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_savedata.o: $(MGBA_DIR)/gba/savedata.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_serialize.o: $(MGBA_DIR)/gba/serialize.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_sharkport.o: $(MGBA_DIR)/gba/sharkport.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_sio.o: $(MGBA_DIR)/gba/sio.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_timer.o: $(MGBA_DIR)/gba/timer.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_video.o: $(MGBA_DIR)/gba/video.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_cart_ereader.o: $(MGBA_DIR)/gba/cart/ereader.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_cart_gpio.o: $(MGBA_DIR)/gba/cart/gpio.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_cart_matrix.o: $(MGBA_DIR)/gba/cart/matrix.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_cart_unlicensed.o: $(MGBA_DIR)/gba/cart/unlicensed.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_cart_vfame.o: $(MGBA_DIR)/gba/cart/vfame.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_cheats_codebreaker.o: $(MGBA_DIR)/gba/cheats/codebreaker.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_cheats_gameshark.o: $(MGBA_DIR)/gba/cheats/gameshark.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_cheats_parv3.o: $(MGBA_DIR)/gba/cheats/parv3.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_renderers_cache-set.o: $(MGBA_DIR)/gba/renderers/cache-set.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_renderers_common.o: $(MGBA_DIR)/gba/renderers/common.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_renderers_gl.o: $(MGBA_DIR)/gba/renderers/gl.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_renderers_software-bg.o: $(MGBA_DIR)/gba/renderers/software-bg.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_renderers_software-mode0.o: $(MGBA_DIR)/gba/renderers/software-mode0.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_renderers_software-obj.o: $(MGBA_DIR)/gba/renderers/software-obj.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_renderers_video-software.o: $(MGBA_DIR)/gba/renderers/video-software.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gba_sio_gbp.o: $(MGBA_DIR)/gba/sio/gbp.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_arm_arm.o: $(MGBA_DIR)/arm/arm.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_arm_decoder-arm.o: $(MGBA_DIR)/arm/decoder-arm.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_arm_decoder-thumb.o: $(MGBA_DIR)/arm/decoder-thumb.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_arm_decoder.o: $(MGBA_DIR)/arm/decoder.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_arm_isa-arm.o: $(MGBA_DIR)/arm/isa-arm.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_arm_isa-thumb.o: $(MGBA_DIR)/arm/isa-thumb.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_bitmap-cache.o: $(MGBA_DIR)/core/bitmap-cache.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_cache-set.o: $(MGBA_DIR)/core/cache-set.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_cheats.o: $(MGBA_DIR)/core/cheats.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_config.o: $(MGBA_DIR)/core/config.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_core.o: $(MGBA_DIR)/core/core.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_directories.o: $(MGBA_DIR)/core/directories.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_input.o: $(MGBA_DIR)/core/input.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_interface.o: $(MGBA_DIR)/core/interface.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_library.o: $(MGBA_DIR)/core/library.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_lockstep.o: $(MGBA_DIR)/core/lockstep.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_log.o: $(MGBA_DIR)/core/log.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_map-cache.o: $(MGBA_DIR)/core/map-cache.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_mem-search.o: $(MGBA_DIR)/core/mem-search.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_rewind.o: $(MGBA_DIR)/core/rewind.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_serialize.o: $(MGBA_DIR)/core/serialize.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_sync.o: $(MGBA_DIR)/core/sync.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_thread.o: $(MGBA_DIR)/core/thread.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_tile-cache.o: $(MGBA_DIR)/core/tile-cache.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_core_timing.o: $(MGBA_DIR)/core/timing.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_audio-buffer.o: $(MGBA_DIR)/util/audio-buffer.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_audio-resampler.o: $(MGBA_DIR)/util/audio-resampler.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_circle-buffer.o: $(MGBA_DIR)/util/circle-buffer.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_configuration.o: $(MGBA_DIR)/util/configuration.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_convolve.o: $(MGBA_DIR)/util/convolve.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_crc32.o: $(MGBA_DIR)/util/crc32.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_elf-read.o: $(MGBA_DIR)/util/elf-read.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_formatting.o: $(MGBA_DIR)/util/formatting.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_gbk-table.o: $(MGBA_DIR)/util/gbk-table.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_geometry.o: $(MGBA_DIR)/util/geometry.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_gui.o: $(MGBA_DIR)/util/gui.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_hash.o: $(MGBA_DIR)/util/hash.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_image.o: $(MGBA_DIR)/util/image.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_interpolator.o: $(MGBA_DIR)/util/interpolator.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_md5.o: $(MGBA_DIR)/util/md5.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_memory.o: $(MGBA_DIR)/util/memory.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_patch-fast.o: $(MGBA_DIR)/util/patch-fast.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_patch-ips.o: $(MGBA_DIR)/util/patch-ips.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_patch-ups.o: $(MGBA_DIR)/util/patch-ups.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_patch.o: $(MGBA_DIR)/util/patch.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_ring-fifo.o: $(MGBA_DIR)/util/ring-fifo.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_sfo.o: $(MGBA_DIR)/util/sfo.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_sha1.o: $(MGBA_DIR)/util/sha1.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_string.o: $(MGBA_DIR)/util/string.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_table.o: $(MGBA_DIR)/util/table.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_text-codec.o: $(MGBA_DIR)/util/text-codec.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_vector.o: $(MGBA_DIR)/util/vector.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_vfs.o: $(MGBA_DIR)/util/vfs.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_vfs_vfs-fifo.o: $(MGBA_DIR)/util/vfs/vfs-fifo.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_vfs_vfs-lzma.o: $(MGBA_DIR)/util/vfs/vfs-lzma.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_util_vfs_vfs-mem.o: $(MGBA_DIR)/util/vfs/vfs-mem.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_gb_audio.o: $(MGBA_DIR)/gb/audio.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<
$(BUILD)/mgba_third-party_inih_ini.o: $(MGBA_DIR)/third-party/inih/ini.c | $(BUILD)
	$(CC) $(MGBA_CFLAGS) $(MGBA_INC) -c -o $@ $<

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
# Линкуем через response-файл linker.rsp: список объектов (~1300 шт.) не
# влезает в командную строку Windows (~32K), а длинный printf в рецепте
# обрезался — часть объектов (mcume_Collision.o) молча терялась и линковка
# падала с «undefined reference to col_state».
# r965: rsp пишется make-функцией $(file) — без внешних процессов и без
# ограничения длины; пути для нативного линкера переводятся в Windows-вид
# ОДНИМ вызовом cygpath (cygpath -m $(CURDIR)), дальше — $(subst).
ifeq ($(shell command -v cygpath 2>/dev/null),)
WINPATH = $1
else
WINROOT := $(shell cygpath -m $(CURDIR))
WINPATH = $(subst //,/,$(subst $(CURDIR)/,$(WINROOT)/,$(1)))
endif
$(ELF): $(OBJ) $(TOP)h3_bare/platform/linker.ld $(BUILD)/ms1504_renamed.stamp $(BUILD)/ngp_renamed.stamp $(BUILD)/mgba_renamed.stamp $(BUILD)/a8_host.o $(BUILD)/a8_fs.o $(BUILD)/a8_renamed.stamp $(BUILD)/cpc_renamed.stamp $(BUILD)/vice_renamed.stamp $(BUILD)/puae_renamed.stamp
	$(file >$(BUILD)/linker.rsp)
	$(foreach o,$(OBJ),$(file >>$(BUILD)/linker.rsp,$(call WINPATH,$(o))))
	$(file >>$(BUILD)/linker.rsp,-lstdc++ -lgcc -lc -lm -lgcc)
	$(LD) -T $(TOP)h3_bare/platform/linker.ld -nostdlib -Wl,-gc-sections \
	    -Wl,--allow-multiple-definition \
	    -o $@ @$(BUILD)/linker.rsp

$(BIN): $(ELF) $(BUILD)/linkcheck.stamp
	$(OBJCOPY) -O binary $(ELF) $@
	cp -f $@ $(TOP)h3_bare.bin
	@echo "--- h3_bare.bin: $$(stat -c%s $@) байт (build/ и корень) ---"

# r703 (C6): проверка целостности линковки обособленным шагом.
# Bare-metal образ самодостаточен: не должно быть НЕопределённых символов
# (undefined refs) в итоговом ELF. Дополнительно ловим «тихие» коллизии objcopy-
# переименований (gc-blargg/msx/bk/gba/cps1): повторное определение одних и тех же
# blargg-символов (Blip_Buffer и т.п. БЕЗ суффикса _gc) означало бы, что gc-объект
# не был переименован и склеился с Lynx-версией (краш Coleco, r0.193).
$(BUILD)/linkcheck.stamp: $(ELF) $(OBJ)
	@echo "linkcheck: undefined symbols + межобъектные дубли глобалов..."
	@if arm-none-eabi-nm $(ELF) | awk '$$1=="U" || $$1=="w" || $$1=="v" {print}' | grep -q .; then \
	    echo "ОШИБКА: в h3_bare.elf есть неразрешённые символы:"; \
	    arm-none-eabi-nm $(ELF) | awk '$$1=="U" || $$1=="w" || $$1=="v" {print "  "$$0}'; \
	    exit 1; \
	fi
	@if printf '%s\n' $(OBJ) | xargs -n 1 arm-none-eabi-nm 2>/dev/null | \
	    awk '$$2=="T"||$$2=="D"||$$2=="B" {print $$3}' | sort | uniq -d | \
	    grep -vE '^(emu_sndInit|BurnYM2612UpdateRequest|emu_sndPlaySound|emu_sndPlayBuzz|emu_FileRead|SndSave_|_Z18system18_io_chip_r[ij]$$|_Z18system18_io_chip_w[ij]t$$|_Z19HamawayGfxBankWritejt$$|_Z20System18GfxBankWritejt$$)$$' | \
	    grep -q .; then \
	    echo "ОШИБКА: межобъектные дубли глобальных символов (T/D/B, >1 .o):"; \
	    printf '%s\n' $(OBJ) | xargs -n 1 arm-none-eabi-nm 2>/dev/null | \
	        awk '$$2=="T"||$$2=="D"||$$2=="B" {print $$3}' | sort | uniq -d | \
	        grep -vE '^(emu_sndInit|BurnYM2612UpdateRequest|emu_sndPlaySound|emu_sndPlayBuzz|emu_FileRead|SndSave_|_Z18system18_io_chip_r[ij]$$|_Z18system18_io_chip_w[ij]t$$|_Z19HamawayGfxBankWritejt$$|_Z20System18GfxBankWritejt$$)$$' | \
	        while read s; do echo "  дубль: $$s"; done; \
	    exit 1; \
	fi
	@touch $@
	@echo "linkcheck: OK"

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

# r1155 (E10): зависимости от .h (см. DEPFLAGS). Нативный gcc пишет в .d
# виндовые пути (E:/...), а MSYS-make внутри работает с /e/... — цели не
# совпали бы, поэтому .d сводятся в build/deps.mk с конвертацией буквы диска.
# -include молчит, если deps.mk ещё нет (первая сборка после make clean).
DEPS_D  := $(sort $(wildcard $(BUILD)/*.d))
DEPS_MK := $(BUILD)/deps.mk
ifneq ($(DEPS_D),)
$(DEPS_MK): $(DEPS_D)
	@sed -E 's#([A-Za-z]):/#/\L\1/#g' $(DEPS_D) > $@
-include $(DEPS_MK)
endif
