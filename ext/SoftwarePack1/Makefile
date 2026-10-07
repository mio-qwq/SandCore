# =====================================================================
# Makefile —— SandCore_ExtraSoftware_Pack_1 构建脚本
# 产物：dist/SandCore_ExtraSoftware_Pack_1.scx（唯一交付物）
#
# 构建链与主项目完全一致（见 sandcore/Makefile）：
#   gcc freestanding -> ld -T libex/linker.ld (基址 0x400000)
#   -> objcopy 抻平 -> tools/mkscx.py 封 SCX（--icon 内嵌 SCB2）
# Windows MinGW 的 ld 产 PE，objcopy 需显式挑段；Linux ELF 直接抻平。
# 规则用 foreach/eval 显式生成：MinGW make 对"目标与前置都带目录
# 的隐式模式规则"匹配有缺陷，显式规则在两种平台都可靠。
# =====================================================================

PY      ?= python3
NASM    ?= nasm
CC      ?= gcc
LD      ?= ld
OBJCOPY ?= objcopy
NM      ?= nm

# 与主项目 ABI 一致：无 libc/libm/栈保护/PIC；本包使用 -Os；
# -fno-leading-underscore 保证 MinGW PE 下汇编与 C 符号同名。
CFLAGS := -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -fno-pie -fno-asynchronous-unwind-tables -fno-exceptions \
          -fno-leading-underscore -Os -minline-all-stringops -Wall -Wno-unused-function -Wno-misleading-indentation \
          -I../libex -Iinstaller -MMD -MP

ifeq ($(OS),Windows_NT)
  # PE 抻平：只保留装载段；BSS 由 mkscx 从符号表计算、加载器清零
  OBJCPYFLAGS := -j .text -j .rdata -j .rodata -j .data
  NASMFMT     := win32
  NASMDEFS    := -D WIN_COFF
else
  LDFLAGS_EXTRA := -m elf_i386
  OBJCPYFLAGS   :=
  NASMFMT       := elf32
  NASMDEFS      :=
endif

MKSCX := ../tools/mkscx.py
BUILD := build
DIST  := dist
PACK  := SandCore_ExtraSoftware_Pack_1

APPS    := pcalc sheet hexed blocks raider
LIBOBJS := $(BUILD)/exutil.o $(BUILD)/exaudio.o

all: $(DIST)/$(PACK).scx

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/start.o $(BUILD)/exutil.o $(BUILD)/exaudio.o $(BUILD)/installer.o $(BUILD)/payload.o $(foreach a,$(APPS),$(BUILD)/$(a).o $(BUILD)/$(a).scb) $(BUILD)/installer.scb: | $(BUILD)

# ---------------- 共享库 ----------------
$(BUILD)/exutil.o: ../libex/exutil.c ../libex/exutil.h ../libex/SCAPI.H
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/exaudio.o: ../libex/exaudio.c ../libex/exaudio.h ../libex/SCAPI.H
	$(CC) $(CFLAGS) -c $< -o $@

# ---------------- 入口 ----------------
$(BUILD)/start.o: ../libex/start.asm
	$(NASM) -f $(NASMFMT) $(NASMDEFS) -o $@ $<

# ---------------- 应用规则（显式生成） ----------------
define APP_RULES
$(BUILD)/$(1).o: $(1)/$(1).c ../libex/NUI.inc ../libex/exui_ext.inc \
                 ../libex/exutil.h ../libex/exaudio.h ../libex/SCAPI.H
	$$(CC) $$(CFLAGS) -c $$< -o $$@

$(BUILD)/$(1).pe: $(BUILD)/start.o $(BUILD)/$(1).o $$(LIBOBJS) \
                  ../libex/linker.ld
	$$(LD) $$(LDFLAGS_EXTRA) -T ../libex/linker.ld $$(filter %.o,$$^) -o $$@

$(BUILD)/$(1).sym: $(BUILD)/$(1).pe
	$$(NM) -n $$< > $$@

$(BUILD)/$(1).bin: $(BUILD)/$(1).pe
	$$(OBJCOPY) $$(OBJCPYFLAGS) -O binary $$< $$@

$(BUILD)/$(1).scb: icons/$(1).txt ../tools/mkicon.py
	$$(PY) ../tools/mkicon.py $$< $$@

$(BUILD)/$(1).scx: $(BUILD)/$(1).bin $(BUILD)/$(1).scb $(BUILD)/$(1).sym \
                   $$(MKSCX)
	$$(PY) $$(MKSCX) $$< $$@ 0 0x400000 $(BUILD)/$(1).sym \
	      --icon $(BUILD)/$(1).scb
endef
$(foreach a,$(APPS),$(eval $(call APP_RULES,$(a))))

# ---------------- 安装器 ----------------
PAYLOAD := installer/payload.h installer/payload.c

$(PAYLOAD) &: manifest.txt tools/pack_payload.py $(foreach a,$(APPS),$(BUILD)/$(a).scx) \
            $(foreach a,$(APPS),$(BUILD)/$(a).scb) $(wildcard cfg/*.CFG)
	$(PY) tools/pack_payload.py manifest.txt installer/payload.h installer/payload.c

$(BUILD)/installer.o: installer/installer.c $(PAYLOAD) ../libex/NUI.inc \
                      ../libex/exui_ext.inc ../libex/exutil.h \
                      ../libex/SCAPI.H
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/payload.o: installer/payload.c installer/payload.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(PACK).pe: $(BUILD)/start.o $(BUILD)/installer.o \
                     $(BUILD)/payload.o $(LIBOBJS) ../libex/linker.ld
	$(LD) $(LDFLAGS_EXTRA) -T ../libex/linker.ld $(filter %.o,$^) -o $@

$(BUILD)/$(PACK).sym: $(BUILD)/$(PACK).pe
	$(NM) -n $< > $@

$(BUILD)/$(PACK).bin: $(BUILD)/$(PACK).pe
	$(OBJCOPY) $(OBJCPYFLAGS) -O binary $< $@

$(BUILD)/installer.scb: icons/installer.txt ../tools/mkicon.py
	$(PY) ../tools/mkicon.py $< $@

$(DIST)/$(PACK).scx: $(BUILD)/$(PACK).bin $(BUILD)/installer.scb \
                     $(BUILD)/$(PACK).sym $(MKSCX) | $(DIST)
	$(PY) $(MKSCX) $< $@ 0 0x400000 $(BUILD)/$(PACK).sym \
	      --icon $(BUILD)/installer.scb

$(DIST):
	mkdir -p $(DIST)

clean:
	rm -rf $(BUILD) $(DIST) installer/payload.h installer/payload.c

.PHONY: all clean

-include $(wildcard $(BUILD)/*.d)
