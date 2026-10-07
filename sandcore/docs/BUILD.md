# SandCore 构建系统文档 v0.7

> **2026-10-07最新：** 默认公开键/已签WALL100的整包构建formal-build-01退出0，独立输出build/m10a1-formal-01；MAIN与冻结10全字节相同，正文SHA 3e57a363f5164cc69485a67dfab9f08c76bd1af5426375cc2c2100d5a23f3170。两原M9来源新迁移formal-01/02均256MiB、目录容量2048、1489/1451项，正常扩展只有WALL100，原盘/原M10开发盘不覆盖。M9的225份与M10的167份固定源码/声明归档审计通过。Shell管道修正定向构建输出shell-fix-01，再迁移独立shell-inputs-01双盘；网络/会话与全部其它合同继续，普通入口整体验收未完成。下方早期“默认接线尚未重新构建”仅属沿革。

> **2026-10-06 M10a1当前入口：** 实现/资源/构建规则/规范已按完整合同核对收齐，进入统一构建与实际验收，依据AGENTS第十一节已有授权，无新阶段审批。根build.bat转发sandcore/build.bat，默认WSL make m10a1，独立输出build/m10a1-work；build-04退出0，磁盘MAIN/loader/18网络SCX及256MiB盘实际生成，Windows QEMU验收开始。见M10A1-READINESS.md与M10A1-VERIFICATION.md。下文早期禁构建/M9“已构建”是历史记录，不覆盖当前M10状态。

M10用ELF32 core.ld生成磁盘MAIN、loader.ld生成只读启动器、mkimg --sectors 128生成软盘；MAIN必须进入SYS/CORE/CORE.SKM。m10.mk复用M9成熟独立CLI/固定基线资源接线，不重启M8冻结资源生成。源盘默认已有build/m9-work/sanddata.img，否则只读固定M9精简发布包并核摘要提取到build/baselines；可用M10_SOURCE_IMAGE指定另一只读来源，迁移器拒绝输入输出同一路径。生成盘默认256MiB，已有输出不覆盖，显式M10_REPLACE=1才保留备份后替换。

M10_PUBLIC_KEY只接受32B用户公钥；收到用户公开结果后，默认读取assets/trust/M10-OWNER-ED25519.PUB，M10_SIGNED_DIR默认modules/signed，仅含用户签署的WALL100.SKM。公钥与完整扩展的SHA256分别为fc0f26d1b2f3cfea86ca19f3d5d37acd2fea932f683307887ace3bda7200e4f7和4ba998a48e72b623e078ba07de6d699081fc8462dc456bd0a6fe53091ddd7ea4；明确设置M10_PUBLIC_KEY为空则默认不安装扩展、生成无信任键的拒绝配置。可显式指定其它公开键/已签目录；只安装通过宿主验签的SKM2，不安装10/20/30验收夹具或坏格式样本。WALL待签消息仍可生成，正式签署始终由用户独立完成，见CORE.md与assets/trust/README.md。默认公开结果接线已写，普通整包入口的重新构建/发布验证仍待执行；不以验收盘通过代替它。Monocypher及其它选用源码/声明通过固定清单审计并完整复制SYS/LICENSE。当前MinGW备用不能构建新MAIN格式，M10明确要求WSL ELF。独立历史重建入口保持。

修订：2026-10-06，新增M10独立磁盘核心/最小loader/只读基线迁移与公开签名接线，全部规则未执行。
修订：2026-10-06，统一build-04通过；补Monocypher头搜索与完整上游依赖头，独立编辑器EXE构建退出0，原M9 EXE摘要保持。
修订：2026-10-07，归档用户公开键与已签壁纸，默认入口接公开结果并保留显式无键回退；代理不接触私钥，默认入口新整包验收待执行。

> **2026-10-05仓库复现入口**：根目录`build.bat`默认M9；
> `build-version.bat M6a|M6|M7|M8a`重建对应历史快照到独立目录。
> 五版均已从干净克隆构建通过。M9读取仓库内精简M7/M8a基线，不再依赖
> 本机原大ZIP；完整依赖、实际证据和宿主/原生发布区别见[REBUILD-VERIFICATION.md](REBUILD-VERIFICATION.md)。
> 下文旧命令、固定原ZIP路径及阶段状态为历史记录，当前复现以此入口为准。

> 当前验证范围已更新，见末尾“2026-10-05 M9最终验证记录”及[M9验收报告](M9-ACCEPTANCE.md)。早期未验证/待验标记保留阶段背景。

> M9当前进展（2026-10-05）：独立build/m9-work整包WSL构建已通过；
> 18双盘集成224项、回退01八组68项、19CLI原生自举双盘18项通过。
> 本文后半早期“尚未执行”保留阶段沿革，当前范围以M9-VERIFICATION.md为准。

> M8当前运行器更新（2026-10-04）：run.bat/Makefile优先Windows
> x86_64 QEMU的WHPX硬件加速，保留TCG及if=floppy+IDE双盘。
> 历史M6/M7章节中的i386记录保留，当前参数/证据见QEMU.md。

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
       build/kernel.bin (补 0 对齐到 512 倍数, 当前源码必须 ≤384 扇区)
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

已验收M7包的默认数据盘内bin/s3c.scx为原生G2，Files/IDE/Debug/Lumen/Race/World/Probe
为该 G2 生成的机器码；m7-native-runtime.json 记录源码与最终双盘摘要。
原启动盘保存为 M7-bootstrap-sanddata.img。若需完整重做首次启动链，在备份
个人数据后用 build.bat -B 强制构建，再依次运行 verify_s3c.py、verify_graphics.py、
publish_native.py 和回归/审计；普通 build.bat 没有变化时不会覆盖已发布原生产物。
package_m7.py 只收录成功证据、源码、双盘及诊断产物，逐文件重读 ZIP 验证。

2026-10-02：正式运行盘安装原生 G2/七个应用/八份符号图；发布工具、全零新盘与快照、重建顺序、M7 打包合同同步。

2026-10-02：M8源码/API已变化并重建为宿主启动版本，不能沿用M7原生发布声明。
build.bat保持纯ASCII，构建失败显式exit /b 1，成功0，防止echo覆盖make失败码。
truecolor验收脚本用独立双盘副本与Windows QEMU，输出build/m8-truecolor，完整M8发布待完成。

2026-10-02：新增theme.o、主题预设/探针；NUI ARGB后台从高端用户堆申请。
verify_theme.py以已验收M7包G2实际编译本轮SCAPI.H/NUI/Settings，输出新目录m8-theme，
不覆盖M7历史来源声明；真彩色底座的独立静态审计为audit_truecolor.py。

2026-10-02：新增image.o/SCB2资源编译；图片资源需要Pillow，WSL缺Pillow时ARTPY使用已安装Windows python.exe，仅资源转换切解释器，GCC/LD仍WSL ELF。preserve_legacy.py核对原M7 ZIP摘要后保存旧SCX/源码。数据盘默认64MB且内核读取真实设备容量，旧盘仍兼容。验证新增verify_images.py；仍需完整原生发布/三代收敛和M8包。

2026-10-03：用户空间/CPU/存储独立API、19个独立CLI和新512目录
同步构建；SETTINGS.inc为三环私有片段并上SYS/SRC，SCAPI.H仍唯一
公共头。verify_settings.py真实G2编译和运行当前六页设置，宿主
产物不能称为原生发布。verify_cpu_storage/verify_userspace的精确
已验证二进制/源文件快照另存m8-system/verified-*，继续开发不混用。

2026-10-03：六页设置精确源码/双盘/G2产物/测试冷启动盘与
校验清单冻结M8-settings-evidence.zip，脚本package_m8_settings_stage.py
重读CRC及逐项SHA检查。开IRQ整帧复制另用verify_frame_irq.py：
旧核参考已保留，同一真实G2程序的新核输入/完整画布/活动关闭与
复制内PIT通过。verify_frame_regressions.py串行四套独立解释器/
双盘/子目录，防止导入OUT混用及继续构建覆盖输入。

2026-10-03：开IRQ及四套回归冻结包M8-frame-irq-evidence.zip已重读
CRC/325项SHA，整包25ad98e580302a1c6527b4aff5669e7b76bdff133e6220a72af7610cfebd3235。
新增verify_idle_yield.py参考阶段真实G2编译一次，优化阶段运行同一
SCX，计数实际整数工作/旧YIELD/双忙任务/真实输入及全页回收；
新核120980B/237扇区，静态审计与API核对另存optimized，不混旧核。

2026-10-03：空量子四套回归和大帧输入/关闭、1024/1080延迟均
通过，新增package_m8_idle_yield_stage.py冻结精确源码/默认盘与
各套原生测试盘。写完逐项重读CRC/SHA；默认盘HOST、G2测试产物
和旧参考核仍分别标识，不提前称完整M8原生发布。

2026-10-03：243扇区局部合成四套回归PASS；新增
verify_damage_matrix.py以同一G2产物逐模式比较局部/完整非任务栏
PCI，按住拖动时对照；package_m8_damage_stage.py先核对18组合、
大帧/四套/延迟全部输入再封存，此时仍待测试完成，尚未执行打包。
两个damage脚本复测需新--tag，成功报告不覆盖。

2026-10-03：18组合矩阵及大帧/四套回归/1024和1080延迟已经全部PASS。矩阵验证器补真实Shell提示符就绪基线，保留原验证器快照及失败历史；冻结工具同时保存旧快照来源，不把旧核或失败图混入成功证据。

2026-10-03：局部合成阶段包M8-damage-evidence.zip已实际封存并重读CRC/560项SHA，整包15d34f56a440bef7905eeaa673d20b96a5c9048f4fb6556d1a91d70e4a9ff390。包含243扇区精确核/源码/默认HOST双盘、真实G2原生产物、18组合和四套/大帧/延迟成功证据及独立测试盘；旧验证器快照保存，失败历史不计入成功。完整M8与下一批Canvas/NUI适配继续。


## M8 第一阶段整批构建接线（尚未执行）

2026-10-03用户要求第一阶段全部实现后停止，批准第二阶段才构建。
当前源码额度384扇区/192KiB；boot.asm、mkimg.py、linker.ld与BOOT.md
同步，BSS上限仍2MiB。旧243扇区产物不更新为新源的构建证明。

Makefile加入boot_scene.o/image_service.o，所有user顶层.c/.inc/.H
复制到SYS/SRC，所有大写.H到SYS/INC；SCAPI.H仍是内核ABI，SCENE/
IMAGECLIENT是源级库。普通应用/编译器的依赖包含全部私有片段，
修改共用库需要重新产生有关SCX并重跑验证，不能借旧二进制套新源。

SYS/CORE/IMAGE.SCX与IMAGE.LIC列入sanddata.img依赖。服务采用固定
stb/libwebp、freestanding适配与按节裁剪，源在third_party/user/codec；
不进入内核OBJS。当前来源HOST_BOOTSTRAP，普通M8程序/库仍要求真实
系统内SCCC生成。codec子目录的.d文件也参与依赖，SCAPI变化会重编。
本轮没有构建，尺寸/链接/禁浮点/无库审计统一留给第二阶段。

批准后仍使用cd sandcore && build.bat（WSL ELF主力）。先备份正在
使用的数据盘、旧镜像和验收包，不运行make clean删除历史资料；
mkfs继续合并用户HOME/DESK/自改配置。Windows QEMU只在独立盘副本
运行，必须if=floppy引导盘+IDE数据盘。失败停止后续，修复重建/
重新客体编译对应产物，不覆盖已有成功与失败历史。

publish_m8_native.py、package_m8.py已写入来源/完整证据/独立发布
合同，参数与清单见PACKAGE.md；第一阶段未执行，尚无新原生发布盘。

2026-10-03：同步整批源码/codec许可/大写库头/384容量和发布工具接线；
记录HOST_BOOTSTRAP边界与两阶段门槛，未新增构建/运行结论。

2026-10-04：新增WHPX/TCG选择及宿主文件/加速身份记录；两份启动器
六模式已实际无头双盘验证。SCWIDE沿大写头/源码安装规则接入，
数学两代原生通过；SCENE精确批次及正式新应用另记录。

## M9独立链

2026-10-05最新实际状态：全部源码/资源接线收齐后已执行WSL `make m9`，
build/m9-work/build-05.log退出0。新947项v5数据盘、播放器/CLI/音效、
夹具及221份第三方源码/声明归档已生成；后文“尚未执行”保留为实现阶段记录。
内核无未定义符号，text211591/data64/BSS827796B，BSS末端0x1CA194。
为避免GCC的大聚合初始化隐式引入libc，M9链采用-minline-all-stringops，
由编译器发整数REP等指令；未提供memset/memcpy标准库。cron只复制有效项。
Windows QEMU双盘调试开始，当前尚无运行验收成功证据。

M9先收齐全部实现，再统一构建/运行。届时从WSL执行`make m9`，递归明确
`BUILD=build/m9-work M9_BUILD=1`，不会借发布重启M8游戏/影片。新内核容量
640扇区/320KiB与boot/linker/mkimg/BOOT合同同步；旧产物不是新源构建证明。

`m9-artifacts`链接独立CLI、SCCC/SandAsm与音频播放器，生成自写音效，安装
新增API/源码/桌面入口、完整第三方源码及声明。`m9-publish`只读源M8a数据盘，
用mkfs_m9迁移/合并出v5新盘；已有M9输出须显式M9_REPLACE=1并先备份。
禁止make clean删除历史证据；Windows QEMU只用独立测试副本。

DEFLATE仅四个独立压缩命令链接私有miniz核心；MP3链仅三环解码采用x87，
内核编译禁浮点/SIMD约束不解除。统一来源及SYS/LICENSE完整归档见
THIRD-PARTY.md。当前没有调用上述规则，也没有产生M9发布盘。

M9的bin/s3c与bin/sccc均为CLI编译入口；nano/scdbg及新增工具由user/m9/*.c
逐个独立链接。RTC内核对象与只读CLOCK2、完整CPUINFO2接线已加，旧ABI保持。
宿主串口QEMU明确-rtc base=utc，双UART/私有pipe与无网络启动合同不变。
这些是构建规则/源码，尚未实际调用构建。全部剩余代码写齐后自动统一构建、
系统内开发闭环及双盘Windows QEMU，不再另请用户确认。

修订：2026-10-05，同步M9独立构建/迁移链、640扇区及全部第三方源码/声明
归档；静态接线不代表构建、原生编译或运行通过。

M9新增pack私有链只用于五个bzip2/LZMA命令，BZ_NO_STDIO禁用上游stdio，
整数核心不链接音频x87或libc。BZIP2/LZMA选用原源码完整安装SYS/LICENSE，
私有适配源码安装SYS/SRC，SCPACK/cron/日志等项目头走既有源码/头安装规则。
SYS/MAN安装实用工具文本页；缺页由man明确失败，不能拿通用帮助充覆盖。

SYS/TEST安装原创M9CHECK/IODENY/IDENTITY/DEBUG/ONCE源码，统一阶段由客体
s3c/sccc实际编译；SIMDSTATE/AUDIOCHECK按复杂组件授权用宿主固定链生成
客体执行夹具。m9fixtures产生原创极小谱MP3及Python标准工具独立编码的
bzip2/LZMA/DEFLATE测试数据；这是开发工具调用，不移植Python实现到系统。

tools/verify_m9.py已写双盘Windows入口，传--boot新引导盘、--symbols同批
kernel.sym、两个不同--data和全新--out。它只在全部实现收齐后执行；源码
包含串口二进制/坏帧、CLI字节行为、native编译、nano、scdbg、UID/UART、
SYSTEM窗口拒绝、SKM/BSS、COM1真实断点、HMP输入/截屏及客体截图取回。
新符号摘要和镜像副本记录在证据中，脚本未执行，完整性能/坏盘/历史软件
及音频格式兼容仍有明确剩余矩阵；集成脚本成功不输出M9_COMPLETE。

修订：2026-10-05，同步压缩私有链、手册/客体夹具及双盘验收脚本源码；
尚未生成资源、构建、编译或启动QEMU。


修订：2026-10-05，本轮播放器07双盘88项、整数无FPU20项、身份/旧程序54项、默认音效/关机和宿主进度17项通过，范围与全部失败见M9-VERIFICATION.md；独立FLAC试玩包保留旧基线/会话，完整M9仍未验收。

## 2026-10-05 M9最终验证记录

build31退出0，内核仍为30原字节，164产品ELF无未解析符号/动态TLS、内核213240B/BSS末端0x1ca194，225份第三方归档及两盘SCX布局通过。m9-artifacts包含动态尺寸CORE.SKM。原版M7/M8a固定ZIP复用要求与摘要见audit_m9_compat.py，源包/旧盘不覆盖。

修订：2026-10-05，记录实际范围与证据，待用户验收；前述早期未验证叙述保留为历史。

## M10a1源码接线（未执行）

m10.mk在M10_BUILD=1追加自写e1000/network/net_socket、私有port/timeouts/loopback与固定lwIP公共/IPv4/Ethernet原翻译单元。原timeouts单元由预算适配包含、不重复链接；头文件/依赖跟踪覆盖私有配置与上游头，不编译IPv6/PPP/TLS/上游应用。仍无内核libc/浮点。全部实际原文件/声明归SYS/LICENSE/LWIP，私有源归SYS/NETSRC。

18个user/net/*.c独立编译链接打SCX，M10_NET_NAMES显式列名，复用user-start/linker/mkscx，不用多调用分派。独立源码/NETCLI.inc归SYS/SRC/net，SCNET.H归原SRC/INC规则；NETWORK.md/CLI-M10.md归SYS/MAN。nc -e新增0x263，旧SPAWN2不改。M10独立build/m10a1-work及256MiB迁移规则不覆盖M9/原盘；所有规则仍未调用。

修订：2026-10-06，补网络原核心/私有适配/18独立工具及完整来源/开发手册构建接线；全部实现收齐前不构建，不以本记录宣布收齐全轮或PASS。

tests/m10/M10NET.C以源码归SYS/TEST，统一阶段由客体s3c编译；只用公开网络/流/身份接口，不向内核加入测试入口。verify_m10_network.py、独立Ethernet对端/HTTP/TFTP/echo服务已写，均未执行。根目录run-m10a1.bat调用run_m10a1.py，创建新会话副本并复用私有QMP/UART与IPv4 e1000；不使用旧启动器的TCP监控口。来源镜像仍为独立build/m10a1-work两盘，目前尚不存在本轮新构建产物。

修订：2026-10-06，补M10独立启动器/私有控制通道、客体网络探针与统一验收脚本源码；编辑器目录OOM提示/迁移索引自归档接线，未构建未运行。

M10测试源码通配规则同样安装新增M10LIFE.C。统一阶段由客体s3c
编译至/TMP/M10LIFE.SCX，verify_m10_lifecycle.py在两个不同来源
新盘副本上执行并发/回收/复用/公平/真实耗尽，默认无NIC；详细
边界见M10-LIFECYCLE.md。当前只写接线，脚本与探针均未执行。

同规则安装M10RES.C，统一脚本先客体编译并执行18原生窗口、
超过旧8份/64MiB截图和12并发事务用例；M10LIFE.MD手册同批
归SYS/MAN。两份探针不会覆盖M9旧配额/回收历史源码。

修订：2026-10-06，补动态任务公开ABI生命周期探针与双盘执行源码，
仍未构建/运行，不能把验证代码已经写好当作验证通过。
