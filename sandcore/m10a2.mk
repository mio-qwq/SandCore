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

# 编译器第二个切片另用小输出目录；已验证core/运行时与基盘直接复用。
M10A2_NATIVE_DIR ?= build/m10a2-native-bootstrap
.PHONY: m10a2-native-compiler
m10a2-native-compiler:
	$(MAKE) BUILD=$(M10A2_NATIVE_DIR) M9_BUILD=1 M10_BUILD=1 $(M10A2_NATIVE_DIR)/fs/bin/sccc.scx

# 内核只解析并映射无依赖引导根，不执行库代码。两个校验器与三环
# 运行时共用源文件，确保系统根和第三方库不存在宽松解析分支。
$(BUILD)/scl_boot.o: kernel/scl_boot.c kernel/scl_boot.h user/runtime/SCLFMT.H user/runtime/SCDYN.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/scl_validate.o: user/runtime/scl_format.c user/runtime/SCLFMT.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -Iuser/runtime -c $< -o $@
$(BUILD)/scd_validate.o: user/runtime/scd_format.c user/runtime/SCDYN.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -Iuser/runtime -c $< -o $@

M10A2_DYNAMIC_DIR ?= build/m10a2-dynamic-bootstrap
.PHONY: m10a2-dynamic-bootstrap m10a2-dynamic-artifacts
m10a2-dynamic-bootstrap:
	$(MAKE) BUILD=$(M10A2_DYNAMIC_DIR) M9_BUILD=1 M10_BUILD=1 $(M10A2_DYNAMIC_DIR)/sandcore.img $(M10A2_DYNAMIC_DIR)/fs/SYS/CORE/CORE.SKM $(M10A2_DYNAMIC_DIR)/fs/bin/sccc.scx m10a2-dynamic-artifacts
$(BUILD)/scl-root.o: user/runtime/SCLROOT.C user/runtime/SCLDR.H user/runtime/SCLFMT.H user/runtime/SCDYN.H user/runtime/SCLPRIVATE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -x c -Iuser/runtime -c $< -o $@
$(BUILD)/SCLDR.SCL: $(BUILD)/scl-root.o user/runtime/SCLROOT.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/runtime/SCLROOT.json
$(BUILD)/dynapp.o: tests/m10a2/DYNAPP.C $(STAMP)
	$(CC) $(CFLAGS) -Werror -x c -c $< -o $@
$(BUILD)/DYNAPP.SCX: $(BUILD)/dynapp.o tests/m10a2/DYNAPP.json tools/mkdscx.py tools/scd_format.py tools/mkscl.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/DYNAPP.json
m10a2-dynamic-artifacts: $(BUILD)/SCLDR.SCL $(BUILD)/DYNAPP.SCX
