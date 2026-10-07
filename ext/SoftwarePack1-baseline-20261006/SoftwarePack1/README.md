# SandCore_ExtraSoftware_Pack_1 —— 扩展用户态软件包 1

> 版本 1.0 ｜ 2026-10-06 ｜ 交付物：**一个 SCX 安装器**。
> 状态声明（诚实边界）：全部源码完成、宿主工具链完整构建通过
> （WSL gcc 15.2 ELF + MinGW 交叉验证语法）；**未做客体内实机
> 运行验证**——按用户指示本轮不进行 QEMU/实机验证，"可运行"
> 的最终确认以上机为准。

## 1. 这是什么

五个功能完整的用户态程序，打包成一个自包含安装器
`dist/SandCore_ExtraSoftware_Pack_1.scx`（257,881B，内嵌全部应用
SCX 与 SCB2 图标，载荷 255,509B / BSS 112,603B，SCX1MIO 合同内）：

| 组件 | 类型 | 大小 | 功能 |
|---|---|---:|---|
| PCalc | 工具 | 26,433B | 表达式/程序员计算器（见 [pcalc/README.md](pcalc/README.md)） |
| SandSheet | 工具 | 46,333B | 公式表格 + CSV（见 [sheet/README.md](sheet/README.md)） |
| SandHex | 工具 | 46,957B | 十六进制编辑器（见 [hexed/README.md](hexed/README.md)） |
| SandBlocks | 游戏 | 32,581B | 方块游戏 + 高分榜（见 [blocks/README.md](blocks/README.md)） |
| SandRaider | 游戏 | 40,349B | DOOM 式光栅枪战（见 [raider/README.md](raider/README.md)） |

安装器自身带图标（SCX bit1 内嵌 + 落盘 `SYS/ICONS/INSTALLER 主题
无关`），向导页：欢迎 → 组件勾选 → 选项（**安装目录可自选**/
桌面快捷方式/默认配置/程序菜单项）→ 进度 → 完成；检测到已装
组件时提供卸载（含配置清理选项、菜单项剔除）。

## 2. 上机部署（唯一需要的步骤）

1. 把 `dist/SandCore_ExtraSoftware_Pack_1.scx` 放进数据盘任意目录
   （如 `HOME/`，用主项目的 mkfs 上盘、Files 拷贝或串口传）。
2. Shell 里 `run HOME/SandCore_ExtraSoftware_Pack_1.scx`（或桌面
   Files 双击）。
3. 向导里勾选组件、确认安装目录（默认 `/APPS`）、按需勾选快捷
   方式/配置/菜单项 → Install。
4. 完成页即提示 `sc_reload` 已刷新桌面；快捷方式指向所选目录的
   绝对路径，装在非默认目录也能从桌面启动。

卸载：再次运行安装器 → Uninstall → 勾选组件 → Remove selected。

## 3. 安装器行为合同

- 内嵌 SCX 落盘前先 CRC32 复核（构建期 mkpayload.py 记录），坏包
  拒装；写入后 STAT 复核大小。
- 配置文件**只在缺失时**写默认值：更新/重装不覆盖用户配置。
- 快捷方式 `DESK/<名>.LNK` 三行合同（标签≤31B/命令≤127B/图标≤63B）。
- 程序菜单项：候选文件写入 HOME → `sc_config_check(0)` 纯校验 →
  通过才替换 `SYS/MENU.CFG`；校验失败自动跳过，绝不写坏系统菜单。
- 每步与完成后调用 `sc_reload()` 刷新桌面。

## 4. 目录结构

```
SoftwarePack1/
  pcalc/ sheet/ hexed/ blocks/ raider/   应用源码 + icon.txt + README
  installer/installer.c                  总安装器（唯一交付物）
  icons/  *.txt                          像素画源（mkicon.py 编译成 SCB2）
  cfg/    *.CFG                          默认配置模板（随包内嵌）
  manifest.txt                           载荷清单（mkpayload.py 消费）
  Makefile                               一键构建（WSL 主 / MinGW 备）
  build/  dist/                          构建中间物 / 最终交付物
```

图标流程：`icons/<app>.txt`（像素画文本）→ `tools/mkicon.py` →
`build/<app>.scb` → `mkscx --icon` 内嵌进应用 SCX + 由 mkpayload
冻结进安装器（安装时落盘 `SYS/ICONS/<APP>.SCB` 供快捷方式引用）。
一套图标服务两处；Classic/Aurora 主题的图标渲染差异由内核桌面
负责（最近邻 vs ARGB 合成），应用与图标无需适配两套。

## 5. 构建

```
cd SoftwarePack1
make CC=gcc          # WSL（主力链，ELF）
mingw32-make CC=gcc  # Windows MinGW（备用链，PE + -j 抻平）
```

细节、平台坑（libgcc 辅助符号、`__main` 桩、MinGW 模式规则缺陷）
见 [../libex/LIBEX.md](../libex/LIBEX.md) 与 [BUILD.md](BUILD.md)。

## 6. 验证状态（未实机）

已验证：全源 `gcc -fsyntax-only` 零错误/零告警（除平台片段自带
告警）；完整链接/封装成功；SCX 头逐字段核对（魔数/入口/载荷/
BSS/栈/图标位/基址）；图标 24×24 SCB2 精确 2336B ×6。

未验证（如实声明）：客体内窗口/输入/文件写入/音频/主题行为、
安装器全部页面流转、卸载路径、菜单项校验路径、实机性能（尤其
SandRaider 的逐帧渲染帧率）。上机后发现问题在本目录登记修复，
不改动主项目。
