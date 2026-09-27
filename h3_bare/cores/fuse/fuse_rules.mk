# fuse_rules.mk — автогенерированные правила сборки Fuse (194 объекта).
$(BUILD)/fz_bzip2_blocksort.c.o: $(FUSE_ROOT)/bzip2/blocksort.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_bzip2_bzlib.c.o: $(FUSE_ROOT)/bzip2/bzlib.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_bzip2_compress.c.o: $(FUSE_ROOT)/bzip2/compress.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_bzip2_crctable.c.o: $(FUSE_ROOT)/bzip2/crctable.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_bzip2_decompress.c.o: $(FUSE_ROOT)/bzip2/decompress.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_bzip2_huffman.c.o: $(FUSE_ROOT)/bzip2/huffman.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_bzip2_randtable.c.o: $(FUSE_ROOT)/bzip2/randtable.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_compat_compat_posix_string.c.o: $(FUSE_ROOT)/deps/libretro-common/compat/compat_posix_string.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_compat_compat_strcasestr.c.o: $(FUSE_ROOT)/deps/libretro-common/compat/compat_strcasestr.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_compat_compat_strl.c.o: $(FUSE_ROOT)/deps/libretro-common/compat/compat_strl.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_compat_fopen_utf8.c.o: $(FUSE_ROOT)/deps/libretro-common/compat/fopen_utf8.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_encodings_encoding_utf.c.o: $(FUSE_ROOT)/deps/libretro-common/encodings/encoding_utf.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_file_file_path.c.o: $(FUSE_ROOT)/deps/libretro-common/file/file_path.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_file_file_path_io.c.o: $(FUSE_ROOT)/deps/libretro-common/file/file_path_io.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_lists_string_list.c.o: $(FUSE_ROOT)/deps/libretro-common/lists/string_list.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_streams_file_stream.c.o: $(FUSE_ROOT)/deps/libretro-common/streams/file_stream.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_string_stdstring.c.o: $(FUSE_ROOT)/deps/libretro-common/string/stdstring.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_time_rtime.c.o: $(FUSE_ROOT)/deps/libretro-common/time/rtime.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_deps_libretro-common_vfs_vfs_implementation.c.o: $(FUSE_ROOT)/deps/libretro-common/vfs/vfs_implementation.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_breakpoint.c.o: $(FUSE_ROOT)/fuse/debugger/breakpoint.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_command.c.o: $(FUSE_ROOT)/fuse/debugger/command.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_commandl.c.o: $(FUSE_ROOT)/fuse/debugger/commandl.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_commandy.c.o: $(FUSE_ROOT)/fuse/debugger/commandy.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_debugger.c.o: $(FUSE_ROOT)/fuse/debugger/debugger.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_disassemble.c.o: $(FUSE_ROOT)/fuse/debugger/disassemble.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_event.c.o: $(FUSE_ROOT)/fuse/debugger/event.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_expression.c.o: $(FUSE_ROOT)/fuse/debugger/expression.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_system_variable.c.o: $(FUSE_ROOT)/fuse/debugger/system_variable.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_debugger_variable.c.o: $(FUSE_ROOT)/fuse/debugger/variable.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_display.c.o: $(FUSE_ROOT)/fuse/display.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_event.c.o: $(FUSE_ROOT)/fuse/event.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_infrastructure_startup_manager.c.o: $(FUSE_ROOT)/fuse/infrastructure/startup_manager.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_input.c.o: $(FUSE_ROOT)/fuse/input.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_keyboard.c.o: $(FUSE_ROOT)/fuse/keyboard.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_loader.c.o: $(FUSE_ROOT)/fuse/loader.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machine.c.o: $(FUSE_ROOT)/fuse/machine.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_machines_periph.c.o: $(FUSE_ROOT)/fuse/machines/machines_periph.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_pentagon1024.c.o: $(FUSE_ROOT)/fuse/machines/pentagon1024.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_pentagon512.c.o: $(FUSE_ROOT)/fuse/machines/pentagon512.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_pentagon.c.o: $(FUSE_ROOT)/fuse/machines/pentagon.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_scorpion.c.o: $(FUSE_ROOT)/fuse/machines/scorpion.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_spec128.c.o: $(FUSE_ROOT)/fuse/machines/spec128.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_spec16.c.o: $(FUSE_ROOT)/fuse/machines/spec16.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_spec48.c.o: $(FUSE_ROOT)/fuse/machines/spec48.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_spec48_ntsc.c.o: $(FUSE_ROOT)/fuse/machines/spec48_ntsc.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_specplus2a.c.o: $(FUSE_ROOT)/fuse/machines/specplus2a.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_specplus2.c.o: $(FUSE_ROOT)/fuse/machines/specplus2.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_specplus3.c.o: $(FUSE_ROOT)/fuse/machines/specplus3.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_specplus3e.c.o: $(FUSE_ROOT)/fuse/machines/specplus3e.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_spec_se.c.o: $(FUSE_ROOT)/fuse/machines/spec_se.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_tc2048.c.o: $(FUSE_ROOT)/fuse/machines/tc2048.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_tc2068.c.o: $(FUSE_ROOT)/fuse/machines/tc2068.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_machines_ts2068.c.o: $(FUSE_ROOT)/fuse/machines/ts2068.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_memory_pages.c.o: $(FUSE_ROOT)/fuse/memory_pages.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_mempool.c.o: $(FUSE_ROOT)/fuse/mempool.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_module.c.o: $(FUSE_ROOT)/fuse/module.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_periph.c.o: $(FUSE_ROOT)/fuse/periph.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ay.c.o: $(FUSE_ROOT)/fuse/peripherals/ay.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_covox.c.o: $(FUSE_ROOT)/fuse/peripherals/covox.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_dck.c.o: $(FUSE_ROOT)/fuse/peripherals/dck.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_beta.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/beta.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_crc.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/crc.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_didaktik.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/didaktik.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_disciple.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/disciple.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_disk.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/disk.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_fdd.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/fdd.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_opus.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/opus.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_plusd.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/plusd.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_trdos.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/trdos.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_upd_fdc.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/upd_fdc.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_disk_wd_fdc.c.o: $(FUSE_ROOT)/fuse/peripherals/disk/wd_fdc.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_flash_am29f010.c.o: $(FUSE_ROOT)/fuse/peripherals/flash/am29f010.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_fuller.c.o: $(FUSE_ROOT)/fuse/peripherals/fuller.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ide_divide.c.o: $(FUSE_ROOT)/fuse/peripherals/ide/divide.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ide_divmmc.c.o: $(FUSE_ROOT)/fuse/peripherals/ide/divmmc.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ide_divxxx.c.o: $(FUSE_ROOT)/fuse/peripherals/ide/divxxx.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ide_ide.c.o: $(FUSE_ROOT)/fuse/peripherals/ide/ide.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ide_simpleide.c.o: $(FUSE_ROOT)/fuse/peripherals/ide/simpleide.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ide_zxatasp.c.o: $(FUSE_ROOT)/fuse/peripherals/ide/zxatasp.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ide_zxcf.c.o: $(FUSE_ROOT)/fuse/peripherals/ide/zxcf.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ide_zxmmc.c.o: $(FUSE_ROOT)/fuse/peripherals/ide/zxmmc.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_if1.c.o: $(FUSE_ROOT)/fuse/peripherals/if1.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_if2.c.o: $(FUSE_ROOT)/fuse/peripherals/if2.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_joystick.c.o: $(FUSE_ROOT)/fuse/peripherals/joystick.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_kempmouse.c.o: $(FUSE_ROOT)/fuse/peripherals/kempmouse.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_melodik.c.o: $(FUSE_ROOT)/fuse/peripherals/melodik.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_multiface.c.o: $(FUSE_ROOT)/fuse/peripherals/multiface.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_printer.c.o: $(FUSE_ROOT)/fuse/peripherals/printer.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_scld.c.o: $(FUSE_ROOT)/fuse/peripherals/scld.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_sound_sp0256.c.o: $(FUSE_ROOT)/fuse/peripherals/sound/sp0256.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_sound_uspeech.c.o: $(FUSE_ROOT)/fuse/peripherals/sound/uspeech.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_speccyboot.c.o: $(FUSE_ROOT)/fuse/peripherals/speccyboot.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_specdrum.c.o: $(FUSE_ROOT)/fuse/peripherals/specdrum.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_spectranet.c.o: $(FUSE_ROOT)/fuse/peripherals/spectranet.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ttx2000s.c.o: $(FUSE_ROOT)/fuse/peripherals/ttx2000s.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_ula.c.o: $(FUSE_ROOT)/fuse/peripherals/ula.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_peripherals_usource.c.o: $(FUSE_ROOT)/fuse/peripherals/usource.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_phantom_typist.c.o: $(FUSE_ROOT)/fuse/phantom_typist.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_pokefinder_pokefinder.c.o: $(FUSE_ROOT)/fuse/pokefinder/pokefinder.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_pokefinder_pokemem.c.o: $(FUSE_ROOT)/fuse/pokefinder/pokemem.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_psg.c.o: $(FUSE_ROOT)/fuse/psg.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_rectangle.c.o: $(FUSE_ROOT)/fuse/rectangle.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_rzx.c.o: $(FUSE_ROOT)/fuse/rzx.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_slt.c.o: $(FUSE_ROOT)/fuse/slt.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_sound_blipbuffer.c.o: $(FUSE_ROOT)/fuse/sound/blipbuffer.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_sound.c.o: $(FUSE_ROOT)/fuse/sound.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_spectrum.c.o: $(FUSE_ROOT)/fuse/spectrum.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_svg.c.o: $(FUSE_ROOT)/fuse/svg.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_tape.c.o: $(FUSE_ROOT)/fuse/tape.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_timer_native.c.o: $(FUSE_ROOT)/fuse/timer/native.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_timer_timer.c.o: $(FUSE_ROOT)/fuse/timer/timer.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_uidisplay.c.o: $(FUSE_ROOT)/fuse/uidisplay.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_uimedia.c.o: $(FUSE_ROOT)/fuse/uimedia.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_unittests_unittests.c.o: $(FUSE_ROOT)/fuse/unittests/unittests.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_z80_z80.c.o: $(FUSE_ROOT)/fuse/z80/z80.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_z80_z80_debugger_variables.c.o: $(FUSE_ROOT)/fuse/z80/z80_debugger_variables.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_fuse_z80_z80_ops.c.o: $(FUSE_ROOT)/fuse/z80/z80_ops.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_buffer.c.o: $(FUSE_ROOT)/libspectrum/buffer.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_bzip2.c.o: $(FUSE_ROOT)/libspectrum/bzip2.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_creator.c.o: $(FUSE_ROOT)/libspectrum/creator.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_crypto.c.o: $(FUSE_ROOT)/libspectrum/crypto.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_csw.c.o: $(FUSE_ROOT)/libspectrum/csw.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_dck.c.o: $(FUSE_ROOT)/libspectrum/dck.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_ide.c.o: $(FUSE_ROOT)/libspectrum/ide.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_libspectrum.c.o: $(FUSE_ROOT)/libspectrum/libspectrum.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_memory.c.o: $(FUSE_ROOT)/libspectrum/memory.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_microdrive.c.o: $(FUSE_ROOT)/libspectrum/microdrive.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_mmc.c.o: $(FUSE_ROOT)/libspectrum/mmc.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_myglib_garray.c.o: $(FUSE_ROOT)/libspectrum/myglib/garray.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_myglib_ghash.c.o: $(FUSE_ROOT)/libspectrum/myglib/ghash.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_myglib_gslist.c.o: $(FUSE_ROOT)/libspectrum/myglib/gslist.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_plusd.c.o: $(FUSE_ROOT)/libspectrum/plusd.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_pzx_read.c.o: $(FUSE_ROOT)/libspectrum/pzx_read.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_rzx.c.o: $(FUSE_ROOT)/libspectrum/rzx.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_sna.c.o: $(FUSE_ROOT)/libspectrum/sna.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_snap_accessors.c.o: $(FUSE_ROOT)/libspectrum/snap_accessors.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_snapshot.c.o: $(FUSE_ROOT)/libspectrum/snapshot.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_snp.c.o: $(FUSE_ROOT)/libspectrum/snp.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_sp.c.o: $(FUSE_ROOT)/libspectrum/sp.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_symbol_table.c.o: $(FUSE_ROOT)/libspectrum/symbol_table.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_szx.c.o: $(FUSE_ROOT)/libspectrum/szx.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_tap.c.o: $(FUSE_ROOT)/libspectrum/tap.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_tape_accessors.c.o: $(FUSE_ROOT)/libspectrum/tape_accessors.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_tape_block.c.o: $(FUSE_ROOT)/libspectrum/tape_block.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_tape.c.o: $(FUSE_ROOT)/libspectrum/tape.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_tape_set.c.o: $(FUSE_ROOT)/libspectrum/tape_set.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_timings.c.o: $(FUSE_ROOT)/libspectrum/timings.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_tzx_read.c.o: $(FUSE_ROOT)/libspectrum/tzx_read.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_tzx_write.c.o: $(FUSE_ROOT)/libspectrum/tzx_write.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_utilities.c.o: $(FUSE_ROOT)/libspectrum/utilities.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_warajevo_read.c.o: $(FUSE_ROOT)/libspectrum/warajevo_read.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_wav.c.o: $(FUSE_ROOT)/libspectrum/wav.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_z80.c.o: $(FUSE_ROOT)/libspectrum/z80.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_z80em.c.o: $(FUSE_ROOT)/libspectrum/z80em.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_zip.c.o: $(FUSE_ROOT)/libspectrum/zip.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_zlib.c.o: $(FUSE_ROOT)/libspectrum/zlib.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_libspectrum_zxs.c.o: $(FUSE_ROOT)/libspectrum/zxs.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_dir.c.o: $(FUSE_ROOT)/src/compat/dir.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_display.c.o: $(FUSE_ROOT)/src/compat/display.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_fat.c.o: $(FUSE_ROOT)/src/compat/fat.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_file.c.o: $(FUSE_ROOT)/src/compat/file.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_joystick.c.o: $(FUSE_ROOT)/src/compat/joystick.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_keyboard.c.o: $(FUSE_ROOT)/src/compat/keyboard.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_mouse.c.o: $(FUSE_ROOT)/src/compat/mouse.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_osname.c.o: $(FUSE_ROOT)/src/compat/osname.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_paths.c.o: $(FUSE_ROOT)/src/compat/paths.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_sound.c.o: $(FUSE_ROOT)/src/compat/sound.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_timer.c.o: $(FUSE_ROOT)/src/compat/timer.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_compat_ui.c.o: $(FUSE_ROOT)/src/compat/ui.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_coreopt.c.o: $(FUSE_ROOT)/src/coreopt.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_fuse_fuse.c.o: $(FUSE_ROOT)/src/fuse/fuse.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_fuse_options_enumerate.c.o: $(FUSE_ROOT)/src/fuse/options_enumerate.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_fuse_settings.c.o: $(FUSE_ROOT)/src/fuse/settings.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_fuse_snapshot.c.o: $(FUSE_ROOT)/src/fuse/snapshot.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_fuse_ui.c.o: $(FUSE_ROOT)/src/fuse/ui.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_fuse_utils.c.o: $(FUSE_ROOT)/src/fuse/utils.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_libretro.c.o: $(FUSE_ROOT)/src/libretro.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_missing.c.o: $(FUSE_ROOT)/src/missing.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_src_version.c.o: $(FUSE_ROOT)/src/version.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_adler32.c.o: $(FUSE_ROOT)/zlib/adler32.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_compress.c.o: $(FUSE_ROOT)/zlib/compress.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_crc32.c.o: $(FUSE_ROOT)/zlib/crc32.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_deflate.c.o: $(FUSE_ROOT)/zlib/deflate.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_gzclose.c.o: $(FUSE_ROOT)/zlib/gzclose.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_gzlib.c.o: $(FUSE_ROOT)/zlib/gzlib.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_gzread.c.o: $(FUSE_ROOT)/zlib/gzread.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_gzwrite.c.o: $(FUSE_ROOT)/zlib/gzwrite.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_infback.c.o: $(FUSE_ROOT)/zlib/infback.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_inffast.c.o: $(FUSE_ROOT)/zlib/inffast.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_inflate.c.o: $(FUSE_ROOT)/zlib/inflate.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_inftrees.c.o: $(FUSE_ROOT)/zlib/inftrees.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_trees.c.o: $(FUSE_ROOT)/zlib/trees.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_uncompr.c.o: $(FUSE_ROOT)/zlib/uncompr.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/fz_zlib_zutil.c.o: $(FUSE_ROOT)/zlib/zutil.c | $(BUILD)
	$(CC) $(FUSEFLAGS) $(FUSE_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/fuse/fuse_rename.sh $@.tmp && mv $@.tmp $@

