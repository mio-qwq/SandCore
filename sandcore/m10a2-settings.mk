# 设置和文件安装GUI仅在显式组件目标构建，保留旧Settings与INSTALL。
.PHONY: m10a2-settings-artifacts
m10a2-settings-artifacts: $(BUILD)/SETTINGS2.SCX $(BUILD)/INSTALLER2.SCX
m10a2-app-artifacts: m10a2-settings-artifacts
$(BUILD)/setup-app-%.o: user/settings2/%.C $(wildcard user/settings2/*.H user/settings2/*.inc) $(M10A2_APP_HEADERS) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -Wno-unused-function -x c -Iuser/stdlib -Iuser/sclib -Iuser -Iuser/settings2 -c $< -o $@
$(BUILD)/%.SCX: $(BUILD)/setup-app-%.o user/settings2/%.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/settings2/$*.json
