# 首个SCL引导切片；不调用全应用/冻结游戏目标，不覆盖M10a1验收盘。
M10A2_BUILD_DIR ?= build/m10a2-bootstrap
.PHONY: m10a2-bootstrap m10a2-scl-artifacts
m10a2-bootstrap:
	$(MAKE) BUILD=$(M10A2_BUILD_DIR) M9_BUILD=1 M10_BUILD=1 $(M10A2_BUILD_DIR)/sandcore.img $(M10A2_BUILD_DIR)/fs/SYS/CORE/CORE.SKM m10a2-scl-artifacts

M10A2_RUNTIME_OBJECTS := $(BUILD)/scl-format.o $(BUILD)/scl-runtime.o $(BUILD)/scl-platform.o
$(BUILD)/scl-format.o: user/runtime/scl_format.c user/runtime/SCLFMT.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -Iuser/runtime -c $< -o $@
$(BUILD)/scl-runtime.o: user/runtime/scldr.c user/runtime/SCLDR.H user/runtime/SCLFMT.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -Iuser/runtime -c $< -o $@
$(BUILD)/scl-platform.o: user/runtime/scl_platform.c user/runtime/SCLDR.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -Iuser/runtime -c $< -o $@
$(BUILD)/scl-probe-support.o: tests/m10a2/probe_support.c user/SCAPI.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -Iuser -c $< -o $@
$(BUILD)/scl-probe.o: tests/m10a2/SCLPROBE.C user/runtime/SCLDR.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -x c -Iuser/runtime -c $< -o $@
$(BUILD)/scl-probe.elf: $(BUILD)/user-start.o $(M10A2_RUNTIME_OBJECTS) $(BUILD)/scl-probe.o $(BUILD)/scl-probe-support.o user/linker.ld
	$(LD) -m elf_i386 -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/scl-probe.sym
$(BUILD)/scl-probe.bin: $(BUILD)/scl-probe.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/SCLPROBE.SCX: $(BUILD)/scl-probe.bin tools/mkscx.py
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/scl-probe.sym
$(BUILD)/scl-fixture-%.o: tests/m10a2/lib%.c $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/BASE.SCL: $(BUILD)/scl-fixture-base.o tests/m10a2/libbase.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest tests/m10a2/libbase.json
$(BUILD)/UPPER.SCL: $(BUILD)/scl-fixture-upper.o tests/m10a2/libupper.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest tests/m10a2/libupper.json
m10a2-scl-artifacts: $(BUILD)/SCLPROBE.SCX $(BUILD)/BASE.SCL $(BUILD)/UPPER.SCL
