# =====================================================================
#  SandCore OS 构建脚本 —— 双工具链自动切换
#  ---------------------------------------------------------------------
#  【两条构建路线】
#
#  ① WSL / Linux (主力, 正统 ELF 工具链):
#       wsl -e bash -c "cd /mnt/c/.../sandcore && make"
#     ld 用 elf_i386 模拟产出 kernel.elf, 再 objcopy 抻直成纯二进制。
#     nasm 优先用 WSL 内安装的; 没装也能干活 —— 通过 WSL 互操作直接
#     调用 Windows 侧的 nasm.exe (引导扇区汇编成 -f bin 与平台无关)。
#
#  ② Windows MinGW (用户日常):  build.bat  (或 mingw32-make)
#     Windows 的 ld 只会产 PE, 所以走 PE -> objcopy -O binary 的路,
#     --section-alignment 压掉段间隙, -j 只抽取需要的段。
#     环境判断用 OS 环境变量 (Windows 恒为 Windows_NT)。
#     【M3 调试实录】曾用 uname 判断 —— PowerShell 里没有 uname,
#     误判成 Linux 后去调 python3/uname, 报 Error 9009。
#
#  【产物链】
#    boot/boot.asm  --nasm-->   build/boot.bin   (512B 引导扇区)
#    kernel/font16.txt --mkfont.py-> build/font16_data.h (汉字字形表)
#    kernel/*.c/asm --gcc/nasm-> build/*.o
#    *.o + linker.ld --ld-->     build/kernel.elf / kernel.pe
#                    --objcopy-> build/kernel.bin  (纯二进制内核)
#    boot.bin + kernel.bin --mkimg.py-> build/sandcore.img (引导/内核盘)
#    build/files/*      --mkfs.py-> build/sanddata.img (SandFS 数据盘)
#
#  【QEMU 挂盘铁律】引导镜像必须 if=floppy (M3 实录);
#  SandFS 在数据盘 sanddata.img 上 (IDE), 因 ATA 驱动够不着 FDC 软盘 (M5 实录)。
# =====================================================================

SHELL := sh
BUILD := build

.DEFAULT_GOAL := all

# ---------------- 环境探测与工具链选择 ----------------
ifeq ($(OS),Windows_NT)
# ============ 路线②: Windows MinGW 原生 ============
NASM        := nasm
ENTFMT      := win32
CC          := gcc
# --image-base=0: 让脚本地址 (0x10000) 原样生效, 避开例行警告
# --section/file-alignment=16: 压掉 PE 段间隙, 否则纯二进制全是 0 洞
LDFLAGS     := -m i386pe --image-base=0 --section-alignment=16 --file-alignment=16
KOUT        := kernel.pe
OBJCPYFLAGS := -j .text -j .rdata -j .data
PY          := python
QEMU        ?= C:/Program Files/qemu/qemu-system-i386.exe
else
# ============ 路线①: WSL / Linux 正统 ELF ============
NASM        := $(shell command -v nasm 2>/dev/null)
ifeq ($(NASM),)
  NASM      := /mnt/c/Users/Administrator/mingw32/bin/nasm.exe
endif
ENTFMT      := elf32
CC          := gcc
LDFLAGS     := -m elf_i386
KOUT        := kernel.elf
OBJCPYFLAGS :=
PY          := python3
QEMU        ?= qemu-system-i386
endif

LD      := ld
OBJCOPY := objcopy

# -fno-leading-underscore: Windows PE 默认给 C 符号加 "_" 前缀, 关掉它,
#                          汇编与 C 两边符号名完全一致 (ELF 本来就没前缀)
CFLAGS := -m32 -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
          -fno-pie -fno-asynchronous-unwind-tables -fno-exceptions \
          -fno-leading-underscore \
          -I$(BUILD) \
          -O2 -Wall -Wextra

# ---------------- 工具链指纹 ----------------
# 切换工具链后 build/ 里的旧格式 .o 会混链出错。指纹文件变了就全量重编。
STAMP := $(BUILD)/.toolchain-$(KOUT)

$(STAMP): | $(BUILD)
	@rm -f $(BUILD)/.toolchain-*
	@touch $@

# ---------------- 字形表编译 ----------------
# 汉字字形的真正源头是 kernel/font16.txt (人画的文本艺术),
# make 时先编译成 build/font16_data.h, 再被 gfx.o 包含。
# 你改 txt → 重 make → 字形自动进系统。
$(BUILD)/font16_data.h: kernel/font16.txt tools/mkfont.py | $(BUILD)
	$(PY) tools/mkfont.py kernel/font16.txt $@

# ---------------- 产物规则 ----------------
# 内核参与链接的全部目标文件。entry.o 必须排第一 (链接脚本里 _start 在前)。
OBJS := entry.o main.o idt.o interrupts.o timer.o keyboard.o mouse.o \
        memory.o heap.o paging.o gfx.o term.o wm.o task.o ata.o fs.o

all: $(BUILD)/sandcore.img $(BUILD)/sanddata.img

$(BUILD):
	mkdir -p $@

$(BUILD)/boot.bin: boot/boot.asm | $(BUILD)
	$(NASM) -f bin $< -o $@

$(BUILD)/entry.o: kernel/entry.asm $(STAMP)
	$(NASM) -f $(ENTFMT) $< -o $@

$(BUILD)/idt.o: kernel/idt.asm $(STAMP)
	$(NASM) -f $(ENTFMT) $< -o $@

$(BUILD)/main.o: kernel/main.c kernel/palette.h kernel/gfx.h \
                 kernel/interrupts.h kernel/timer.h kernel/keyboard.h \
                 kernel/mouse.h kernel/memory.h kernel/heap.h kernel/ata.h \
                 kernel/fs.h kernel/task.h kernel/wm.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/interrupts.o: kernel/interrupts.c kernel/interrupts.h kernel/io.h \
                       kernel/timer.h kernel/keyboard.h kernel/mouse.h \
                       kernel/wm.h kernel/task.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/timer.o: kernel/timer.c kernel/timer.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/keyboard.o: kernel/keyboard.c kernel/keyboard.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/mouse.o: kernel/mouse.c kernel/mouse.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/memory.o: kernel/memory.c kernel/memory.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/heap.o: kernel/heap.c kernel/heap.h kernel/memory.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gfx.o: kernel/gfx.c kernel/gfx.h kernel/font.h kernel/font16.h \
                kernel/io.h $(BUILD)/font16_data.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/term.o: kernel/term.c kernel/term.h kernel/io.h kernel/palette.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/wm.o: kernel/wm.c kernel/wm.h kernel/gfx.h kernel/term.h kernel/mouse.h \
               kernel/timer.h kernel/palette.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/paging.o: kernel/paging.c kernel/paging.h kernel/memory.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/task.o: kernel/task.c kernel/task.h kernel/memory.h kernel/term.h \
                 kernel/timer.h kernel/fs.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/ata.o: kernel/ata.c kernel/ata.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/fs.o: kernel/fs.c kernel/fs.h kernel/ata.h kernel/io.h $(STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(KOUT): $(addprefix $(BUILD)/,$(OBJS)) linker.ld | $(BUILD)
	$(LD) $(LDFLAGS) -T linker.ld $(addprefix $(BUILD)/,$(OBJS)) -o $@

$(BUILD)/kernel.bin: $(BUILD)/$(KOUT)
	$(OBJCOPY) $(OBJCPYFLAGS) -O binary $< $@

$(BUILD)/sandcore.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin tools/mkimg.py
	$(PY) tools/mkimg.py $(BUILD)/boot.bin $(BUILD)/kernel.bin $@

# SandFS 数据盘: 字体 + 用户程序上盘 (独立于引导软盘, 挂 IDE)
$(BUILD)/sanddata.img: tools/mkfs.py kernel/font16.txt \
                       $(BUILD)/fs/bin/shell.scx $(BUILD)/fs/apps/hello.scx
	$(PY) tools/mkfs.py

$(BUILD)/hello.bin: user/hello.asm | $(BUILD)
	$(NASM) -f bin $< -o $@

$(BUILD)/fs/bin/shell.scx: user/shell.asm tools/mkscx.py | $(BUILD)
	mkdir -p $(BUILD)/fs/bin
	$(PY) tools/mkscx.py $< $@ 0 0x400000

$(BUILD)/fs/apps/hello.scx: user/hello.asm tools/mkscx.py | $(BUILD)
	mkdir -p $(BUILD)/fs/apps
	$(PY) tools/mkscx.py $< $@ 0 0x400000

# ---------------- 运行与辅助 ----------------
run: all
	"$(QEMU)" -drive format=raw,if=floppy,file=$(BUILD)/sandcore.img \
		-drive format=raw,if=ide,file=$(BUILD)/sanddata.img

run-headless: all
	"$(QEMU)" -drive format=raw,if=floppy,file=$(BUILD)/sandcore.img \
		-drive format=raw,if=ide,file=$(BUILD)/sanddata.img \
		-display none -monitor tcp:127.0.0.1:4444,server,nowait \
		-qmp tcp:127.0.0.1:4445,server,nowait

clean:
	rm -rf $(BUILD)

.PHONY: all run run-headless clean
