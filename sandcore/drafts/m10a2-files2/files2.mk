# 可选整合片段：显式传给make，不改主Makefile，不自动安装或覆盖旧FILES。
FILES2_SOURCE_DIR := drafts/m10a2-files2

.PHONY: m10a2-files2-artifacts
m10a2-files2-artifacts: $(BUILD)/FILES2.SCX

$(BUILD)/files2-native.o: $(FILES2_SOURCE_DIR)/FILES2.C \
    $(wildcard $(FILES2_SOURCE_DIR)/*.H $(FILES2_SOURCE_DIR)/*.inc) \
    user/SC32API.H user/SC32PATH.H user/SC32LEASE.H user/SC32BROKER.H \
    user/SC32HOST.H user/SCUI.H user/SCSHELL.H user/sclib/SCLIB.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@

$(BUILD)/FILES2.SCX: $(BUILD)/files2-native.o $(FILES2_SOURCE_DIR)/FILES2.json \
    tools/mkdscx.py tools/scd_format.py tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest $(FILES2_SOURCE_DIR)/FILES2.json
