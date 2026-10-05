# SandCore 自测指南（TESTING GUIDE）

> 每完成一个里程碑，这里会追加该里程碑的**手工自测指南**。
> 2026-10-02：M6a + M6 自动化通过，用户界面验收尚未确认。当前操作见下面 M6a/M6，M0-M5 操作保留存档。
> 所有命令都在 `sandcore/` 目录下执行；Windows QEMU **必须双盘（引导 if=floppy + 数据 if=ide）**。

## 通用操作

```sh
# 构建 (Windows 一条命令, 自动编译字形表)
build.bat

# 玩
run.bat

# 无头自动化验证 (可选)
wsl -e bash -c "cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore && make run-headless"
python tools\snap.py build\shot.png --quit
```

进系统后：开机画面按**任意键** → 桌面与文件图标出现，**不自动开 Shell**。

## M6a 自测指南

先 `build.bat` 或 WSL make，再 `run.bat`（Windows QEMU，软盘+IDE 双盘）。单击 Shell 图标，输入 help/echo/ls/clear。
`run apps/hello.scx` 出现第二个窗口；按 Enter 关闭 hello 后继续在 Shell 输入，提示符应完整。
把 hello 标题栏拖动，点 Shell 露出的部分置顶；Shell 接收自己的按键，hello 内容不变。
反复启动/退出 hello 超过 8 次应仍可启动；Shell 红叉或 exit 应回收窗口与任务。
`run apps/panic.scx` 为验收用除零程序，会得到 `VEC 0x00` 红屏，数字不重叠；重启 QEMU 即可。
自动化：`python tools/verify_m6.py all`（Windows 运行，需先 WSL make 生成 kernel.sym）；工具自动挂双盘、使用测试数据盘副本、输入、截图、检查画布/页表/owner 与 10 轮回收，并退出自己的 QEMU。
证据：`build/m6a/01-boot.png` 至 `11-panic.png`，断言结果 `results.json`；完整说明见 M6A.md。

## M6 自测指南：原生汇编器与用户态组件

先构建，再运行 `run.bat`；按任意键进入只有四个文件图标的桌面。应用均可按 Esc 或红叉关闭。
下面的 Shell 命令需先单击 Shell 图标；EXEC 异步启动，新窗口取得焦点，关闭后回到 Shell。

| # | 步骤 | 预期结果 |
|---|---|---|
| 1 | Shell 输入 `asm home/demo.asm apps/demo.scx` | SandAsm 结果页显示 ASSEMBLED / SCX1MIO、98 bytes；源文件到机器码的过程在系统内完成 |
| 2 | Esc 关结果页，Shell 输入 `run apps/demo.scx` | 新程序窗口出现 hello from SandAsm / SCX1MIO / mio；红叉可回收示例循环任务 |
| 3 | `run apps/note.scx home/probe.txt`，输入 `saved inside SandCore`，F2 保存，Esc 关闭，再执行相同命令 | 文本原样重读，保存正文不包含终止 NUL；F3 可丢弃修改并重载 |
| 4 | `run apps/note.scx home/bad.asm`，输入 `nonsense`，F2、Esc；再输入 `asm home/bad.asm apps/bad.scx` | 第 1 行 unsupported instruction；失败不产生 apps/bad.scx |
| 5 | 单击 Files，按 4，j/k 或上下选 home/probe.txt，Enter | 前缀目录过滤和选择正确，Notes 在独立窗口打开同一文件；F5 可刷新新增条目 |
| 6 | `run apps/calc.scx`，输入 `12*7`、Enter；C 清空，再输入 `7/0`、Enter | 首次为 84，第二次 division by zero，内核继续工作；该程序只支持两个整数与一个操作符 |
| 7 | `run apps/mines.scx`，空格揭开首格，方向键选择、F 标旗、鼠标揭开、R 重开 | 首格不炸，选择/旗标/重开有效；全揭 40 个安全格显示 CLEAR，踩雷显示 BOOM 与所有雷 |
| 8 | 单击 Palette，按 2，观察 saved；Esc 关闭并重启 QEMU | 夜色壁纸写入 sys/wall.cfg；下一次按键进桌面仍是夜色，无自动窗口；1/3 可切回沙丘/暖沙 |

### 自动复现与证据判读

```text
build.bat
python tools/verify_m6.py all
python tools/verify_apps.py
python tools/audit_m6.py
python tools/package_m6.py
```

分别顺序执行，不要与另一台使用 4444/4445 监视器端口的 QEMU 同时运行。
两个脚本均直接启动 Windows QEMU，复制 build/sanddata.img 为测试盘，不往实际工作数据盘注入测试文本。
HMP sendkey 驱动键盘、QMP 分沿注入鼠标、HMP screendump 保存证据；pmemsave 和磁盘读取只用于诊断断言。
Mines 胜负用读取雷区的测试诊断驱动完整揭格，不改变程序的布雷/揭开逻辑；不是声称人工盲玩获胜。
壁纸验证使用 HMP system_reset，再实际走开机按键和 wm_init 配置读取，排除“只修改内存”的假成功。

结果与截图：M6a 在 `build/m6a/`，M6 在 `build/m6/`；各有 results.json 与完整 qemu-command.json。
应用矩阵共 24 张截图，包括非法指令/超长符号/非法入口、保存重读/F3、文件打开、除零/溢出、扫雷旗标/胜负、重启壁纸。
截图说明与限制见 [M6.md](M6.md)、[ASM.md](ASM.md)、[APPS.md](APPS.md)。
用户运行时保存文件属于挂载数据盘；资源修改触发 mkfs 会重建目录，保留个人文件先备份整盘，详见 FS.md。

## M5 自测指南：文件系统 + 用户程序（v0.5 存档）

| # | 步骤 | 预期结果 |
|---|---|---|
| 1 | 进桌面后输入 `ls` 回车 | 列出 SandFS 上的文件: `font.scf 7463B` + `hello.scx 163B` |
| 2 | 输入 `run hello` 回车 | 显示 `[scx size=163]` + `task started`, 随后终端打出 `hello from ring3! (user program by SCX)` / `sandcore says hi to mio` / `bye.` |
| 3 | 输入 `ps` 回车 | `0 kernel(run)  run` + `1 hello.scx  zombie` —— 用户程序跑完退出的证明 |
| 4 | 开机瞬间观察桌面 | 中文界面字体来自 SandFS 上的 font.scf (渲染器零改动, 字体文件策略生效) |
| 5 | `run 不存在的名字` | 琥珀色 `run failed, code 1` (1=文件不存在 2=魔数错 3=尺寸非法) |

**M0-M4 回归速查**：桌面/拖拽/关闭正常 → `mem` 数字一致 → `dune` 来回切 → `about` 中文页 → panic 红屏 `VEC 0x**` 数字不再重叠。

---

## M4 自测指南：窗口系统 + 鼠标（存档）

| # | 步骤 | 预期结果 |
|---|---|---|
| 1 | 启动，按任意键进桌面 | 沙漠壁纸 + **终端窗口**（青色标题栏"终端"、红叉关闭钮、shell 横幅和提示符在窗口内）+ **关于窗口**（在终端后面）+ 底部任务栏（`沙核` + `UP 秒数 S`） |
| 2 | 动鼠标 | 白色箭头光标跟随移动（不撕裂） |
| 3 | 点击终端窗口任意处 | 终端置顶聚焦（标题栏变青色，光标在终端里闪烁） |
| 4 | 在终端里输入 `mem` 回车 | 输出显示在**终端窗口内部**，超 38 列自动折行，满了自动滚动 |
| 5 | 按住终端标题栏拖动 | 窗口跟随鼠标移动，底部不会压进任务栏 |
| 6 | 点击关于窗口露出的部分 | 关于窗口置顶（盖住终端） |
| 7 | 点关于窗口标题栏右侧红叉 | 关于窗口关闭，露出下面的终端 |
| 8 | 输入 `about` 回车 | 新开一个关于窗口（可开多个，逐个红叉关闭） |
| 9 | 观察任务栏时钟 | 每秒 +1（`UP 59 S` → `UP 60 S`），说明合成循环活着 |
| 10 | 输入 `panic` 回车 | 红屏 panic 页（重启即可） |

**M0-M3 回归速查**：开机画面「沙核」中文标题正常 → `dune` 全屏沙漠来回切 → `about` 双语页 → `mem` 三组数字 → 退格删不到提示符。

---

## M3 自测指南（存档）

开机画面「沙核」汉字 → `about` 全屏中文页 → 按键返回 → `dune` 来回切 → `mem`/`uptime`/`echo`/未知命令警告。

## M2 自测指南（存档）

`mem`（E820/分配器/堆三组数字）· `uptime`（秒与 ticks 互证）· `echo` 带空格 · 未知命令琥珀警告 · `dune`/`clear`/`panic`/`reboot`/`halt` 逐一执行。

## M1 自测指南（存档）

打字回显 → 退格（删不到提示符/横幅，无残迹）→ 长输入触发滚动（无碎片）→ uptime 连续增长（PIT 活着）。

## M0 自测指南（存档）

开机画面完整（渐变/沙丘/落日/标题/星），`PRESS ANY KEY` 后进入控制台。

## 修订记录

| 日期 | 变更 |
|---|---|
| 2026-10-02 | M6a 双盘桌面、Shell/并发/回收/panic 自测与自动化脚本，旧版本指南标为存档 |
| 2026-10-02 | M6 原生汇编/Notes/Files/Calculator/Mines/Palette 手工步骤、错误/胜负/重启自动复现、测试盘隔离与截图证据同步 |
| 2026-10-02 | 补充汇编器超长名字/非法入口、F3/旗标/溢出守卫，24 张应用截图；静态完整性与 ZIP 重读校验命令 |
