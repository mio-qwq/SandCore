# 新组件只按显式目标构建，不覆盖历史SCX或触发冻结游戏构建。
M10A2_APP_NAMES := FILES2 NOTES2 DEBUG2 CALC2 PALETTE2 MONITOR2 STUDIO2
M10A2_APP_HEADERS := $(filter-out user/SCUIDROP.H user/SCSHELLDROP.H,$(wildcard user/SC*.H user/sclib/*.H))

$(BUILD)/files2-native.o: user/files2/FILES2.C user/SCUIDROP.H user/SCSHELLDROP.H $(wildcard user/files2/*.H user/files2/*.inc) $(M10A2_APP_HEADERS) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/FILES2.SCX: $(BUILD)/files2-native.o user/files2/FILES2.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/files2/FILES2.json

$(BUILD)/notes2-native.o: user/notes2/NOTES2.C $(wildcard user/notes2/*.H user/notes2/*.c) $(M10A2_APP_HEADERS) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -Wno-misleading-indentation -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/NOTES2.SCX: $(BUILD)/notes2-native.o user/notes2/NOTES2.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/notes2/NOTES2.json

$(BUILD)/system-tool-%.o: user/systemtools/%.C $(wildcard user/systemtools/*.H user/systemtools/*.inc) $(M10A2_APP_HEADERS) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -Wno-unused-function -x c -Iuser/stdlib -Iuser/sclib -Iuser -Iuser/systemtools -c $< -o $@
$(BUILD)/%.SCX: $(BUILD)/system-tool-%.o user/systemtools/%.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/systemtools/$*.json

.PHONY: m10a2-files2-artifacts m10a2-notes2-artifacts m10a2-app-artifacts
m10a2-files2-artifacts: $(BUILD)/FILES2.SCX
m10a2-notes2-artifacts: $(BUILD)/NOTES2.SCX
m10a2-app-artifacts: $(addprefix $(BUILD)/,$(addsuffix .SCX,$(M10A2_APP_NAMES)))

$(BUILD)/studio2-native.o: user/studio2/STUDIO2.C $(wildcard user/studio2/*.H user/studio2/*.c) $(M10A2_APP_HEADERS) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/STUDIO2.SCX: $(BUILD)/studio2-native.o user/studio2/STUDIO2.JSON tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/studio2/STUDIO2.JSON
.PHONY: m10a2-studio2-artifacts
m10a2-studio2-artifacts: $(BUILD)/STUDIO2.SCX
