# SandCore 构建系统文档 v0.7

> 本文解释 `Makefile` 的全部设计决策，特别是"为什么在 Windows 上编内核
> 这么别扭"以及两条工具链路线各自的坑与解法。

## 1. 最终产物是什么

**纯二进制镜像（flat binary）**：没有任何文件头，第一个字节就是可执行代码，
引导器用 BIOS 把它原样搬到物理地址 0x10000 后直接跳过去执行。

编译器/链接器的原生输出（ELF 或 PE）都只是**中间集装箱**，
最后一步 `objcopy -O binary` 沿着链接脚本定义的地址线把内容"抻直"导出，
文件头、符号表、重定位表全部不进入最终镜像。
**两条工具链路线产物格式完全一致、可互换。**

## 2. 双工具链路线

| | 路线① WSL/Linux（主力） | 路线② Windows MinGW（备用） |
|---|---|---|
| 触发方式 | `wsl -e bash -c "cd <项目> && make"` | `mingw32-make` |
| 汇编 entry.asm | `nasm -f elf32` | `nasm -f win32` (COFF) |
| C 编译 | `gcc -m32 -ffreestanding` | 同左（MinGW i686 版天生 32 位） |
| 链接 | `ld -m elf_i386` → kernel.elf | `ld -m i386pe ...` → kernel.pe |
| 抽二进制 | `objcopy -O binary` | `objcopy -j .text -j .rdata -j .rodata -j .data -O binary` |
| 镜像打包 | `python3 tools/mkimg.py` | `python tools/mkimg.py` |

Makefile 用环境变量 `OS=Windows_NT` 选择 MinGW，否则走 WSL/Linux ELF；不依赖 Windows 缺少的 uname。

### C 编译旗标逐条解释（两条路线相同）

```
-m32                            强制 32 位代码
-ffreestanding                  自由站立: 没有操作系统/ libc 伺候
-nostdlib -fno-builtin          不链任何库; 禁止把 memcpy 暗中替换成库调用
-fno-stack-protector            栈保护需要库函数 __stack_chk_fail, 砍掉
-fno-pie                        不生成位置无关代码 (内核按固定地址布局)
-fno-asynchronous-unwind-tables 不要 .eh_frame 异常展开表
-fno-exceptions                 禁用异常 (纯 C 双保险)
-fno-leading-underscore         PE 与 ELF 的 C/汇编符号名统一
-msoft-float -mno-sse -mno-mmx   不生成需要浮点/向量状态保存的指令
-MMD -MP                       自动生成头文件依赖与占位规则
-O2 -Wall -Wextra               优化 + 全量告警
```

## 3. 路线②（PE）踩坑实录 —— 为什么参数长这样

Windows 的 `ld` 只有 `i386pe` 一种模拟（没有 ELF），而 PE 格式给内核
开发者埋了三个坑，对应三个参数：

| 坑 | 现象 | 解法参数 |
|---|---|---|
| C 符号带下划线 | `undefined reference to kmain`（实际是 `_kmain`） | C 统一 `-fno-leading-underscore`，两平台汇编直接引用 kmain |
| 段摆得稀疏 | .text 在 0、.rdata 被甩到 0x106c0，objcopy 出的镜像全是 0 洞（一度 72KB） | `--section-alignment=16 --file-alignment=16` 把段间距压到最小 |
| 夹带 .reloc 段 | PE 重定位表混进镜像 | `objcopy -j .text -j .rdata -j .rodata -j .data` 白名单抽取；.rodata 是用户链接脚本合并后的段 |
| 莫名警告 | `section below image base` | `--image-base=0` 让脚本地址原样生效，警告消失 |

> 结论记录在案：镜像里**不会**混入 PE 头/重定位表等任何"别的"东西，
> `-j` 白名单 + 链接脚本 `/DISCARD/`（丢弃 .note/.comment/.eh_frame）双保险。

## 4. 工具链指纹（防脏产物）

**症状**：从 Windows 路线切到 WSL 路线后链接报 `undefined reference to kmain`
——build/ 里躺着上一个工具链编的 COFF 格式 main.o（符号 `_kmain`），
和 ELF 的 entry.o 混链。

**机制**：Makefile 为每个工具链维护一个指纹文件 `build/.toolchain-<KOUT>`，
所有 .o 都依赖它。切换工具链 → 指纹文件名变化 → 旧指纹不存在 → 自动重建，
旧指纹被清理。**切工具链不再需要手工 `make clean`**。

## 5. 镜像打包格式（tools/mkimg.py）

```
输入:  build/boot.bin (必须 ≤512B)
       build/kernel.bin (补 0 对齐到 512 倍数, 必须 ≤256 扇区)
输出:  build/sandcore.img (1474560 B = 1.44MB 软盘)

布局:  [0..511]           boot.bin
       [512..512+N]       kernel.bin
       其余补 0
```

大小上限与 `boot/boot.asm` 的 `KERNEL_SECTS` 是同一份合同，
详见 docs/BOOT.md 第 2 节。

## 6. 无头验证与截屏（tools/snap.py）

自动化验证链路（不需要人眼看 QEMU 窗口）：

```
QEMU (-display none -monitor tcp:127.0.0.1:4444)
        │  HMP 监视器协议 (人读文本协议, 非 JSON 的 QMP)
        ▼
snap.py 发送 "screendump build/shot.ppm"
        │  QEMU 把当前帧存成 PPM (P6 二进制)
        ▼
snap.py 内置 PPM→PNG 转换 (zlib + CRC32 手拼 PNG 块, 零依赖)
        ▼
build/shot.png  → 人工目检或进 README
```

注意：`--quit` 通过重连监视器发送 `quit` 命令关机。需要兜底时只终止本次启动的 PID，
避免关闭其他 QEMU 会话；自动验收脚本保存自己的进程对象并在 finally 中退出。

## 7. 常用命令速查

```sh
make clean            # 清空 build/
make                  # 构建 (WSL 内) / mingw32-make (Windows)
make run              # 弹 QEMU 窗口 (Windows 侧用 run.bat 更方便)
make run-headless     # 无头启动 (配合 snap.py)
```

## 7.5 双盘时代（M5 起）

- 产物是**两块盘**：`sandcore.img`（引导+内核，软盘）+ `sanddata.img`（SandFS 数据，8MB，IDE）；
- QEMU 必须同时挂：`-drive format=raw,if=floppy,file=build/sandcore.img -drive format=raw,if=ide,file=build/sanddata.img`
  （Makefile 的 run/run-headless、run.bat、run.sh 都已挂双盘）；
- Windows 一键构建：`build.bat` 优先 `wsl.exe --cd 当前目录 -e make`（ELF 主力），未安装 WSL 时才调用 mingw32-make；不安装任何包。Makefile 用 OS 环境变量区分工具链。

### M6a 资源构建链

`user/*.asm → nasm -f bin → build/*.bin → mkscx.py → build/fs/{bin,apps}/*.scx`。
字体和图标经 `mkscf.py` 生成 `build/fs/sys/*.scf`；快捷方式复制到 `build/fs/desk/`；mkfs 只打包目录树。
头文件依赖由 GCC `-MMD -MP` 自动追踪。内核禁止浮点/SSE/MMX/libc；链接脚本检查 BSS 不侵占引导栈预留区。
Windows QEMU 绝对路径为 `C:\Program Files\qemu\qemu-system-i386.exe`；WSL 仅承担构建。
Makefile 的 WSL run/run-headless 也通过 Windows 互操作调用 `/mnt/c/Program Files/qemu/qemu-system-i386.exe`；不启动 Linux 版 QEMU。
验收工具 `tools/verify_m6.py` 经 HMP sendkey/screendump/pmemsave、QMP 分别按下/松开验证行为。

### M6 用户态 C 构建链

`user/*.c + api.h → freestanding GCC → user-*.o`，再与 `user/start.asm` 的入口/cdecl 桥一起链接到 0x400000。
M6 历史版 `user/linker.ld` 把 BSS 物化为零数据；M7 改为 NOLOAD，objcopy 导出私有平映像，mkscx 加 36B SCX1MIO 头。
保留 `user-*.sym` 与 kernel.sym 供验收工具定位变量；.SECONDARY 保留中间产物，方便对照，不进入系统盘。
链接器提示 flat image 的 LOAD 段 RWX 是当前合段产物的属性，未使用 ELF 在系统内运行；没有 libc 或未解析外部符号。
SandAsm 本身由这条链构建，但运行后汇编用户源文件时不调用宿主工具链；原生汇编职责见 ASM.md。
`tools/verify_apps.py` 使用测试盘副本完成 24 张应用证据，具体步骤见 TESTING.md/M6.md。
`audit_m6.py` 核对发布盘与资源树、SCX/SCF/字体摘要、ELF 库符号/浮点向量指令；`package_m6.py` 收录完整源码与必要产物，ZIP 逐文件重读 SHA256，避免夹带暂存内存或早期超大日志。

## 8. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-10-01 | 初版：双工具链、PE 踩坑四连、指纹机制、截屏链路 |
| v0.2 | 2026-10-01 | M2：所有 QEMU 启动命令加 `if=floppy`（缺省挂成 IDE 硬盘导致 CHS 寻址读错磁头，见 docs/BOOT.md §0）；新增 keys.py 按键注入工具 |
| v0.3 | 2026-10-02 | M5/M6a：双盘时代（§7.5）、build.bat 一键构建、字形表编译进构建链（mkfont）、OBJS 增 paging.o |
| v0.4 | 2026-10-02 | M6a 修复旧依赖与资源链、全部启动器双盘、自动头依赖、禁浮点参数、BSS 栈边界与 Windows QEMU 验证工具 |
| v0.5 | 2026-10-02 | M6：用户态 C 的 start.asm/cdecl 调用桥/平映像链接，SCX BSS 物化；build.bat 实测走 WSL，QEMU 统一 Windows，PE 白名单补 .rodata；应用验收用盘副本 |
| v0.6 | 2026-10-02 | 最终 24 张应用证据，发布盘/ELF/字体/批处理静态核对与可复验 ZIP；补充构建旗标、平映像 RWX 属性和进程清理范围 |

### M7 当前构建与原生产物

build.bat 仍用 WSL ELF 工具链构建内核/首次启动组件；内核额度 256
扇区，BSS 在 1MB..2MB。用户 BSS 为 NOLOAD，用 SCX 的 bss_size
清零，不再把数 MB 的编译器工作区物化到磁盘载荷。
SCAPI.H 为唯一公共 C 头（全部大写），api.h 仅为旧源兼容转接。
SCCC 源与私有片段/UI/应用源预装 SYS/SRC，SCAPI.H 位于 SYS/INC。

字体构建：汉字只从 font16.txt，ASCII 提取缓存同源凤凰 16px；普通
make 的 mkfont8.py 只用标准库，补字/重建缓存的 import_font.py 才
需要 Pillow。详见 FONT.md。内核无 libc、x87、MMX、SSE 依赖。

verify_s3c.py 实际产生 G1/G2/G3，verify_graphics.py 用 G2 产生全部
M7 图形应用与探针。源码/API/UI/G2 完全相同时复用已验证原生文件，
否则重新在系统内编译；native-provenance.json 保存来源摘要。
运行 tools/publish_native.py：核对测试结果、三代收敛和全部当前源文件后，
把真实生成的 SCX 与 .map 装入 build/fs，重建无测试杂项的正式盘，
让 run.bat 直接运行内部编译产物。以后源码变化并 build.bat 后，
宿主版本会成为新的启动版，需重做内部编译链才能恢复原生产物声明。

新版验证使用 audit_m7.py；audit_m6.py/package_m6.py 保留历史包合同，
不要用新字体/新盘格式去覆盖已交付的 M6 历史包。

2026-10-02：M7 NOLOAD BSS/容量、同源字体缓存、原生编译来源/发布及新版审计合同同步。

当前交付默认数据盘内的 bin/s3c.scx 为原生 G2，Files/IDE/Debug/Lumen/Race/World/Probe
为该 G2 生成的机器码；m7-native-runtime.json 记录源码与最终双盘摘要。
原启动盘保存为 M7-bootstrap-sanddata.img。若需完整重做首次启动链，在备份
个人数据后用 build.bat -B 强制构建，再依次运行 verify_s3c.py、verify_graphics.py、
publish_native.py 和回归/审计；普通 build.bat 没有变化时不会覆盖已发布原生产物。
package_m7.py 只收录成功证据、源码、双盘及诊断产物，逐文件重读 ZIP 验证。

2026-10-02：正式运行盘安装原生 G2/七个应用/八份符号图；发布工具、全零新盘与快照、重建顺序、M7 打包合同同步。
