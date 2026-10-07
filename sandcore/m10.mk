# M10a1独立输出，不覆盖M9用户盘/正式产物，不创建Release。
# 完整实现收齐前仅写规则，不执行本目标。旧M9规则复用独立CLI/音频依赖。
M10_SOURCE_IMAGE ?= $(if $(wildcard build/m9-work/sanddata.img),build/m9-work/sanddata.img,build/baselines/M9-sanddata.img)
.PHONY: m10a1 m10a1-artifacts m10a1-publish
m10a1: m9-environment
	$(MAKE) BUILD=build/m10a1-work M9_BUILD=1 M10_BUILD=1 M9_BASE_IMAGE="$(M10_SOURCE_IMAGE)" M10_SOURCE_IMAGE="$(M10_SOURCE_IMAGE)" m10a1-publish
build/baselines/M9-sanddata.img: tools/published_m10_base.py ../releases/SandCore-M9-build.zip
	$(PY) tools/published_m10_base.py --out $@

ifeq ($(M10_BUILD),1)
# 新主核规则以ELF为权威，不把PE的未知布局当作已验证备用；宿主允许
# WSL构建，MinGW主核支持需要独立格式合同与实际证据后再加。
ifeq ($(OS),Windows_NT)
$(error M10a1 disk CORE requires WSL ELF; use the Windows entry batch to dispatch WSL)
endif
CFLAGS += -minline-all-stringops -DM10_DISK_CORE=1
$(STAMP): m10.mk kernel/core_image.h
OBJS := $(filter-out module.o,$(OBJS)) module2.o core_signature.o monocypher.o monocypher-ed25519.o
LWIP_SOURCES := $(filter-out third_party/lwip/src/core/timeouts.c third_party/lwip/src/core/ipv4/dhcp.c,$(wildcard third_party/lwip/src/core/*.c third_party/lwip/src/core/ipv4/*.c)) third_party/lwip/src/netif/ethernet.c
LWIP_OBJECTS := $(patsubst third_party/lwip/%.c,lwip/%.o,$(LWIP_SOURCES))
NET_FLAGS := -Ikernel/net_port -Ithird_party/lwip/src/include
NET_HEADERS := $(wildcard kernel/net_port/*.h kernel/net_port/arch/*.h)
OBJS += e1000.o network.o net_socket.o net_port/port.o net_port/timeouts.o net_port/dhcp.o net_port/loopback.o $(LWIP_OBJECTS)
CORE_OBJECTS := core_entry.o $(filter-out entry.o,$(OBJS))
LOADER_CFLAGS := $(CFLAGS) -Os -ffunction-sections -fdata-sections -DBOOT_READ_ONLY
# 正式默认只接用户公开结果；空值可明确构建无键/无扩展回退。
# 验收10/20/30及坏格式始终留在独立副本，不混入正常发布树。
M10_PUBLIC_KEY ?= assets/trust/M10-OWNER-ED25519.PUB
M10_SIGNED_DIR ?= $(if $(strip $(M10_PUBLIC_KEY)),modules/signed)
M10_NET_NAMES := ip ifconfig ifup ifdown route arp arping ping traceroute netstat nslookup hostname dnsdomainname ipcalc udhcpc nc wget tftp curl
M10_NET_SCX := $(addprefix $(BUILD)/fs/bin/,$(addsuffix .scx,$(M10_NET_NAMES)))
M10_NET_SOURCE := $(patsubst user/%,$(BUILD)/fs/SYS/SRC/%,$(wildcard user/net/*.c user/net/*.inc))
M10_TEST_SOURCE := $(patsubst tests/m10/%,$(BUILD)/fs/SYS/TEST/%,$(wildcard tests/m10/*.C))
$(M10_TEST_SOURCE): $(BUILD)/fs/SYS/TEST/%: tests/m10/%
	mkdir -p $(dir $@)
	cp $< $@

$(BUILD)/net-cli-%.o: user/net/%.c user/net/NETCLI.inc user/net/HTTP.inc $(wildcard user/*.H) $(STAMP)
	$(CC) $(CFLAGS) -Os -ffunction-sections -fdata-sections -Iuser -c $< -o $@
$(BUILD)/net-cli-%.$(KOUT): $(BUILD)/user-start.o $(BUILD)/net-cli-%.o user/linker.ld
	$(LD) $(LDFLAGS) --gc-sections -T user/linker.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/net-cli-$*.sym
$(BUILD)/net-cli-%.bin: $(BUILD)/net-cli-%.$(KOUT)
	$(OBJCOPY) $(OBJCPYFLAGS) -O binary $< $@
$(M10_NET_SCX): $(BUILD)/fs/bin/%.scx: $(BUILD)/net-cli-%.bin tools/mkscx.py
	mkdir -p $(dir $@)
	$(PY) tools/mkscx.py $< $@ 0 0x400000 $(BUILD)/net-cli-$*.sym

# 客体源码NETCLI使用../头文件，独立探针使用公共INC；定向发布
# 一个网络源码或SCX时也必须收齐两处当前头，不能让旧盘SRC副本
# 掩盖新ABI常量。这里只加资源依赖，不改变SCX正文或旧源码原件。
M10_NET_NATIVE_HEADERS := $(addprefix $(BUILD)/fs/SYS/SRC/,SCAPI.H SCIO.H SCNET.H) $(addprefix $(BUILD)/fs/SYS/INC/,SCAPI.H SCIO.H SCNET.H)
$(M10_NET_SCX) $(M10_NET_SOURCE): | $(M10_NET_NATIVE_HEADERS)

$(BUILD)/lwip/%.o: third_party/lwip/%.c $(NET_HEADERS) $(STAMP)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(NET_FLAGS) -ffunction-sections -fdata-sections -c $< -o $@
$(BUILD)/net_port/%.o: kernel/net_port/%.c $(NET_HEADERS) $(STAMP)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(NET_FLAGS) -ffunction-sections -fdata-sections -c $< -o $@
# 原BSD快照不覆盖；修正默认T2的乘7溢出，实际派生源码须一起归档。
$(BUILD)/net_port/dhcp_upstream.inc: third_party/lwip/src/core/ipv4/dhcp.c tools/prepare_lwip_dhcp.py
	$(PY) tools/prepare_lwip_dhcp.py --source $< --out $@
$(BUILD)/net_port/dhcp.o: NET_FLAGS += -I$(BUILD)/net_port
$(BUILD)/net_port/dhcp.o: $(BUILD)/net_port/dhcp_upstream.inc
$(BUILD)/e1000.o: kernel/e1000.c kernel/e1000.h kernel/paging.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/network.o: kernel/network.c kernel/network.h kernel/net_port/network_internal.h $(STAMP)
	$(CC) $(CFLAGS) $(NET_FLAGS) -c $< -o $@
$(BUILD)/net_socket.o: kernel/net_socket.c kernel/network.h kernel/net_port/network_internal.h $(STAMP)
	$(CC) $(CFLAGS) $(NET_FLAGS) -c $< -o $@
-include $(wildcard $(BUILD)/net_port/*.d $(BUILD)/lwip/src/core/*.d $(BUILD)/lwip/src/core/ipv4/*.d $(BUILD)/lwip/src/netif/*.d)

.PHONY: m10-public-key-check
m10-public-key-check:
$(BUILD)/core_public_key.h: tools/mkcore_public_key.py $(M10_PUBLIC_KEY) m10.mk m10-public-key-check | $(BUILD)
	$(PY) tools/mkcore_public_key.py $(if $(M10_PUBLIC_KEY),--public-key "$(M10_PUBLIC_KEY)") --out $@
$(BUILD)/core-signing/WALL.SKM.msg: $(BUILD)/module-wall-v1.skm tools/mkext.py
	$(PY) tools/mkext.py --legacy $< --number 100 --out $@
$(BUILD)/core_signature.o: kernel/core_signature.c kernel/core_signature.h kernel/crypto.h third_party/monocypher/src/optional/monocypher-ed25519.h third_party/monocypher/src/monocypher.h $(BUILD)/core_public_key.h $(STAMP)
	$(CC) $(CFLAGS) -Ithird_party/monocypher/src -c $< -o $@
$(BUILD)/module2.o: kernel/module2.c kernel/module.h kernel/core_signature.h kernel/objpool.h kernel/fs.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/monocypher.o: third_party/monocypher/src/monocypher.c third_party/monocypher/src/monocypher.h $(STAMP)
	$(CC) $(CFLAGS) -Os -ffunction-sections -fdata-sections -c $< -o $@
$(BUILD)/monocypher-ed25519.o: third_party/monocypher/src/optional/monocypher-ed25519.c third_party/monocypher/src/optional/monocypher-ed25519.h third_party/monocypher/src/monocypher.h $(STAMP)
	$(CC) $(CFLAGS) -Os -ffunction-sections -fdata-sections -Ithird_party/monocypher/src -c $< -o $@

$(BUILD)/core_entry.o: kernel/core_entry.asm $(STAMP)
	$(NASM) -f elf32 $< -o $@
$(BUILD)/core.elf: $(addprefix $(BUILD)/,$(CORE_OBJECTS)) core.ld
	$(LD) -m elf_i386 --gc-sections -T core.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/core.sym
$(BUILD)/core.bin: $(BUILD)/core.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/fs/SYS/CORE/CORE.SKM: $(BUILD)/core.elf $(BUILD)/core.bin tools/mkcore.py
	$(PY) tools/mkcore.py $(BUILD)/core.elf $(BUILD)/core.bin $@

$(BUILD)/loader-entry.o: boot/loader_entry.asm $(STAMP)
	$(NASM) -f elf32 $< -o $@
$(BUILD)/loader-main.o: boot/loader.c kernel/core_image.h kernel/io.h kernel/ata.h kernel/crypto.h kernel/crc.h kernel/palette.h $(BUILD)/font8_data.h $(STAMP)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@
$(BUILD)/loader-ata.o: kernel/ata.c kernel/ata.h kernel/io.h $(STAMP)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@
$(BUILD)/loader-crypto.o: kernel/crypto.c kernel/crypto.h kernel/io.h $(STAMP)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@
$(BUILD)/loader-crc.o: kernel/crc.c kernel/crc.h kernel/io.h $(STAMP)
	$(CC) $(LOADER_CFLAGS) -c $< -o $@
$(BUILD)/loader.elf: $(BUILD)/loader-entry.o $(BUILD)/loader-main.o $(BUILD)/loader-ata.o $(BUILD)/loader-crypto.o $(BUILD)/loader-crc.o boot/loader.ld
	$(LD) -m elf_i386 --gc-sections -T boot/loader.ld $(filter %.o,$^) -o $@
	nm -n $@ > $(BUILD)/loader.sym
$(BUILD)/loader.bin: $(BUILD)/loader.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/sandcore.img: $(BUILD)/boot.bin $(BUILD)/loader.bin tools/mkimg.py
	$(PY) tools/mkimg.py $(BUILD)/boot.bin $(BUILD)/loader.bin $@ --sectors 128

# 只读来源里的旧CORE壁纸先归历史目录，不能把SKM1改名后自动绕过验签。
# 恢复核心首次由本轮核心建立副本；后续发布保留既有恢复副本并记录摘要。
m10a1-artifacts: m9-artifacts $(BUILD)/fs/SYS/CORE/LOGIN.SCX $(BUILD)/fs/apps/session.scx $(USER_SCX) $(BUILD)/module-wall-v1.skm $(BUILD)/core-signing/WALL.SKM.msg $(BUILD)/m10-third-party.stamp $(BUILD)/fs/SYS/FONT/PHOENIX12.TTF $(BUILD)/fs/SYS/FONT/PHOENIX16.TTF $(M10_NET_SCX) $(M10_NET_SOURCE) $(M10_TEST_SOURCE) $(BUILD)/fs/SYS/MAN/NETWORK.MD $(BUILD)/fs/SYS/MAN/NETCLI.MD $(BUILD)/fs/SYS/MAN/M10LIFE.MD
$(BUILD)/fs/SYS/MAN/M10LIFE.MD: docs/M10-LIFECYCLE.md
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/MAN/NETWORK.MD: docs/NETWORK.md
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/MAN/NETCLI.MD: docs/CLI-M10.md
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/FONT/PHOENIX12.TTF: third_party/vonwaon/VonwaonBitmap-12px.ttf
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/fs/SYS/FONT/PHOENIX16.TTF: third_party/vonwaon/VonwaonBitmap-16px.ttf
	mkdir -p $(dir $@)
	cp $< $@
$(BUILD)/m10-third-party.stamp: $(wildcard third_party/monocypher/*.md third_party/monocypher/src/* third_party/monocypher/src/optional/* third_party/vonwaon/*) $(shell find third_party/lwip -type f) $(wildcard kernel/net_port/* kernel/net_port/arch/*) $(BUILD)/net_port/dhcp_upstream.inc tools/prepare_lwip_dhcp.py kernel/network.c kernel/network.h kernel/e1000.c kernel/e1000.h kernel/net_socket.c third_party/M10-SOURCES.json tools/audit_third_party.py docs/THIRD-PARTY.md | $(BUILD)
	$(PY) tools/audit_third_party.py --catalog third_party/M10-SOURCES.json
	mkdir -p $(BUILD)/fs/SYS/LICENSE/MONOCYPHER
	cp -R third_party/monocypher/. $(BUILD)/fs/SYS/LICENSE/MONOCYPHER/
	mkdir -p $(BUILD)/fs/SYS/LICENSE/VONWAON
	cp -R third_party/vonwaon/. $(BUILD)/fs/SYS/LICENSE/VONWAON/
	mkdir -p $(BUILD)/fs/SYS/LICENSE/LWIP $(BUILD)/fs/SYS/NETSRC/PORT
	cp -R third_party/lwip/. $(BUILD)/fs/SYS/LICENSE/LWIP/
	cp -R kernel/net_port/. $(BUILD)/fs/SYS/NETSRC/PORT/
	cp $(BUILD)/net_port/dhcp_upstream.inc $(BUILD)/fs/SYS/NETSRC/PORT/
	cp tools/prepare_lwip_dhcp.py $(BUILD)/fs/SYS/NETSRC/PORT/DHCPPREP.PY
	cp kernel/network.c kernel/network.h kernel/e1000.c kernel/e1000.h kernel/net_socket.c $(BUILD)/fs/SYS/NETSRC/
	cp third_party/M10-SOURCES.json $(BUILD)/fs/SYS/LICENSE/M10.JSON
	touch $@
m10a1-publish: m10a1-artifacts $(M10_SOURCE_IMAGE)
	$(PY) tools/audit_third_party.py --tree "$(BUILD)/fs"
	$(PY) tools/audit_third_party.py --catalog third_party/M10-SOURCES.json --tree "$(BUILD)/fs"
	$(PY) tools/font_coverage.py --out "$(BUILD)/font-coverage.json"
	$(PY) tools/mkfs_m10.py --source-image "$(M10_SOURCE_IMAGE)" --tree "$(BUILD)/fs" --out "$(BUILD)/sanddata.img" --core "$(BUILD)/fs/SYS/CORE/CORE.SKM" --legacy-wall "$(BUILD)/module-wall-v1.skm" $(if $(M10_PUBLIC_KEY),--public-key "$(M10_PUBLIC_KEY)") $(if $(M10_SIGNED_DIR),--signed-dir "$(M10_SIGNED_DIR)") $(if $(filter 1,$(M10_REPLACE)),--replace)
endif
