# bk_rules.mk — правила сборки ядра BK (сгенерировано из bk/Makefile.common + bk_roms.c).

$(BUILD)/bk_access.c.o: $(BK_ROOT)/access.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_boot.c.o: $(BK_ROOT)/boot.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_branch.c.o: $(BK_ROOT)/branch.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_covox.c.o: $(BK_ROOT)/covox.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_double.c.o: $(BK_ROOT)/double.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_ea.c.o: $(BK_ROOT)/ea.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_itab.c.o: $(BK_ROOT)/itab.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_libretro.c.o: $(BK_ROOT)/libretro.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_service.c.o: $(BK_ROOT)/service.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_timer.c.o: $(BK_ROOT)/timer.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_tape.c.o: $(BK_ROOT)/tape.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_disk.c.o: $(BK_ROOT)/disk.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_mouse.c.o: $(BK_ROOT)/mouse.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_single.c.o: $(BK_ROOT)/single.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_weird.c.o: $(BK_ROOT)/weird.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_tty.c.o: $(BK_ROOT)/tty.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_io.c.o: $(BK_ROOT)/io.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_timing.c.o: $(BK_ROOT)/timing.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_sound.c.o: $(BK_ROOT)/sound.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_terakdisk.c.o: $(BK_ROOT)/terakdisk.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_synth.c.o: $(BK_ROOT)/synth.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_emu2149.c.o: $(BK_ROOT)/emu2149.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_main.c.o: $(BK_ROOT)/main.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_scr-libretro.c.o: $(BK_ROOT)/scr-libretro.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_scr.c.o: $(BK_ROOT)/scr.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_tty-libretro.c.o: $(BK_ROOT)/tty-libretro.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_joystick.c.o: $(BK_ROOT)/joystick.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@

$(BUILD)/bk_bk_roms.c.o: $(BK_ROOT)/bk_roms.c | $(BUILD)
	$(CC) $(BKFLAGS) $(BK_INC) $(INCLUDES) -c -o $@.tmp $<
	$(TOP)h3_bare/cores/bk/bk_rename.sh $@.tmp && mv $@.tmp $@
