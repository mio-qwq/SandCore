# SandCore OS（沙核）

一个从零手写的 32 位 x86 迷你图形操作系统。**不使用 GRUB、不使用 ELF 作为系统格式、不使用 FAT** —— 引导器、内核、图形、Shell、文件系统、可执行格式，自己定义、自己实现；中文点阵统一采用用户 mio 管理的凤凰点阵体，按专用导入工具补字。

```
电源 → BIOS → [自研引导器] → [自研内核] → [自研图形] → [自研窗口系统] → [自研Shell]
```

当前进度：**M6a + M6 自动化验证通过，待用户界面验收**。双盘桌面、ring3 SandShell、分页并发与回收已实测；SandAsm 可在系统内汇编并运行新 SCX，Files/Notes/Palette/Calculator/Mines 均已上机验证，壁纸设置重启恢复。M0-M5 已全部验收；M7 尚未开工。证据与变更见 [docs/M6A.md](docs/M6A.md)、[docs/M6.md](docs/M6.md)。

> 接手/续作必读：根目录 `../HANDOFF.md`（现状 + 剩余清单 + M6/M7 规格）。

**作者: mio** ｜ 全系统字体统一来源: 凤凰点阵体 (CC0, 详见 kernel/font16.txt 头注释), M5 起字体文件上 SandFS 作为默认字体。

![M6 系统内生成并运行程序](build/m6/05-generated-program.png)


## 快速开始

```sh
# Windows 构建（在 sandcore/ 下，优先 WSL ELF）
build.bat
# WSL 内也可直接 make；mingw32-make 是备用路线

# 玩 (弹出 QEMU 窗口)
run.bat            # 或: mingw32-make run

# 无头验证 (自动截屏到 build/shot.png)
build.bat run-headless   # 窗口 A
python tools/snap.py build/shot.png --quit   # 窗口 B

# 自动验收，分别启动自己的 Windows QEMU、复制测试数据盘、验证后退出
python tools/verify_m6.py all
python tools/verify_apps.py
```

前提：QEMU 装在 `C:\Program Files\qemu`（`run.bat` 已写全路径）；WSL 里需要 `gcc / make / nasm`（Ubuntu: `sudo apt install gcc make nasm`）。

> ⚠️ **`if=floppy` 是铁律**：QEMU 缺省把 raw 镜像挂成 IDE 硬盘，我们的
> CHS 软盘寻址会读错磁头——所有启动命令必须带 `if=floppy`（详见 docs/BOOT.md §0）。

## 目录结构

```
sandcore/
├── boot/boot.asm      一级引导器: E820 采集 + 读盘 + VGA 13h + 保护模式
├── kernel/
│   ├── entry.asm      内核入口 (0x10000): 清 .bss → call kmain
│   ├── idt.asm        48 个中断/异常入口桩 (宏生成) + 桩地址表
│   ├── interrupts.c   IDT 装配、PIC 重映射、IRQ 分发表、panic 红屏
│   ├── timer.c        PIT 时钟驱动 (100Hz 心跳, uptime)
│   ├── keyboard.c     PS/2 键盘驱动 (扫描码→ASCII, Shift/CapsLock, 环形缓冲)
│   ├── mouse.c        PS/2 鼠标驱动 (IRQ12, 三字节包, 光标状态)
│   ├── memory.c       E820 解析 + PF-Bitmap 物理页分配器
│   ├── heap.c         SC-Heap v1 内核堆 (8 级尺寸 + 分级空闲链)
│   ├── gfx.c          绘图模块 v2: 双缓冲/线/圆/文字渲染 (SCF-8+SCF-16)
│   ├── term.c         M4 历史终端缓冲（M6 内核不链接，用户窗口流式输出取代）
│   ├── wm.c           窗口管理器: 合成器/拖拽/置顶/关闭/任务栏/光标/panic 屏
│   ├── task.c         任务/调度 v2: 分页版 ring3 + int 0x7C 分发 + SCX 装载
│   ├── paging.c       分页: 恒等映射 + 每任务用户区 (0x400000 私有)
│   ├── ata.c          ATA PIO 驱动 (读 + 写)
│   ├── fs.c           SandFS v3 (96 项可写伪目录 sys/bin/apps/home/desk)
│   ├── main.c         内核主体: 开机画面 + 桌面主循环
│   ├── palette.h      调色板槽位分配 (唯一权威定义, 配 docs/GFX.md 表)
│   ├── font.h         自制 SCF-8 点阵字体 v1.1 (完整 ASCII 95 字形, 逐位手绘)
│   ├── font16.h       SCF-16 中文字体接口 (数据由 font16.txt 编译生成)
│   └── io.h           端口 IO / hlt / 开关中断 (全内核共用)
├── linker.ld          内核链接脚本 (两套工具链共用)
├── user/hello.asm     ring3 窗口程序 (系统调用画图)
├── user/shell.asm     SandShell: ring3 化的 Shell (help/clear/echo/ls/run)
├── user/api.h         当前用户态公共接口（M7 的 scapi.h 尚未实现）
├── user/start.asm     freestanding C 入口与 cdecl 系统调用桥
├── user/assembler.c   SandAsm: 系统内两遍汇编生成 SCX
├── user/*.c           Files / Notes / Palette / Calculator / Mines
├── assets/            文件快捷方式、独立 SCF 图标、示例文本/汇编源
├── tools/
│   ├── mkimg.py       引导/内核盘打包器
│   ├── mkfs.py        SandFS 数据盘打包器 (字体+程序上盘)
│   ├── mkscx.py       SCX1MIO 可执行容器打包器
│   ├── mkscf.py       字体/图标 SCF1MIO 打包（12B 头与内核对齐）
│   ├── verify_m6.py   M6a 行为/分页/画布/生命周期断言与截图
│   ├── verify_apps.py M6 原生汇编/应用/错误/重启验证与截图
│   ├── snap.py        QEMU 截屏 (HMP screendump + PPM→PNG, 纯标准库)
│   ├── keys.py        QEMU 按键注入 (HMP sendkey)
│   ├── mouse.py       QMP 鼠标注入 (move/down/up/click)
│   ├── mkfont.py      字形表编译器 (font16.txt → font16_data.h)
│   └── font_preview.py 字形预览器 (不开机看全部汉字)
├── docs/              ★ 全部自定义规范的详细文档 (见下)
├── run.bat / run.sh   一键运行
└── Makefile           双工具链自动切换 (WSL-ELF 主力 / MinGW-PE 备用)
```

## 文档索引（自定义规范，改代码前必读）

| 文档 | 内容 |
|---|---|
| [docs/BOOT.md](docs/BOOT.md) | **启动协议 v0.4**：`if=floppy` 铁律、双盘布局、E820 契约、引导器↔内核交接、64 扇区合同 |
| [docs/INTR.md](docs/INTR.md) | **中断子系统**：向量号地图、IDT 门格式与栈帧、PIC 重映射、PIT、键盘/鼠标、panic 政策 |
| [docs/SYSCALL.md](docs/SYSCALL.md) | **系统调用 API**：int 0x7C 调用约定、功能号表、用户程序写法 (作者 mio) |
| [docs/FS.md](docs/FS.md) | **磁盘格式**：SandFS 版图 (SANDFSMIO)、SCF1MIO 字体文件、SCX1MIO 可执行 (作者 mio) |
| [docs/MEM.md](docs/MEM.md) | **内存管理**：物理内存版图、E820 解析规则、PF-Bitmap 页分配器、SC-Heap v1 |
| [docs/WM.md](docs/WM.md) | **窗口系统**：鼠标协议、终端缓冲、z 序窗口、合成流水、脏标记模型 |
| [docs/SHELL.md](docs/SHELL.md) | **Shell 规范**：输入流水、分词、命令表、退格守卫、中文命令名路线 |
| [docs/GFX.md](docs/GFX.md) | **图形规范**：VGA 13h 帧缓冲、调色板槽位分配表、自制字体编码、绘图 API、图形控制台 |
| [docs/BUILD.md](docs/BUILD.md) | **构建系统**：双工具链、PE 路线的坑与解法、工具链指纹、镜像打包格式、截屏原理 |
| [docs/ROADMAP.md](docs/ROADMAP.md) | **路线图与 ABI 草案**：里程碑、系统调用/可执行格式/用户态设计、自举（self-hosting）路线 |
| [docs/ASM.md](docs/ASM.md) | **SandAsm**：原生两遍汇编器的语法、机器码、SCX 输出、容量与错误边界 |
| [docs/APPS.md](docs/APPS.md) | **M6 应用**：文件管理器/记事本/调色板/计算器/扫雷，操作与当前限制 |
| [docs/TESTING.md](docs/TESTING.md) | **自测指南**：当前手工操作、自动验收命令及历史里程碑存档 |
| [docs/M6A.md](docs/M6A.md) / [docs/M6.md](docs/M6.md) | **验收记录**：变更清单、行为断言、截图索引与用户待验收状态 |

## 里程碑

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0 | 引导器 + 保护模式内核 + 图形点亮 | ✅ 2026-10-01 |
| M1 | 中断 + 键盘 + 图形控制台（滚动/退格/uptime） | ✅ 2026-10-01 |
| M2 | E820 + 物理页分配器 + SC-Heap + Shell 十条命令 | ✅ 2026-10-01 |
| M3 | gfx 双缓冲模块 + SCF-16 中文点阵字体 + 中文上屏 | ✅ 2026-10-01 |
| M4 | 鼠标驱动 + 窗口系统 + 桌面 + GUI 终端 | ✅ 2026-10-02 |
| M5 | ATA + SandFS + SCX1MIO + ring3 + 系统调用 + 抢占调度 | ✅ 2026-10-02 |
| M6a | 双盘桌面 + ring3 Shell + 分页并发 + 窗口/输入/回收 | 自动化通过，待用户界面验收，2026-10-02 |
| M6 | 原生汇编器 + 用户态应用矩阵 + 持久化壁纸 + 系统调用完善 | 自动化通过，待用户界面验收，2026-10-02 |
| M7 | SCCC 编译器 (s3c) + scapi.h + 内核组件用户态化 | ⬜ |

**自测**：每个里程碑的详细自测指南见 [docs/TESTING.md](docs/TESTING.md)。

## 设计原则

1. **核心组件自己写**：不用 GRUB（引导器手写）、不用 ELF 当系统格式（SCX 自研格式）、不用 FAT（SandFS 自研），系统调用走自己的 `int 0x7C`；ASCII/图标点阵自己设计，中文字库遵循用户的统一来源与 import_font 工作流。
2. **所有自定义的东西必须有文档**：内存布局、磁盘格式、调色板、字体编码、ABI，全部落在 `docs/`，代码与文档同步修改。
3. **全中文注释**：解释"为什么"而不只是"是什么"，让每一步都能看懂原理。
4. **可自举方向**：ABI 与格式稳定后，目标是让 SandCore 能在自己的系统上编译自己的程序（详见 ROADMAP）。

## 修订记录

| 日期 | 变更 |
|---|---|
| 2026-10-02 | M6a 自动化实测、验收包与状态表同步；M6 汇编器/应用继续推进，用户界面验收尚未确认 |
| 2026-10-02 | M6 系统内生成/执行 SCX 与应用矩阵实测，双盘快速开始/文档索引/目录结构/里程碑同步；新增代码详细中文注释，M7 留作下一阶段 |
