# 既有MP3/FLAC成熟核心沿用户授权宿主编译；真实运行仍在Ring3 SCL。
# 新GUI/worker另由SCCC验证，不把解码器的浮点/64位特例扩到内核。
M10A2_SOUND_COMMON := $(CFLAGS) $(M10A2_SCL_CFLAGS) -Iuser/stdlib -Iuser
M10A2_SOUND_OBJECTS := $(BUILD)/sound-api.o $(BUILD)/sound-runtime.o $(BUILD)/sound-decoder.o $(BUILD)/sound-flac.o
$(BUILD)/sound-api.o: user/scsound/sound_api.c user/scsound/SOUNDPRIVATE.H user/SCSOUND.H user/SC32SIMD.H $(STAMP)
	$(CC) $(M10A2_SOUND_COMMON) -Werror -Wno-misleading-indentation -c $< -o $@
$(BUILD)/sound-runtime.o: user/scsound/sound_runtime.c user/scsound/SOUNDPRIVATE.H user/SCSOUND.H $(STAMP)
	$(CC) $(M10A2_SOUND_COMMON) -Werror -Wno-misleading-indentation -c $< -o $@
$(BUILD)/sound-decoder.o: user/scsound/sound_decoder.c user/scsound/SOUNDPRIVATE.H user/audio/vendor/dr_mp3.h $(STAMP)
	$(CC) $(M10A2_SOUND_COMMON) -mhard-float -mfpmath=387 -mno-sse -mno-mmx -Os -Wno-misleading-indentation -c $< -o $@
$(BUILD)/sound-flac.o: user/scsound/sound_flac.c user/scsound/SOUNDPRIVATE.H user/audio/flacvendor/dr_flac.h $(STAMP)
	$(CC) $(M10A2_SOUND_COMMON) -mhard-float -mfpmath=387 -mno-sse -mno-mmx -Os -Wno-misleading-indentation -c $< -o $@
$(BUILD)/scsound-library.o: $(M10A2_SOUND_OBJECTS)
	$(LD) -m elf_i386 -r $^ -o $@
$(BUILD)/SCSOUND.SCL: $(BUILD)/scsound-library.o user/scsound/SCSOUND.json tools/mkscl.py tools/scl_format.py
	$(PY) tools/mkscl.py $< $@ --manifest user/scsound/SCSOUND.json
.PHONY: m10a2-sound-library
m10a2-sound-library: $(BUILD)/SCSOUND.SCL

# GUI只持有菜单/模型/IPC；解码及设备在独立普通worker中持续运行。
$(BUILD)/sound2-native.o: user/sound2/SOUND2.C $(wildcard user/sound2/*.H user/sound2/*.c) user/systemtools/TOOLS.H user/systemtools/tools_ui.inc $(M10A2_APP_HEADERS) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -Wno-unused-function -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SOUND2.HOST.JSON: $(BUILD)/sound2-native.o user/sound2/SOUND2.JSON tools/derive_m10a2_host_manifest.py
	$(PY) tools/derive_m10a2_host_manifest.py --object $< --template user/sound2/SOUND2.JSON --out $@
$(BUILD)/SOUND2.SCX: $(BUILD)/sound2-native.o $(BUILD)/SOUND2.HOST.JSON tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest $(BUILD)/SOUND2.HOST.JSON
$(BUILD)/soundwork-native.o: user/sound2/SOUNDWORK.C user/sound2/sound_cover.c $(wildcard user/sound2/*.H) $(STAMP)
	$(CC) $(CFLAGS) $(M10A2_SCL_CFLAGS) -Werror -x c -Iuser/stdlib -Iuser/sclib -Iuser -c $< -o $@
$(BUILD)/SOUNDWORK.HOST.JSON: $(BUILD)/soundwork-native.o user/sound2/SOUNDWORK.JSON tools/derive_m10a2_host_manifest.py
	$(PY) tools/derive_m10a2_host_manifest.py --object $< --template user/sound2/SOUNDWORK.JSON --out $@
$(BUILD)/SOUNDWORK.SCX: $(BUILD)/soundwork-native.o $(BUILD)/SOUNDWORK.HOST.JSON tools/mkdscx.py tools/scd_format.py
	$(PY) tools/mkdscx.py $< $@ --manifest $(BUILD)/SOUNDWORK.HOST.JSON
.PHONY: m10a2-sound-artifacts m10a2-sound-install
m10a2-sound-artifacts: $(BUILD)/SCSOUND.SCL $(BUILD)/SOUND2.SCX $(BUILD)/SOUNDWORK.SCX
m10a2-app-artifacts: m10a2-sound-artifacts
$(BUILD)/fs/SYS/LIB/SCSOUND.SCL: $(BUILD)/SCSOUND.SCL
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/APPS/SOUND2.SCX: $(BUILD)/SOUND2.SCX
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/APPS/SOUNDWORK.SCX: $(BUILD)/SOUNDWORK.SCX
	mkdir -p $(dir $@)
	cp $< $@
m10a2-sound-install: m10a2-sound-artifacts $(BUILD)/fs/SYS/LIB/SCSOUND.SCL $(BUILD)/fs/APPS/SOUND2.SCX $(BUILD)/fs/APPS/SOUNDWORK.SCX
