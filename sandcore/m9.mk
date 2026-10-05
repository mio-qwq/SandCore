# M9独立产物链。源M8a盘只读迁移，冻结的游戏/影片不进入依赖链。
# 干净克隆从仓库精简M8a包提取不可变基线，不再依赖本机旧build目录。
M9_BASE_IMAGE ?= build/baselines/M8a-sanddata.img
M9_BASELINE_INPUTS := tools/published_baseline.py ../releases/MANIFEST.json ../releases/SandCore-M7-build.zip ../releases/SandCore-M8a-build.zip
M9_CLI_NAMES := $(filter-out soundplay,$(basename $(notdir $(wildcard user/m9/*.c))))
M9_CLI_SCX := $(addprefix $(BUILD)/fs/bin/,$(addsuffix .scx,$(M9_CLI_NAMES)))
M9_EXTRA_SOURCE := $(patsubst user/%,$(BUILD)/fs/SYS/SRC/%,$(wildcard user/m9/*.c user/audio/*.c user/audio/*.h user/audio/include/*.h user/audio/vendor/*.h user/audio/vendor/*.md user/audio/flacvendor/*.h user/audio/flacvendor/*.md user/compress/*.c user/compress/*.h user/compress/include/*.h user/compress/vendor/*.c user/compress/vendor/*.h user/compress/vendor/*.md user/pack/*.c user/pack/include/*.h))
M9_NOTICE_NAMES := THIRDPARTY.MD IMAGE.JSON M9.JSON STB.LIC WEBP.LIC WEBP.AUTHORS WEBP.PATENTS DR_MP3.LIC DR_MP3.MD DR_FLAC.LIC DR_FLAC.MD MINIZ.LIC MINIZ.MD VONWAON.LIC
M9_NOTICES := $(addprefix $(BUILD)/fs/SYS/LICENSE/,$(M9_NOTICE_NAMES))
M9_TEST_SOURCE := $(patsubst tests/m9/%,$(BUILD)/fs/SYS/TEST/%,$(wildcard tests/m9/*.C tests/m9/*.c))
M9_MANUALS := $(patsubst assets/m9/man/%,$(BUILD)/fs/SYS/MAN/%,$(wildcard assets/m9/man/*.TXT)) $(BUILD)/fs/SYS/MAN/sccc.TXT $(BUILD)/fs/SYS/MAN/lsmod.TXT $(BUILD)/fs/SYS/MAN/crontab.TXT
M9_VENDOR_INPUTS := $(wildcard third_party/stb/* third_party/libwebp/* third_party/libwebp/src/*/* user/audio/vendor/* user/audio/flacvendor/* user/compress/vendor/* user/pack/bzip2/* user/pack/lzma/* user/pack/lzma/C/* user/pack/lzma/DOC/*)
.PHONY: m9 m9-artifacts m9-publish m9-environment
m9-environment:
	$(PY) tools/check_build_environment.py --nasm "$(NASM)" --art-python "$(ARTPY)"
m9: m9-environment
	$(MAKE) BUILD=build/m9-work M9_BUILD=1 M9_BASE_IMAGE="$(M9_BASE_IMAGE)" m9-publish
build/baselines/M8a-sanddata.img: tools/published_baseline.py ../releases/SandCore-M8a-build.zip
	$(PY) tools/published_baseline.py --version M8a --image-out "$@"
ifeq ($(M9_BUILD),1)

# GCC大聚合初始化会隐式调memset/memcpy；M9不提供标准C运行库。
# 让编译器直接发i386整数字符串指令，未知的真实外部符号仍报错。
# 更新工具链戳令现有对象按同一合同重编，不混用旧flags的半批产物。
CFLAGS += -minline-all-stringops
$(STAMP): m9.mk

# 引导盘及全部新增用户程序可独立完成；打包另设命令，避免重复构建
# 顺手覆盖正在QEMU里被编辑的M9数据盘。重新发布显式M9_REPLACE=1。
m9-artifacts: $(BUILD)/sandcore.img $(BUILD)/fs/SYS/CORE/CORE.SKM $(M9_CLI_SCX) $(BUILD)/fs/bin/sccc.scx $(BUILD)/fs/bin/s3c.scx \
             $(BUILD)/fs/bin/sandasm.scx $(BUILD)/fs/bin/soundplay.scx \
             $(BUILD)/fs/apps/sound.scx $(BUILD)/m9-sounds.stamp \
             $(M9_NOTICES) $(BUILD)/m9-third-party.stamp $(BUILD)/m9-legacy.stamp $(BUILD)/m9-image-service.stamp \
             $(BUILD)/fs/desk/shell.lnk $(BUILD)/fs/desk/sound.lnk \
             $(BUILD)/fs/sys/shell.scf $(BUILD)/fs/sys/sound.scf $(BUILD)/m9-icons.stamp $(SCCC_SOURCE) $(M9_EXTRA_SOURCE) $(M9_TEST_SOURCE) $(M9_MANUALS) $(BUILD)/fs/SYS/TEST/SIMDSTATE.SCX $(BUILD)/fs/SYS/TEST/FPUSTATE.SCX $(BUILD)/fs/SYS/TEST/AUDIOCHECK.SCX $(BUILD)/fs/SYS/TEST/PLAYERTEST.SCX $(BUILD)/m9-fixtures.stamp

$(BUILD)/m9-icons.stamp: tools/make_m9_icons.py assets/design/tokens.json $(M9_BASE_IMAGE) $(BUILD)/m9-visual-baseline.stamp | $(BUILD)
	$(ARTPY) tools/make_m9_icons.py --tree $(BUILD)/fs --preview $(BUILD)/icons --shell-source-image $(M9_BASE_IMAGE)
	touch $@

$(BUILD)/m9-visual-baseline.stamp: tools/install_m9_visual_baseline.py $(M9_BASELINE_INPUTS) | $(BUILD)
	$(PY) tools/install_m9_visual_baseline.py --tree $(BUILD)/fs
	touch $@

# 第二测试源M8-start没有LEGACY，不能依赖开发盘偶然已有这些旧文件。
# 固定验收ZIP只读；只向本次M9树补原字节，不重建旧程序/写冻结树。
$(BUILD)/m9-legacy.stamp: tools/install_m9_legacy.py $(M9_BASELINE_INPUTS) | $(BUILD)
	$(PY) tools/install_m9_legacy.py --tree $(BUILD)/fs
	touch $@
$(BUILD)/m9-image-service.stamp: tools/install_m9_image_service.py $(M9_BASELINE_INPUTS) | $(BUILD)
	$(PY) tools/install_m9_image_service.py --tree $(BUILD)/fs
	touch $@

m9-publish: m9-artifacts $(M9_BASE_IMAGE)
	$(PY) tools/audit_third_party.py --tree "$(BUILD)/fs"
	$(PY) tools/mkfs_m9.py --source-image "$(M9_BASE_IMAGE)" --tree "$(BUILD)/fs" --out "$(BUILD)/sanddata.img" --reuse-identical $(if $(filter 1,$(M9_REPLACE)),--replace)

$(BUILD)/m9-%.o: user/m9/%.c $(wildcard user/*.H user/*.inc) $(STAMP)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -Iuser -c $< -o $@

$(BUILD)/m9-%.$(KOUT): $(BUILD)/user-start.o $(BUILD)/m9-%.o user/linker.ld
	$(LD) $(LDFLAGS) --gc-sections -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/m9-$*.sym

# libbzip2/LZMA核心原快照不修改；stdio及标准堆被私有编译适配隔离。
# 不把这些依赖链接到其它CLI，更不链接进内核。
M9_PACK_NAMES := bzip2 bunzip2 bzcat unlzma lzmacat
M9_PACK_FLAGS := $(CFLAGS) -Os -ffunction-sections -fdata-sections -DBZ_NO_STDIO -Iuser/pack/include -Iuser
M9_BZIP_OBJECTS := $(addprefix $(BUILD)/pack-bzip-,$(addsuffix .o,bzlib blocksort huffman compress decompress crctable randtable))
$(BUILD)/pack-bzip-%.o: user/pack/bzip2/%.c $(wildcard user/pack/bzip2/*.h user/pack/include/*.h) $(STAMP)
	$(CC) $(M9_PACK_FLAGS) -c $< -o $@
$(BUILD)/pack-lzma.o: user/pack/lzma/C/LzmaDec.c $(wildcard user/pack/lzma/C/*.h user/pack/include/*.h) $(STAMP)
	$(CC) $(M9_PACK_FLAGS) -c $< -o $@
$(BUILD)/pack-%.o: user/pack/%.c user/SCPACK.H $(wildcard user/pack/include/*.h user/pack/bzip2/*.h user/pack/lzma/C/*.h) $(STAMP)
	$(CC) $(M9_PACK_FLAGS) -c $< -o $@
$(addprefix $(BUILD)/m9-,$(addsuffix .$(KOUT),$(M9_PACK_NAMES))): $(BUILD)/m9-%.$(KOUT): $(BUILD)/user-start.o $(BUILD)/m9-%.o $(BUILD)/pack-runtime.o $(BUILD)/pack-stream.o $(M9_BZIP_OBJECTS) $(BUILD)/pack-lzma.o user/linker.ld
	$(LD) $(LDFLAGS) --gc-sections -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/m9-$*.sym

# 只有这些命令链接压缩核心；每个命令仍有自己独立main和SCX。
# 私有适配不装入SYS/INC，低层整数DEFLATE不借用音频x87合同。
M9_COMPRESS_NAMES := gzip gunzip zcat unzip
M9_COMPRESS_FLAGS := $(CFLAGS) -Os -ffunction-sections -fdata-sections -Iuser/compress/include -Iuser
$(BUILD)/compress-%.o: user/compress/%.c $(wildcard user/compress/*.h user/compress/include/*.h user/compress/vendor/*.h user/compress/vendor/*.c) $(STAMP)
	$(CC) $(M9_COMPRESS_FLAGS) -c $< -o $@
$(addprefix $(BUILD)/m9-,$(addsuffix .$(KOUT),$(M9_COMPRESS_NAMES))): $(BUILD)/m9-%.$(KOUT): $(BUILD)/user-start.o $(BUILD)/m9-%.o $(BUILD)/compress-deflate.o $(BUILD)/compress-inflate.o $(BUILD)/compress-stream.o user/linker.ld
	$(LD) $(LDFLAGS) --gc-sections -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/m9-$*.sym

$(BUILD)/m9-%.bin: $(BUILD)/m9-%.$(KOUT)
	$(OBJCOPY) $(OBJCPYFLAGS) -O binary $< $@

$(BUILD)/fs/SYS/TEST/%: tests/m9/%
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/MAN/%: assets/m9/man/%
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/MAN/sccc.TXT: assets/m9/man/s3c.TXT
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/MAN/lsmod.TXT: assets/m9/man/insmod.TXT
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/MAN/crontab.TXT: assets/m9/man/crond.TXT
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/m9-simdstate.o: tests/m9/SIMDSTATE.c $(wildcard user/*.H) $(STAMP)
	$(CC) $(CFLAGS) -Os -Iuser -c $< -o $@
$(BUILD)/m9-simdstate.$(KOUT): $(BUILD)/user-start.o $(BUILD)/m9-simdstate.o user/linker.ld
	$(LD) $(LDFLAGS) --gc-sections -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/m9-simdstate.sym
$(BUILD)/fs/SYS/TEST/SIMDSTATE.SCX: $(BUILD)/m9-simdstate.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/m9-simdstate.sym
$(BUILD)/m9-fpustate.o: tests/m9/FPUSTATE.c $(wildcard user/*.H) $(STAMP)
	$(CC) $(CFLAGS) -Os -Iuser -c $< -o $@
$(BUILD)/m9-fpustate.$(KOUT): $(BUILD)/user-start.o $(BUILD)/m9-fpustate.o user/linker.ld
	$(LD) $(LDFLAGS) --gc-sections -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/m9-fpustate.sym
$(BUILD)/fs/SYS/TEST/FPUSTATE.SCX: $(BUILD)/m9-fpustate.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/m9-fpustate.sym
$(BUILD)/m9-audiocheck.o: tests/m9/AUDIOCHECK.c user/SCAUDIO.H $(STAMP)
	$(CC) $(CFLAGS) -Os -Iuser -c $< -o $@
$(BUILD)/m9-audiocheck.$(KOUT): $(BUILD)/user-start.o $(BUILD)/m9-audiocheck.o $(BUILD)/audio-decoder.o $(BUILD)/audio-flac.o $(BUILD)/audio-runtime.o user/linker.ld
	$(LD) $(LDFLAGS) --gc-sections -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/m9-audiocheck.sym
$(BUILD)/fs/SYS/TEST/AUDIOCHECK.SCX: $(BUILD)/m9-audiocheck.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/m9-audiocheck.sym
$(BUILD)/m9-playertest.o: tests/m9/PLAYERTEST.c user/audio/cover.h $(wildcard user/*.H) $(STAMP)
	$(CC) $(CFLAGS) -Os -ffunction-sections -fdata-sections -Iuser -Iuser/audio -c $< -o $@
$(BUILD)/m9-playertest.$(KOUT): $(BUILD)/user-start.o $(BUILD)/m9-playertest.o $(BUILD)/audio-decoder.o $(BUILD)/audio-flac.o $(BUILD)/audio-runtime.o $(BUILD)/audio-cover.o user/linker.ld
	$(LD) $(LDFLAGS) --gc-sections -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/m9-playertest.sym
$(BUILD)/fs/SYS/TEST/PLAYERTEST.SCX: $(BUILD)/m9-playertest.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/m9-playertest.sym
$(BUILD)/m9-fixtures.stamp: tools/m9fixtures.py | $(BUILD)
	$(PY) tools/m9fixtures.py --out $(BUILD)/fs/SYS/TEST
	touch $@

$(M9_CLI_SCX): $(BUILD)/fs/bin/%.scx: $(BUILD)/m9-%.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/m9-$*.sym

$(BUILD)/fs/bin/sccc.scx: $(BUILD)/user-sccc.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/user-sccc.sym

# M9的s3c/sccc是同一编译器的CLI命令名，旧M8树和源码仍保留。
# 只有命令行外壳复用编译核心，不把别名计作两个BusyBox工具。
$(BUILD)/fs/bin/s3c.scx: $(BUILD)/user-sccc.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/user-sccc.sym

$(BUILD)/fs/bin/sandasm.scx: $(BUILD)/user-sandasm.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/user-sandasm.sym

# 音频解码按用户一般授权宿主编译；仅此私有链采用x87，内核仍
# -msoft-float/-mno-sse。任务切换已接扩展状态，CPU无FPU时拒绝MP3。
M9_AUDIO_FLAGS := $(filter-out -msoft-float -O2,$(CFLAGS)) -Os -mfpmath=387 \
                  -ffunction-sections -fdata-sections -Iuser/audio/include -Iuser
$(BUILD)/audio-decoder.o: user/audio/decoder.c user/audio/flac.h user/audio/runtime.h user/SCAUDIO.H user/audio/vendor/dr_mp3.h $(STAMP)
	$(CC) $(M9_AUDIO_FLAGS) -c $< -o $@
$(BUILD)/audio-flac.o: user/audio/flac.c user/audio/flac.h user/audio/runtime.h user/SCAUDIO.H user/audio/flacvendor/dr_flac.h $(STAMP)
	$(CC) $(M9_AUDIO_FLAGS) -c $< -o $@
$(BUILD)/audio-cover.o: user/audio/cover.c user/audio/cover.h user/IMAGECLIENT.inc $(wildcard user/*.H) $(STAMP)
	$(CC) $(CFLAGS) -Os -ffunction-sections -fdata-sections -Iuser -c $< -o $@
$(BUILD)/audio-runtime.o: user/audio/runtime.c user/audio/runtime.h $(STAMP)
	$(CC) $(M9_AUDIO_FLAGS) -c $< -o $@
$(BUILD)/audio-cli.o: user/m9/soundplay.c user/audio/player.c $(wildcard user/*.H user/*.inc) $(STAMP)
	$(CC) $(CFLAGS) -Os -ffunction-sections -fdata-sections -Iuser -c $< -o $@
$(BUILD)/audio-gui.o: user/audio/player.c $(wildcard user/*.H user/*.inc) $(STAMP)
	$(CC) $(CFLAGS) -Os -ffunction-sections -fdata-sections -Iuser -c $< -o $@
# 内嵌封面在播放器私有内存解码，复用原CODEC，不启动服务或写临时盘。
# 两个私有运行时都有64位除法助手，仅重命名本副本的定义避免链接冲突。
M9_COVER_CODEC_OBJS := $(filter-out $(BUILD)/codec/service.o $(BUILD)/codec/runtime.o,$(CODEC_OBJS)) $(BUILD)/cover-codec-runtime.o
# M9私有输出目录也要能从源码生成既有WebP适配，不能依赖旧build缓存。
export SANDCORE_CODEC_ADAPT_DIR := $(BUILD)/codec/webp-adapt
$(BUILD)/cover-codec-runtime.o: user/codec/runtime.c $(CODEC_HEADERS) $(STAMP)
	$(CC) $(CODEC_CFLAGS) -D__udivdi3=cover_udivdi3 -D__umoddi3=cover_umoddi3 -D__divdi3=cover_divdi3 -D__moddi3=cover_moddi3 -c $< -o $@
$(BUILD)/audio-gui.$(KOUT): $(BUILD)/audio-cover.o $(M9_COVER_CODEC_OBJS)
$(BUILD)/m9-playertest.$(KOUT): $(M9_COVER_CODEC_OBJS)
$(BUILD)/audio-%.$(KOUT): $(BUILD)/user-start.o $(BUILD)/audio-%.o $(BUILD)/audio-decoder.o $(BUILD)/audio-flac.o $(BUILD)/audio-runtime.o user/linker.ld
	$(LD) $(LDFLAGS) --gc-sections -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/audio-$*.sym
$(BUILD)/audio-%.bin: $(BUILD)/audio-%.$(KOUT)
	$(OBJCOPY) $(OBJCPYFLAGS) -O binary $< $@
$(BUILD)/fs/bin/soundplay.scx: $(BUILD)/audio-cli.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/audio-cli.sym
$(BUILD)/fs/apps/sound.scx: $(BUILD)/audio-gui.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/audio-gui.sym
$(BUILD)/m9-sounds.stamp: tools/mksounds.py | $(BUILD)
	$(PY) tools/mksounds.py --out $(BUILD)/fs/SYS/SOUND
	touch $@
$(BUILD)/fs/SYS/LICENSE/DR_MP3.LIC: user/audio/vendor/LICENSE user/audio/vendor/DR-MP3-NOTICE user/audio/vendor/MINIMP3-NOTICE
	mkdir -p $(dir $@)
	cat $^ > $@
$(BUILD)/fs/SYS/LICENSE/MINIZ.LIC: user/compress/vendor/LICENSE
	mkdir -p $(dir $@)
	cp $< $@
# 独立声明便于客体内直接阅读，旧CORE/IMAGE.LIC与源快照均保留。
# 只登记构建接线，第一阶段不会调用这些规则。
$(BUILD)/fs/SYS/LICENSE/THIRDPARTY.MD: docs/THIRD-PARTY.md
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/IMAGE.JSON: third_party/SOURCES.json
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/M9.JSON: third_party/M9-SOURCES.json
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/STB.LIC: third_party/stb/LICENSE
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/WEBP.LIC: third_party/libwebp/COPYING
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/WEBP.AUTHORS: third_party/libwebp/AUTHORS
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/WEBP.PATENTS: third_party/libwebp/PATENTS
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/DR_MP3.MD: user/audio/vendor/PROVENANCE.md
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/DR_FLAC.LIC: user/audio/flacvendor/LICENSE user/audio/flacvendor/DR-FLAC-NOTICE
	mkdir -p $(dir $@)
	cat $^ > $@
$(BUILD)/fs/SYS/LICENSE/DR_FLAC.MD: user/audio/flacvendor/PROVENANCE.md
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/MINIZ.MD: user/compress/vendor/PROVENANCE.md
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/LICENSE/VONWAON.LIC: assets/licenses/VONWAON.LIC
	mkdir -p $(dir $@)
	cp $< $@
# 用户要求LICENSE同时归档实际引入的源码，不能只装版权文字。
# 完整固定快照独立存放；仍保留宿主原路径和已有源码接线，便于重编。
$(BUILD)/m9-third-party.stamp: $(M9_VENDOR_INPUTS) third_party/SOURCES.json third_party/M9-SOURCES.json tools/audit_third_party.py | $(BUILD)
	$(PY) tools/audit_third_party.py
	mkdir -p $(BUILD)/fs/SYS/LICENSE/STB $(BUILD)/fs/SYS/LICENSE/LIBWEBP $(BUILD)/fs/SYS/LICENSE/DR_MP3 $(BUILD)/fs/SYS/LICENSE/MINIZ
	cp -R third_party/stb/. $(BUILD)/fs/SYS/LICENSE/STB/
	cp -R third_party/libwebp/. $(BUILD)/fs/SYS/LICENSE/LIBWEBP/
	cp -R user/audio/vendor/. $(BUILD)/fs/SYS/LICENSE/DR_MP3/
	mkdir -p $(BUILD)/fs/SYS/LICENSE/DR_FLAC
	cp -R user/audio/flacvendor/. $(BUILD)/fs/SYS/LICENSE/DR_FLAC/
	cp -R user/compress/vendor/. $(BUILD)/fs/SYS/LICENSE/MINIZ/
	mkdir -p $(BUILD)/fs/SYS/LICENSE/BZIP2 $(BUILD)/fs/SYS/LICENSE/LZMA
	cp -R user/pack/bzip2/. $(BUILD)/fs/SYS/LICENSE/BZIP2/
	cp -R user/pack/lzma/. $(BUILD)/fs/SYS/LICENSE/LZMA/
	touch $@
$(BUILD)/fs/desk/shell.lnk: assets/m9/desk/shell.lnk
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/desk/sound.lnk: assets/m9/desk/sound.lnk
	mkdir -p $(dir $@)
	cp $< $@
endif
