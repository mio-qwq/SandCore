# 显式追加给既有Makefile；本片段不安装、不覆盖历史程序或修改主库。
SYSTEM_TOOLS_SOURCE_DIR := drafts/m10a2-system-tools
.PHONY: m10a2-system-tools-artifacts
m10a2-system-tools-artifacts: $(BUILD)/CALC2.SCX $(BUILD)/PALETTE2.SCX $(BUILD)/MONITOR2.SCX

$(BUILD)/system-tool-%.o: $(SYSTEM_TOOLS_SOURCE_DIR)/%.C \
    $(wildcard $(SYSTEM_TOOLS_SOURCE_DIR)/*.H $(SYSTEM_TOOLS_SOURCE_DIR)/*.inc) \
    user/SCUI.H user/SC32API.H user/SCSHELL.H user/sclib/SCLIB.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -Wno-unused-function -x c \
	  -Iuser/stdlib -Iuser/sclib -Iuser -I$(SYSTEM_TOOLS_SOURCE_DIR) -c $< -o $@

$(BUILD)/%.SCX: $(BUILD)/system-tool-%.o $(SYSTEM_TOOLS_SOURCE_DIR)/%.json \
    tools/mkdscx.py tools/scd_format.py tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest $(SYSTEM_TOOLS_SOURCE_DIR)/$*.json
