# 首个SCL引导切片；不调用全应用/冻结游戏目标，不覆盖M10a1验收盘。
M10A2_BUILD_DIR ?= build/m10a2-bootstrap
# SCCC的通用cdecl调用保证4B入口栈，GCC库必须接受该入口条件。
# 库内部仍偏好16B；incoming=2让需要更强对齐的函数自行重排栈，
# 不以“禁SSE所以碰巧能跑”冒充跨工具链调用合同。
M10A2_SCL_CFLAGS := -mincoming-stack-boundary=2 -mpreferred-stack-boundary=4
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
$(BUILD)/scl-root.o: user/runtime/SCLROOT.C user/runtime/SCLDR.H user/runtime/SCLFMT.H user/runtime/SCDYN.H user/runtime/SCLPRIVATE.H user/runtime/SCLJUMPPRIVATE.H user/runtime/scl_termination.c $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/runtime -c $< -o $@
$(BUILD)/SCLDR.SCL: $(BUILD)/scl-root.o user/runtime/SCLROOT.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/runtime/SCLROOT.json
$(BUILD)/dynapp.o: tests/m10a2/DYNAPP.C $(STAMP)
	$(CC) $(CFLAGS) -Werror -x c -c $< -o $@
$(BUILD)/DYNAPP.SCX: $(BUILD)/dynapp.o tests/m10a2/DYNAPP.json tools/mkdscx.py tools/scd_format.py tools/mkscl.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/DYNAPP.json
m10a2-dynamic-artifacts: $(BUILD)/SCLDR.SCL $(BUILD)/DYNAPP.SCX

$(BUILD)/sc32_core.o: kernel/sc32_core.c kernel/sc32_core.h user/SC32API.H user/sc32/SC32WIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/sc32_file.o: kernel/sc32_file.c kernel/sc32_file.h user/sc32/SC32FILEWIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/sc32_gui.o: kernel/sc32_gui.c kernel/sc32_gui.h user/SC32GUI.H user/sc32/SC32GUIWIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/sc32_raw.o: kernel/sc32_raw.c kernel/sc32_raw.h $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@

M10A2_SC32_DIR ?= build/m10a2-sc32-bootstrap
.PHONY: m10a2-sc32-bootstrap m10a2-sc32-artifacts
m10a2-sc32-bootstrap:
	$(MAKE) BUILD=$(M10A2_SC32_DIR) M9_BUILD=1 M10_BUILD=1 $(M10A2_SC32_DIR)/sandcore.img $(M10A2_SC32_DIR)/fs/SYS/CORE/CORE.SKM $(M10A2_SC32_DIR)/fs/bin/sccc.scx m10a2-sc32-artifacts
$(BUILD)/sc32-library.o: user/sc32/SC32API.C user/SC32API.H user/SCTYPES.H user/runtime/SCLDR.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser -c $< -o $@
$(BUILD)/SC32API.SCL: $(BUILD)/sc32-library.o user/sc32/SC32API.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/sc32/SC32API.json
$(BUILD)/sc32-guard.o: tests/m10a2/SC32GUARD.C user/SC32API.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser -c $< -o $@
$(BUILD)/SC32GUARD.SCL: $(BUILD)/sc32-guard.o tests/m10a2/SC32GUARD.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest tests/m10a2/SC32GUARD.json
$(BUILD)/sc32-probe.o: tests/m10a2/SC32PROBE.C user/SC32API.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser -c $< -o $@
$(BUILD)/SC32PROBE.SCX: $(BUILD)/sc32-probe.o tests/m10a2/SC32PROBE.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/SC32PROBE.json
m10a2-sc32-artifacts: $(BUILD)/SCLDR.SCL $(BUILD)/SC32API.SCL $(BUILD)/SC32GUARD.SCL $(BUILD)/SC32PROBE.SCX

# 实际R3窗口过程应用；像素/模型/回调位于应用，内核只投递事实。
$(BUILD)/sc32-twin.o: tests/m10a2/SC32TWIN.C user/SC32API.H user/SC32GUI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser -c $< -o $@
$(BUILD)/SC32TWIN.SCX: $(BUILD)/sc32-twin.o tests/m10a2/SC32TWIN.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/SC32TWIN.json
m10a2-sc32-artifacts: $(BUILD)/SC32TWIN.SCX

# Phase3只构建实际新库/工具链；原M8游戏和全应用规则不在目标中。
M10A2_STDLIB_DIR ?= build/m10a2-stdlib-bootstrap
.PHONY: m10a2-stdlib-bootstrap m10a2-stdlib-artifacts
m10a2-stdlib-bootstrap:
	$(MAKE) BUILD=$(M10A2_STDLIB_DIR) M9_BUILD=1 M10_BUILD=1 $(M10A2_STDLIB_DIR)/sandcore.img $(M10A2_STDLIB_DIR)/fs/SYS/CORE/CORE.SKM $(M10A2_STDLIB_DIR)/fs/bin/sccc.scx m10a2-stdlib-artifacts
$(BUILD)/stdlib-library.o: user/stdlib/SCCLIB.C $(wildcard user/stdlib/*.c user/stdlib/*.h user/stdlib/*.H) user/SC32API.H user/runtime/SCLDR.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser -c $< -o $@
$(BUILD)/SCCLIB.SCL: $(BUILD)/stdlib-library.o user/stdlib/SCCLIB.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/stdlib/SCCLIB.json
$(BUILD)/utility-library.o: user/sclib/SCLIB.C $(wildcard user/sclib/*.c user/sclib/*.H user/stdlib/*.h) user/SC32API.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser -c $< -o $@
$(BUILD)/SCLIB.SCL: $(BUILD)/utility-library.o user/sclib/SCLIB.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/sclib/SCLIB.json
m10a2-stdlib-artifacts: $(BUILD)/SCLDR.SCL $(BUILD)/SC32API.SCL $(BUILD)/SCCLIB.SCL $(BUILD)/SCLIB.SCL
$(BUILD)/stdlib-guard.o: tests/m10a2/STDLIBGUARD.C user/SC32API.H $(wildcard user/stdlib/*.h) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser -c $< -o $@
$(BUILD)/STDLIBGUARD.SCL: $(BUILD)/stdlib-guard.o tests/m10a2/STDLIBGUARD.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest tests/m10a2/STDLIBGUARD.json
$(BUILD)/stdlib-probe.o: tests/m10a2/STDLIBPROBE.C user/SC32API.H $(wildcard user/stdlib/*.h) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser -c $< -o $@
$(BUILD)/STDLIBPROBE.SCX: $(BUILD)/stdlib-probe.o tests/m10a2/STDLIBPROBE.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/STDLIBPROBE.json
m10a2-stdlib-artifacts: $(BUILD)/STDLIBGUARD.SCL $(BUILD)/STDLIBPROBE.SCX
$(BUILD)/utility-probe.o: tests/m10a2/UTILPROBE.C user/sclib/SCLIB.H user/SC32API.H $(wildcard user/stdlib/*.h) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/sclib -Iuser/stdlib -Iuser -c $< -o $@
$(BUILD)/UTILPROBE.SCX: $(BUILD)/utility-probe.o tests/m10a2/UTILPROBE.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/UTILPROBE.json
m10a2-stdlib-artifacts: $(BUILD)/UTILPROBE.SCX

# Phase4统一收齐后使用独立目录，保留Phase3真实产物和原M10a1盘。
M10A2_UI_DIR ?= build/m10a2-ui-bootstrap
.PHONY: m10a2-ui-bootstrap m10a2-ui-artifacts m10a2-ui-resources
m10a2-ui-bootstrap:
	$(MAKE) BUILD=$(M10A2_UI_DIR) M9_BUILD=1 M10_BUILD=1 $(M10A2_UI_DIR)/sandcore.img $(M10A2_UI_DIR)/fs/SYS/CORE/CORE.SKM $(M10A2_UI_DIR)/fs/bin/sccc.scx m10a2-ui-artifacts m10a2-ui-resources
$(BUILD)/sc32_font.o: kernel/sc32_font.c kernel/sc32_font.h user/SC32FONT.H user/fontapi/SC32FONTWIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/sc32_broker.o: kernel/sc32_broker.c kernel/sc32_broker.h user/SC32BROKER.H user/sc32/SC32BROKERWIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/sc32_drop.o: kernel/sc32_drop.c kernel/sc32_drop.h user/SC32DROP.H user/SC32DROP2.H user/dropapi/SC32DROPWIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/scui-library.o: user/scui/SCUI.C user/SCUI.H user/SCUIDROP.H $(wildcard user/scui/*.c user/scui/*.H user/scui/*.inc) user/SC32API.H user/SC32BROKER.H user/SC32DROP.H user/SC32DROP2.H user/SC32FONT.H user/SC32POPUP.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCUI.SCL: $(BUILD)/scui-library.o user/scui/SCUI.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/scui/SCUI.json
$(BUILD)/scui-demo.o: tests/m10a2/SCUIDEMO.C user/SCUI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCUIDEMO.SCX: $(BUILD)/scui-demo.o tests/m10a2/SCUIDEMO.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/SCUIDEMO.json
$(BUILD)/platform-probe.o: tests/m10a2/PLATFORMPROBE.C user/SC32API.H user/SC32BROKER.H user/SC32FONT.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/PLATFORMPROBE.SCX: $(BUILD)/platform-probe.o tests/m10a2/PLATFORMPROBE.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/PLATFORMPROBE.json
m10a2-ui-artifacts: $(BUILD)/SCLDR.SCL $(BUILD)/SC32API.SCL $(BUILD)/SCCLIB.SCL $(BUILD)/SCLIB.SCL $(BUILD)/SCUI.SCL $(BUILD)/SCUIDEMO.SCX $(BUILD)/PLATFORMPROBE.SCX
assets/ui/UAX15_1.SCU: tools/mkui_textdata.py $(wildcard third_party/unicode15_1/*)
	$(PY) tools/mkui_textdata.py
$(BUILD)/fs/SYS/UI/UAX15_1.SCU: assets/ui/UAX15_1.SCU
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/m10a2-ui-resources.stamp: $(wildcard third_party/unicode15_1/* tests/m10a2/unicode15_1/*) third_party/M10A2-SOURCES.json docs/THIRD-PARTY.md tools/audit_third_party.py $(BUILD)/fs/SYS/UI/UAX15_1.SCU
	$(PY) tools/audit_third_party.py --catalog third_party/M10A2-SOURCES.json
	mkdir -p $(BUILD)/fs/SYS/LICENSE/UNICODE15_1
	cp third_party/unicode15_1/* $(BUILD)/fs/SYS/LICENSE/UNICODE15_1/
	mkdir -p $(BUILD)/fs/SYS/LICENSE/UAX15_1TEST
	cp tests/m10a2/unicode15_1/* $(BUILD)/fs/SYS/LICENSE/UAX15_1TEST/
	cp third_party/M10A2-SOURCES.json $(BUILD)/fs/SYS/LICENSE/M10A2.JSON
	cp docs/THIRD-PARTY.md $(BUILD)/fs/SYS/LICENSE/THIRDPARTY.MD
	$(PY) tools/audit_third_party.py --catalog third_party/M10A2-SOURCES.json --tree $(BUILD)/fs
	touch $@
m10a2-ui-resources: $(BUILD)/m10a2-ui-resources.stamp

$(BUILD)/scui-text.o: tests/m10a2/SCUITEXT.C user/SCUI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCUITEXT.SCX: $(BUILD)/scui-text.o tests/m10a2/SCUITEXT.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/SCUITEXT.json
m10a2-ui-artifacts: $(BUILD)/SCUITEXT.SCX
assets/ui/tests/GRAPHEME.SCT assets/ui/tests/WORD.SCT &: tools/mkui_texttests.py tests/m10a2/unicode15_1/PROVENANCE.json tests/m10a2/unicode15_1/GraphemeBreakTest.txt tests/m10a2/unicode15_1/WordBreakTest.txt
	$(PY) tools/mkui_texttests.py
m10a2-ui-resources: assets/ui/tests/GRAPHEME.SCT assets/ui/tests/WORD.SCT

# SCUI实际所需aggregate调用：仅两个交叉工具链回归，不扩旧系统矩阵。
$(BUILD)/aggregate-callee.o: tests/m10a2/AGGCALLEE.C tests/m10a2/AGGABI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser -c $< -o $@
$(BUILD)/AGGCALLEE.SCL: $(BUILD)/aggregate-callee.o tests/m10a2/AGGCALLEE.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest tests/m10a2/AGGCALLEE.json
$(BUILD)/aggregate-probe.o: tests/m10a2/AGGPROBE.C tests/m10a2/AGGABI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser -c $< -o $@
$(BUILD)/AGGPROBE.SCX: $(BUILD)/aggregate-probe.o tests/m10a2/AGGPROBE.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest tests/m10a2/AGGPROBE.json
m10a2-ui-artifacts: $(BUILD)/AGGCALLEE.SCL $(BUILD)/AGGPROBE.SCX

# 租约正文保持独立新selector，短步清理由可信task/main接线。
$(BUILD)/sc32_lease.o: kernel/sc32_lease.c kernel/sc32_lease.h kernel/fs_lease.h user/SC32LEASE.H user/leaseapi/SC32LEASEWIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/fs.o: $(wildcard kernel/fs_lease*.inc) kernel/fs_lease.h user/SC32LEASE.H user/leaseapi/SC32LEASEWIRE.H
$(BUILD)/sc32_file.o: kernel/sc32_file_lease.inc kernel/fs_lease.h user/SC32LEASE.H
$(BUILD)/sc32-library.o: user/leaseapi/leaseapi.c user/leaseapi/SC32LEASEWIRE.H user/SC32LEASE.H

# 会话寿命启动底座；与Shell角色接线同批开启，旧受管作业不变。
$(BUILD)/sc32_host.o: kernel/sc32_host.c kernel/sc32_host.h user/SC32HOST.H user/hostapi/SC32HOSTWIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/streams.o: kernel/streams_launch.inc
$(BUILD)/sc32_broker.o: kernel/broker_shell.inc
$(BUILD)/wm.o: kernel/wm_shell.inc kernel/sc32_host.h
$(BUILD)/scui-library.o: user/scui/ui_shell_paint.inc
SHELL_HOST_IMAGE ?= $(BUILD)/SCSHELL.SCX
# 正式原生收尾显式指向已核来源的G1产物，不把HOST摘要授给不同字节。
.PHONY: m10a2-shell-issue-force
m10a2-shell-issue-force:
$(BUILD)/shell_host_issue.h: $(SHELL_HOST_IMAGE) tools/mk_shell_issue.py m10a2-shell-issue-force
	$(PY) tools/mk_shell_issue.py --host $(SHELL_HOST_IMAGE) --out $@
$(BUILD)/task.o: kernel/shell_issue.inc $(BUILD)/shell_host_issue.h
$(BUILD)/sc32-library.o: user/SC32HOST.H user/hostapi/SC32HOSTWIRE.H user/hostapi/sc32_launch.c

# Phase5必须统一生成库、worker和桌面，再生成同字节发行摘要，最后链接CORE。
# 这些明确目标不触发封存游戏/影片或全应用构建，也不修改用户原数据盘。
M10A2_SHELL_DIR ?= build/m10a2-shell-bootstrap
.PHONY: m10a2-shell-bootstrap m10a2-shell-artifacts m10a2-shell-install
m10a2-shell-bootstrap:
	$(MAKE) BUILD=$(M10A2_SHELL_DIR) M9_BUILD=1 M10_BUILD=1 $(M10A2_SHELL_DIR)/sandcore.img $(M10A2_SHELL_DIR)/fs/SYS/CORE/CORE.SKM m10a2-shell-artifacts m10a2-ui-resources m10a2-shell-install
$(BUILD)/scshell-library.o: user/scshell/SCSHELL.C user/SCSHELL.H user/SCUIDROP.H user/SCSHELLDROP.H $(wildcard user/scshell/*.c user/scshell/*.H user/scshell/*.inc) user/SC32HOST.H user/SC32LEASE.H user/SCUI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCSHELL.SCL: $(BUILD)/scshell-library.o user/scshell/SCSHELL.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/scshell/SCSHELL.json
$(BUILD)/scfilework.o: user/shellworker/SCFILEWORK.C $(wildcard user/shellworker/*.c user/shellworker/*.H) user/scshell/SCSHELLWIRE.H user/SCSHELL.H user/SC32LEASE.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCFILEWORK.SCX: $(BUILD)/scfilework.o user/shellworker/SCFILEWORK.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/shellworker/SCFILEWORK.json
$(BUILD)/scshell-desktop.o: user/scshell/desktop/SCSHELL.C user/SCUIDROP.H user/SCSHELLDROP.H $(wildcard user/scshell/desktop/*.inc user/scshell/desktop/*.H) user/SCSHELL.H user/SC32HOST.H user/SCUI.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCSHELL.SCX: $(BUILD)/scshell-desktop.o user/scshell/desktop/SCSHELL.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/scshell/desktop/SCSHELL.json
m10a2-shell-artifacts: $(BUILD)/SCLDR.SCL $(BUILD)/SC32API.SCL $(BUILD)/SCCLIB.SCL $(BUILD)/SCLIB.SCL $(BUILD)/SCUI.SCL $(BUILD)/SCSHELL.SCL $(BUILD)/SCFILEWORK.SCX $(BUILD)/SCSHELL.SCX
M10A2_SHELL_LIB_NAMES := SCLDR SC32API SCCLIB SCLIB SCUI SCSHELL
$(addprefix $(BUILD)/fs/SYS/LIB/,$(addsuffix .SCL,$(M10A2_SHELL_LIB_NAMES))): $(BUILD)/fs/SYS/LIB/%.SCL: $(BUILD)/%.SCL
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/SHELL/SCSHELL.SCX: $(SHELL_HOST_IMAGE)
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/SHELL/SCFILEWORK.SCX: $(BUILD)/SCFILEWORK.SCX
	mkdir -p $(dir $@)
	cp $< $@
m10a2-shell-install: $(addprefix $(BUILD)/fs/SYS/LIB/,$(addsuffix .SCL,$(M10A2_SHELL_LIB_NAMES))) $(BUILD)/fs/SYS/SHELL/SCSHELL.SCX $(BUILD)/fs/SYS/SHELL/SCFILEWORK.SCX

$(BUILD)/sc32-library.o: user/SC32PATH.H $(wildcard user/pathapi/*.c user/pathapi/*.H)

# Phase6独立原生对象链接器；只构建开发工具，不重启封存应用/游戏。
# HOST_BOOTSTRAP仍明确标识，真实SCCC→SCO→SCLD客体证据另行记录。
M10A2_OBJECT_DIR ?= build/m10a2-object-bootstrap
.PHONY: m10a2-object-bootstrap m10a2-object-artifacts m10a2-object-install
m10a2-object-bootstrap:
	$(MAKE) BUILD=$(M10A2_OBJECT_DIR) M9_BUILD=1 M10_BUILD=1 $(M10A2_OBJECT_DIR)/fs/bin/sccc.scx m10a2-object-artifacts
$(BUILD)/scld.o: user/scld.c user/SCOFMT.H user/SCDBGFILE.H user/runtime/SCLFMT.H user/runtime/SCDYN.H user/runtime/scl_format.c user/runtime/scd_format.c $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser -c $< -o $@
$(BUILD)/SCLD.SCX: $(BUILD)/scld.o user/SCLD.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/SCLD.json
m10a2-object-artifacts: $(BUILD)/SCLD.SCX
$(BUILD)/fs/BIN/SCLD.SCX: $(BUILD)/SCLD.SCX
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/INC/SCOFMT.H: user/SCOFMT.H
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/INC/SCDBGFILE.H: user/SCDBGFILE.H
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/SRC/SCLD.JSON: user/SCLD.json
	mkdir -p $(dir $@)
	cp $< $@
m10a2-object-install: $(BUILD)/fs/BIN/SCLD.SCX $(BUILD)/fs/SYS/INC/SCOFMT.H $(BUILD)/fs/SYS/INC/SCDBGFILE.H $(BUILD)/fs/SYS/SRC/SCLD.JSON

# Debug2只构建本轮新代码，不启动冻结游戏或复制整盘。
$(BUILD)/sc32_debug.o: kernel/sc32_debug.c kernel/sc32_debug.h kernel/sc32_debug_modules.inc user/SC32DEBUG.H user/debugapi/SC32DEBUGWIRE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -Iuser -Iuser/runtime -Iuser/debugapi -c $< -o $@
$(BUILD)/sc32-library.o: user/debugapi/sc32_debug.c user/debugapi/SC32DEBUGWIRE.H user/SC32DEBUG.H
$(BUILD)/scl-root.o: user/runtime/scl_debug_registry.c user/debugapi/SC32DEBUGWIRE.H user/SC32DEBUG.H
$(BUILD)/debug2.o: user/debug2/DEBUG2.C $(wildcard user/debug2/*.c user/debug2/*.H) user/SC32DEBUG.H user/SCDBGFILE.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser -Iuser/sclib -Iuser/stdlib -Iuser/debug2 -c $< -o $@
$(BUILD)/DEBUG2.SCX: $(BUILD)/debug2.o user/debug2/DEBUG2.json tools/mkdscx.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/debug2/DEBUG2.json
.PHONY: m10a2-debug2-artifacts
m10a2-debug2-artifacts: $(BUILD)/DEBUG2.SCX

include m10a2-apps.mk
$(BUILD)/sc32-library.o: user/SC32WALL.H $(wildcard user/wallapi/*.c user/wallapi/*.H)
$(BUILD)/sc32_core.o: kernel/sc32_wall_query.inc user/SC32WALL.H user/wallapi/SC32WALLWIRE.H
$(BUILD)/wm.o: kernel/wm_wall_snapshot.inc user/SC32WALL.H
$(BUILD)/sc32-library.o: user/SC32AUDIO.H user/SC32SIMD.H $(wildcard user/audioapi/*.c)
$(BUILD)/sc32-library.o: user/SC32CAPTURE.H user/sc32/SC32CAPTUREWIRE.H user/sc32/sc32_capture_api.c
$(BUILD)/sc32_file.o: kernel/sc32_capture_kernel.inc user/SC32CAPTURE.H user/sc32/SC32CAPTUREWIRE.H
$(BUILD)/streams.o: kernel/streams_capture.inc
$(BUILD)/scui-library.o: user/SCUITABS.H user/scui/scui_tabs.inc
$(BUILD)/scshell-library.o: user/SCSHELLABSENT.H
$(BUILD)/sc32-library.o: user/SC32REPLACE.H user/configapi/SC32REPLACEWIRE.H user/configapi/sc32_replace.c
$(BUILD)/fs.o: user/configapi/SC32REPLACEWIRE.H
include m10a2-sound.mk
$(BUILD)/sc32_core.o: kernel/sc32_config_apply.inc user/SC32CONFIG.H user/configapi/SC32CONFIGWIRE.H
$(BUILD)/sc32-library.o: user/SC32CONFIG.H user/configapi/SC32CONFIGWIRE.H user/configapi/sc32_config.c

include m10a2-image.mk
include m10a2-settings.mk
include m10a2-terminal.mk

include m10a2-session.mk
