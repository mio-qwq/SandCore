# SandCore 构建系统文档 v0.1

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
| 抽二进制 | `objcopy -O binary` | `objcopy -j .text -j .rdata -j .data -O binary` |
| 镜像打包 | `python3 tools/mkimg.py` | `python tools/mkimg.py` |

Makefile 里用 `uname -s` 自动探测环境（`MINGW` 前缀 → 路线②，否则路线①）。

### C 编译旗标逐条解释（两条路线相同）

```
-m32                            强制 32 位代码
-ffreestanding                  自由站立: 没有操作系统/ libc 伺候
-nostdlib -fno-builtin          不链任何库; 禁止把 memcpy 暗中替换成库调用
-fno-stack-protector            栈保护需要库函数 __stack_chk_fail, 砍掉
-fno-pie                        不生成位置无关代码 (内核按固定地址布局)
-fno-asynchronous-unwind-tables 不要 .eh_frame 异常展开表
-fno-exceptions                 禁用异常 (纯 C 双保险)
-O2 -Wall -Wextra               优化 + 全量告警
```

## 3. 路线②（PE）踩坑实录 —— 为什么参数长这样

Windows 的 `ld` 只有 `i386pe` 一种模拟（没有 ELF），而 PE 格式给内核
开发者埋了三个坑，对应三个参数：

| 坑 | 现象 | 解法参数 |
|---|---|---|
| C 符号带下划线 | `undefined reference to kmain`（实际是 `_kmain`） | entry.asm 里用 NASM 的 `%ifidn __OUTPUT_FORMAT__` 条件区分，Windows 分支手写 `_kmain` |
| 段摆得稀疏 | .text 在 0、.rdata 被甩到 0x106c0，objcopy 出的镜像全是 0 洞（一度 72KB） | `--section-alignment=16 --file-alignment=16` 把段间距压到最小 |
| 夹带 .reloc 段 | PE 重定位表混进镜像 | `objcopy -j .text -j .rdata -j .data` 白名单抽取 |
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
       build/kernel.bin (补 0 对齐到 512 倍数, 必须 ≤64 扇区)
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

注意：`--quit` 通过重连监视器发送 `quit` 命令关机，偶尔不生效，
可以用 `taskkill //F //IM qemu-system-i386.exe` 兜底。

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
  （Makefile 的 run/run-headless 已带；**run.bat / run.sh 尚未加数据盘** —— 交接待办，见 HANDOFF.md §3.4）；
- Windows 一键构建：`build.bat`（内部就是 mingw32-make，环境判断用 OS 变量，不再用 uname）。

## 8. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-10-01 | 初版：双工具链、PE 踩坑四连、指纹机制、截屏链路 |
| v0.2 | 2026-10-01 | M2：所有 QEMU 启动命令加 `if=floppy`（缺省挂成 IDE 硬盘导致 CHS 寻址读错磁头，见 docs/BOOT.md §0）；新增 keys.py 按键注入工具 |
| v0.3 | 2026-10-02 | M5/M6a：双盘时代（§7.5）、build.bat 一键构建、字形表编译进构建链（mkfont）、OBJS 增 paging.o |
