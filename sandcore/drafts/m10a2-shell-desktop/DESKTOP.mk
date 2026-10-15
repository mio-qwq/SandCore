# 本文件是Phase5整合候选，不由当前Makefile包含，不会提前构建。
M10A2_DESKTOP_SOURCE := user/scshell/desktop/SCSHELL.C
M10A2_DESKTOP_HEADERS := $(wildcard user/scshell/desktop/*.inc user/scshell/desktop/*.H)
$(BUILD)/scshell-desktop.o: $(M10A2_DESKTOP_SOURCE) $(M10A2_DESKTOP_HEADERS) user/SCSHELL.H user/SC32HOST.H user/SC32LEASE.H user/SCUI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/scshell -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCSHELL.SCX: $(BUILD)/scshell-desktop.o user/scshell/desktop/SCSHELL.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/scshell/desktop/SCSHELL.json
$(BUILD)/fs/SYS/SHELL/SCSHELL.SCX: $(BUILD)/SCSHELL.SCX
	mkdir -p $(dir $@)
	cp $< $@
