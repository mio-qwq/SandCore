# 终端绘图和文本模型属于独立Ring3库；内核只提供受管PTY。
$(BUILD)/scterm-library.o: user/scterm/SCTERM.C $(wildcard user/scterm/*.H user/scterm/*.c) user/SCUITERM.H user/SCUI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCTERM.SCL: $(BUILD)/scterm-library.o user/scterm/SCTERM.JSON tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/scterm/SCTERM.JSON
$(BUILD)/terminal2-native.o: user/terminal2/TERMINAL2.C $(wildcard user/terminal2/*.H user/terminal2/*.c) user/SCUITERM.H user/SC32TERMINAL.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/TERMINAL2.SCX: $(BUILD)/terminal2-native.o user/terminal2/TERMINAL2.JSON tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/terminal2/TERMINAL2.JSON
$(BUILD)/sc32-library.o: user/SC32TERMINAL.H user/sc32/SC32TERMINALWIRE.H user/sc32/sc32_terminal_api.c
$(BUILD)/sc32_file.o: kernel/sc32_terminal_kernel.inc user/sc32/SC32TERMINALWIRE.H user/SC32TERMINAL.H
$(BUILD)/streams.o: kernel/streams_pty_core.inc kernel/streams_pty_native.inc
.PHONY: m10a2-terminal-artifacts m10a2-terminal-install
m10a2-terminal-artifacts: $(BUILD)/SCTERM.SCL $(BUILD)/TERMINAL2.SCX
m10a2-app-artifacts: m10a2-terminal-artifacts
$(BUILD)/fs/SYS/LIB/SCTERM.SCL: $(BUILD)/SCTERM.SCL
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/APPS/TERMINAL2.SCX: $(BUILD)/TERMINAL2.SCX
	mkdir -p $(dir $@)
	cp $< $@
m10a2-terminal-install: m10a2-terminal-artifacts $(BUILD)/fs/SYS/LIB/SCTERM.SCL $(BUILD)/fs/APPS/TERMINAL2.SCX
