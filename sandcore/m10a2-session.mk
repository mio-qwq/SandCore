# 新登录/会话仅走显式目标，旧SESSION/LOGIN成品不覆盖，不改引导默认盘。
$(BUILD)/sc32-library.o: user/SC32AUTH.H user/authapi/sc32_auth.c
$(BUILD)/scui-library.o: user/SCUISECRET.H user/scui/ui_secret.inc user/scui/SECRETCORE.H
$(BUILD)/session2.o: user/session2/SESSION2.C $(wildcard user/session2/*.H user/session2/*.c) user/SC32AUTH.H user/SCUISECRET.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SESSION2.SCX: $(BUILD)/session2.o user/session2/SESSION2.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/session2/SESSION2.json
$(BUILD)/LOGIN2.SCX: $(BUILD)/session2.o user/session2/LOGIN2.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/session2/LOGIN2.json
LOGIN2_ISSUE_IMAGE ?= $(BUILD)/LOGIN2.SCX
LOGIN2_ISSUE_LIBRARIES ?= $(BUILD)
.PHONY: m10a2-login-issue-force m10a2-session-artifacts m10a2-session-install
m10a2-login-issue-force:
$(BUILD)/login_issue.stamp: $(LOGIN2_ISSUE_IMAGE) $(addprefix $(LOGIN2_ISSUE_LIBRARIES)/,SCLDR.SCL SC32API.SCL SCCLIB.SCL SCLIB.SCL SCUI.SCL) assets/ui/UAX15_1.SCU tools/mk_login_issue.py m10a2-login-issue-force
	$(PY) tools/mk_login_issue.py --login $(LOGIN2_ISSUE_IMAGE) --libraries $(LOGIN2_ISSUE_LIBRARIES) --unicode assets/ui/UAX15_1.SCU --program-out $(BUILD)/login_program_issue.h --resources-out $(BUILD)/login_resource_issue.h
	@touch $@
$(BUILD)/login_program_issue.h $(BUILD)/login_resource_issue.h: $(BUILD)/login_issue.stamp
	@test -f $@
$(BUILD)/auth.o: kernel/login_role.inc $(BUILD)/login_resource_issue.h
$(BUILD)/task.o: kernel/login_issue.inc $(BUILD)/login_program_issue.h
$(BUILD)/fs/APPS/SESSION2.SCX: $(BUILD)/SESSION2.SCX
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/CORE/LOGIN2.SCX: $(BUILD)/LOGIN2.SCX
	mkdir -p $(dir $@)
	cp $< $@
m10a2-session-artifacts: $(BUILD)/SCLDR.SCL $(BUILD)/SC32API.SCL $(BUILD)/SCCLIB.SCL $(BUILD)/SCLIB.SCL $(BUILD)/SCUI.SCL $(BUILD)/SESSION2.SCX $(BUILD)/LOGIN2.SCX
m10a2-session-install: m10a2-session-artifacts $(BUILD)/fs/APPS/SESSION2.SCX $(BUILD)/fs/SYS/CORE/LOGIN2.SCX
m10a2-app-artifacts: m10a2-session-artifacts
