# 只构建图像平台及两个新组件；旧Canvas/Lens和封存游戏规则不触发。
$(BUILD)/sc32_image.o: kernel/sc32_image.c kernel/sc32_image.h user/imageapi/SC32IMAGEWIRE.H user/SC32IMAGE.H $(STAMP)
	$(CC) $(CFLAGS) -Werror -c $< -o $@
$(BUILD)/image_service.o: kernel/image_memory.inc user/SC32API.H
$(BUILD)/sc32-library.o: user/SC32IMAGE.H $(wildcard user/imageapi/*.c user/imageapi/*.H)
$(BUILD)/scui-library.o: user/SCUIARGB.H user/scui/ui_argb.c
$(BUILD)/scgfx-library.o: user/scgfx/SCGFX.C $(wildcard user/scgfx/*.c user/scgfx/*.H user/scgfx/*.inc user/SCGFX*.H) user/SCUICANVAS.H user/SC32IMAGE.H user/SCUIARGB.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SCGFX.SCL: $(BUILD)/scgfx-library.o user/scgfx/SCGFX.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/scgfx/SCGFX.json
$(BUILD)/imageapp-%.o: user/imageapps/%.C $(wildcard user/imageapps/*.c user/imageapps/*.H) $(wildcard user/SCGFX*.H) user/SCUICANVAS.H $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/CANVAS2.SCX: $(BUILD)/imageapp-CANVAS2.o user/imageapps/CANVAS2.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/imageapps/CANVAS2.json
$(BUILD)/LENS2.SCX: $(BUILD)/imageapp-LENS2.o user/imageapps/LENS2.json tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest user/imageapps/LENS2.json
$(BUILD)/fs/SYS/LIB/SCGFX.SCL: $(BUILD)/SCGFX.SCL
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/APPS/CANVAS2.SCX: $(BUILD)/CANVAS2.SCX
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/APPS/LENS2.SCX: $(BUILD)/LENS2.SCX
	mkdir -p $(dir $@)
	cp $< $@
.PHONY: m10a2-image-artifacts m10a2-image-install
m10a2-image-artifacts: $(BUILD)/SCLDR.SCL $(BUILD)/SC32API.SCL $(BUILD)/SCCLIB.SCL $(BUILD)/SCLIB.SCL $(BUILD)/SCUI.SCL $(BUILD)/SCGFX.SCL $(BUILD)/CANVAS2.SCX $(BUILD)/LENS2.SCX $(BUILD)/fs/SYS/CORE/IMAGE.SCX
m10a2-app-artifacts: m10a2-image-artifacts
m10a2-image-install: m10a2-image-artifacts $(BUILD)/fs/SYS/LIB/SCGFX.SCL $(BUILD)/fs/APPS/CANVAS2.SCX $(BUILD)/fs/APPS/LENS2.SCX
