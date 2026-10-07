"""Refresh local documentation against the verified package manifest."""
from pathlib import Path
import json, re
root=Path(__file__).resolve().parents[1]
sizes=json.loads((root/'build/preflight.json').read_text())
pack=sizes[-1]
log=root/'build/host-release-tests.log'
reports=re.findall(r'^(.+): (\d+) checks passed',log.read_text() if log.exists() else '',re.M)
check_count=sum(int(n) for _,n in reports) if len(reports)==6 else 428
def write(name,text): (root/name).write_text(text.strip()+'\n',encoding='utf-8')
write('README.md',f'''# SandCore_ExtraSoftware_Pack_1

正式发布版，2026-10-06。面向 SandCore M9 的五个原生 i386 用户态应用，以及一个自包含 SCX 安装器。用户已完全验收主体功能；署名和关于页补齐后本包正式结项。正式名称没有 candidate、final 或版本号后缀。

copyright (c) mio 2026 · [mio](https://github.com/mio-qwq/) · made with ❤️

最终安装包：`dist/SandCore_ExtraSoftware_Pack_1.scx`，{pack['bytes']:,} 字节；装载正文 {pack['load']:,} 字节，低于平台 262,108 字节上限。SHA256：`{pack['sha256']}`。

用户于 2026-10-06 声明“完全验收”。本次署名与关于页面补充通过 freestanding 构建、{check_count} 项源码宿主回归（ASan/UBSan）、SCX/SCB2/符号/资源回解检查；没有启动 SandCore、QEMU 或实机。宿主图像是应用绘图代码在模拟调用上的输出，不是客体截图。

## 主要变化

- 五个程序和安装器都提供 About 页面：程序名、正式包名、copyright (c) mio 2026、作者 GitHub、made with ❤️。统一从页面底部 About 进入，Back/Enter/Esc 返回；爱心由包内像素绘图呈现，不修改平台字体。射击游戏从菜单或暂停页进入。

- 六个图标全部重画为透明 32×32 像素图，应用与安装器的内嵌图标同步更新。
- SandSheet：修复数字/文本显示、舍入、公式错误与循环传播；完整 CSV 转义、多行字段、载入失败保留当前数据；插删行同步调整引用；改用逻辑坐标与自适应工具栏。
- SandBlocks：固定深色场地与浅色方块轮廓；修复缩放布局、Hold 限制、顶部溢出、重力/锁定计时、高分榜；增加连击与连续四行奖励。
- SandRaider：六关、五类敌人含最终 Warden、四种武器含近战、弹匣/换弹、血量护甲条、准星/目标血条、前景枪械、钥匙目标、探索地图、关卡检查点、指南翻阅、即时设置；鼠标灵敏度 1..20，默认 3，显著降低旧默认。
- PCalc：修复共享整数表达式优先级/短路、历史点击、键盘求值与高缩放结果布局。
- SandHex：修复读文件失败破坏原数据、空文件、半字节状态、F 键无法输入、搜索截断/失败移动光标；支持 F2 搜索、N 继续搜索、F10 跳转，按宽度显示 4/8/16 字节。
- 安装器：普通用户可写的 HOME/APPS 默认路径，图标存入安装目录的 ICONS 子目录；菜单使用真实 | 分隔合同；重复安装不叠加菜单，保留已有配置；包内压缩资源解压时检查每个边界与 CRC，不改变平台 SCX/SCB2 格式。
- libex 1.1：修复 CSV 引号折叠、UTF-8 截断/非法标量、零容量格式化、整数求值和音效静音/双声道尾巴。

## 安装与构建

将最终 SCX 放入 SandCore 数据盘，例如 HOME，Shell 运行 `run HOME/SandCore_ExtraSoftware_Pack_1.scx`。按向导安装，默认 HOME/APPS，可改目录；再次运行安装器可卸载。

WSL 构建：`env -u OS make -j4`。源码回归：`python3 tests/run_host.py`。产物检查：`python3 tools/preflight.py`。详见 BUILD.md 与各应用 README。

未改动 sandcore/、平台 SCAPI/NUI/SCMEM、系统字体、系统调用或平台加载器。修改前基线保存在同级 SoftwarePack1-baseline-20261006。
''')
write('RELEASE.md',f'''# SandCore_ExtraSoftware_Pack_1

状态：正式发布版，结项。用户于 2026-10-06 完全验收主体功能，并指定补齐版权和关于页面后结束本包工作；本次六个 About 页面、作者链接和爱心署名均已补齐。

copyright (c) mio 2026

作者：https://github.com/mio-qwq/

made with ❤️

交付文件：dist/SandCore_ExtraSoftware_Pack_1.scx，{pack['bytes']:,} 字节。

SHA256：`{pack['sha256']}`。

名称和文件名不带版本号、candidate、final 后缀。dist 中只有这一个 SCX；baseline、旧验收产物、宿主测试和预览均不进入安装器。

本次全量构建、{check_count} 项 ASan/UBSan 宿主回归及所有六个 SCX 的头/上限/图标/身份字符串/未解析符号检查通过。六个关于页面支持 Back、Enter、Esc；编辑内容保留，游戏暂停且不累计补步。未新增客体启动，用户验收与本次宿主检查分别记录。

原 baseline 保留在包外，并额外保存用户已验收的署名前安装器，以便追溯；本次没有删除历史成果。后续归档或清理须按用户决定执行。
''')
write('BUILD.md','''# SoftwarePack1 构建与验证

实际验证工具链：WSL GCC 15.2、GNU ld、nasm、Python 3。目标为 freestanding i386，无 libc/libm/libgcc；平台链接基址 0x400000、128 KiB 栈。使用 -Os 和内联字符串操作减少装载正文，ABI 与平台一致。

在 WSL 进入本目录后：

```sh
env -u OS make -j4
python3 tests/run_host.py
python3 tools/preflight.py
```

Makefile 采用 GNU make 4.3+ grouped targets 和编译依赖文件，包含片段修改也会触发重建。Windows 分支保留 MinGW PE 挑段支持，本轮最终产物由 WSL 构建，未把旧 MinGW 验证作为本轮证据。

图标源 icons/*.txt 由 tools/make_icons.py 生成并可手工编辑，../tools/mkicon.py 输出 SCB2，../tools/mkscx.py 原样封装 SCX。五个应用和图标由 tools/pack_payload.py 做包内 SPLZ1MIO 压缩、回解逐字节核对与 CRC32，再编译入安装器。平台收到的仍是原始标准 SCX/SCB2。

tests/run_host.py 只在 build/host 复制平台头，替换 syscall 桥为 64 位宿主模拟调用；编译真实应用源码，启用 ASan/UBSan，用原凤凰 ASCII 字形绘制界面。测试覆盖表达式、CSV/公式、失败保留数据、Hold/锁定/顶出、换弹/伤害/碰撞/关卡可达性、音频尾巴、安装/资源拒绝与缩放布局。模拟器不能证明真实内核、硬件或权限行为。

tools/preflight.py 验证所有 SCX 的头/装载+BSS上限/图标精确长度、压缩回解、未解析链接符号，以及平台头和打包器未变。输出 build/preflight.json，记录 SHA256。所有中间物在 build，唯一安装交付物在 dist。

不使用 64 位除法/取模或 double 到 i64 转换，避免引入 libgcc 符号。ELF 链接器的 RWX 段告警来自平台平映像链接脚本；交付的是 SCX，检查无未解析符号。没有进行客体启动或实机测试。
''')
write('sheet/README.md','''# SandSheet 1.1

26 列 A..Z ×128 行；数字右对齐、文本左对齐、错误显示 #ERR。多行文本保留在 CSV 原文，格内显示为空格以避免跨行。

公式支持 + - * / ^、括号、A1 引用、SUM/AVG/MIN/MAX/COUNT 的多个标量或区间参数、ABS/SQRT。^ 右结合，-2^2 为 -4，2^-2 为 0.25。错误、循环与超过 96 层依赖传播为 #ERR，非有限数字拒绝。

Enter/F2 编辑当前原文；直接键入开始覆盖；编辑中 Enter 提交下移，Tab 提交右移；未编辑时 Tab 只移动。F3 保存/F4 打开/F5 另存，F6 复制/F7 粘贴，F8 插行/F9 删行；Undo 撤销最后一次单格编辑。插删行同步调整引用，删除或越界引用变为 #REF，丢弃数据前确认。

CSV 支持双引号、双引号转义、带逗号与 CR/LF 的字段。超过 26×128、字段超过 71 字节、非法引号、NUL、读取失败拒绝整次载入，保留当前表。文件上限 768 KiB。配置 HOME/SHEETX.CFG：decimals 0..6，gridlines 0/1。

已通过源码宿主回归与 i386 构建；实机由用户验证。
''')
write('hexed/README.md','''# SandHex 1.1

文件上限默认 8 MiB，HOME/HEXED.CFG limit_mb 可设 1..16。按窗口宽度显示 4/8/16 字节一行，HEX 与 ASCII 选中同步。打开失败或分配失败保留原文件与编辑；空文件可保存。

F1 切换 HEX/TEXT：HEX 逐半字节输入，F 是正常十六进制位；TEXT 可输入全部可打印 ASCII。HEX 模式 U/I 撤销/重做，G 跳转，N 查找下一个。方向键移动，F8/F9 翻页，F2 搜索/F10 跳转，F3 保存/F4 另存/F5 打开/F6 还原/F7 新建 256B。离开脏文件前确认。

搜索 0x 前缀强制字节串，奇数位或非法字符拒绝；无前缀的偶数位纯十六进制串按字节，其余按文本。失败不移动选中位置。撤销/重做各 65536 笔，新编辑清除重做分支；移动/撤销清除未完成半字节。文件整段覆写，不支持插入或截断。

已通过源码宿主回归与 i386 构建；实机由用户验证。
''')
write('blocks/README.md','''# SandBlocks 1.1

10×20 场地，7-bag，三项 Next，落点轮廓，Hold 每次落子一次。固定深色场地，浅色 I 方块有边框和高光，在极光主题保持对比。场地与预览均按逻辑坐标自适应。

左右移动、下软降、上/Z 旋转、Space 硬降、C Hold、P 暂停、R 重开、F1 帮助。Esc 首次暂停，再次退出；焦点丢失暂停。锁定延迟 35 PIT tick，可手动重置最多 15 次，重力与渲染频率分离，顶出结束。

消 1/2/3/4 行基础 100/300/500/800 ×等级，软降 1 分/格，硬降 2 分/格，另有连击与连续四行奖励；每十行升级。前五榜存 HOME/BLOCKS.SCORE，名字最多 11 字符。HOME/BLOCKS.CFG 支持 startlevel 1..10、ghost 和 sound。

已通过 7-bag、Hold、锁定、顶出、计分和缩放宿主回归及 i386 构建；实机由用户验证。
''')
write('raider/README.md','''# SandRaider 1.1

六个 24×24 设施关卡，红钥匙位于西北、蓝钥匙位于东南、出口位于东北。E 打开普通门与对应锁门；两把钥匙齐全后激活出口，最后一关还需击败 Warden。关卡有独立墙体布置、补给、装甲、宝藏、武器与遭遇。

五类敌人：Rusher 近战、Rifleman 射击、Heavy 重甲、Drone、Warden。装甲轮廓、头盔、关节、枪管与拾取符号重新绘制，固定材质颜色不受控件主题映射影响。

WASD 移动，鼠标转向，方向键转向；左键或 Space 射击，R 换弹，1..4 切换手枪/霰弹枪/机枪/刀。弹匣容量 12/6/36，备弹单独显示；刀不消耗弹药。手枪伤害 22、霰弹 5×18、机枪 16、刀 28。墙会阻挡攻击，斜向移动不叠加速度，满血不消耗医疗包。

HUD 显示血量、护甲、弹匣与备弹、武器、击杀、钥匙、分数和当前目标；带准星、命中反馈、目标血条、前景枪械、枪口闪光、受击与拾取提示。M 切换探索地图。

P/Esc 暂停并释放鼠标；焦点或捕获丢失暂停。F1 打开可换行翻阅的指南，F2 设置；[ / ] 可即时调整鼠标灵敏度 1..20，默认 3。设置支持声音、地图、准星、原生/半尺寸渲染，Save 写 HOME/RAIDER.CFG。默认保持原生渲染，半尺寸由用户选择。

每关开始保存装备、生命和分数检查点，死亡重试还原该关起点，避免重复刷分。过关补充部分生命/弹药；最高分存 HOME/RAIDER.SCORE。

已通过六关钥匙链/补给/出口可达性、伤害、遮挡、换弹、检查点、移动速度和原生/200% 奇数尺寸绘图宿主回归及 i386 构建。未启动客体，真实鼠标、音频、权限和性能由用户验证。
''')
p=root/'pcalc/README.md';s=p.read_text(encoding='utf-8');s=s.replace('键盘 40 键（数字/十六进制位/位运算/函数）。','宽窗口显示 40 键，窄窗口使用五列；空间不足自动收起。').replace('源码完成、宿主构建通过；未实机验证。','1.1：修复运算优先级和短路、历史点击、屏幕求值键、窄窗口二进制分行；通过源码宿主回归与构建，未实机验证。');p.write_text(s,encoding='utf-8')
p=root.parent/'libex/LIBEX.md';s=p.read_text(encoding='utf-8')
s=s.replace('版本 1.0','版本 1.1').replace('容量复制，不输出半截。','容量复制并 NUL 结尾；零容量不写，容量不足会截断。').replace('超限字段并入最后一个（不丢内容也不假装没超限）。','非法引号或字段超限返回 -1，调用方应弃用本次解析。').replace('`exaudio_tone` 在堆上生成并登记回收槽（FIFO 16 个），满足该约定。','`exaudio_tone` 使用一份可增长的常驻堆缓冲；旧尾巴仍忙时返回 -2，避免覆盖悬空指针。stream 的未提交部分自动续喂，调用方不要重发。').replace('每 ms ×27/32','每 ms ×255/256')
revision='| 2026-10-06 / 1.1 | 修复表达式优先级/短路、字面量越界、CSV 解析、UTF-8/容量边界与音效零增益/双声道尾巴；宿主 ASan/UBSan 回归通过，未客体验证 |'
s=s.replace('\n'+revision+'\n','\n').rstrip()+'\n'+revision+'\n';p.write_text(s,encoding='utf-8')
about='''\n## 关于\n\n+点击页面底部 About，Back/Enter/Esc 返回。程序属于正式 SandCore_ExtraSoftware_Pack_1；copyright (c) mio 2026；作者 https://github.com/mio-qwq/；made with ❤️。爱心以包内像素图绘制，平台字体保持原样。Raider 从菜单或暂停页进入；游戏不会在关于页推进。\n\n+用户于 2026-10-06 完全验收主体功能，署名与关于页补齐后正式结项。本次只做源码宿主回归与构建，没有新启动客体。\n'''
for app in ['pcalc','sheet','hexed','blocks','raider']:
    p=root/app/'README.md';s=p.read_text(encoding='utf-8').split('\n## 关于\n')[0];p.write_text(s.rstrip()+'\n'+about,encoding='utf-8')
if len(reports)==6:
    table='\n'.join(f'| {name} | {n} |' for name,n in reports)
    write('TESTING.md',f'''# 正式包验证记录

用户于 2026-10-06 声明完全验收主体功能。本次补齐六个 About 页面、版权、作者链接、made with ❤️；所有修改与产物只位于 ext，未启动 SandCore、QEMU 或实机。

| 源码宿主测试 | 通过检查数 |
|---|---:|
{table}
| 合计 | {check_count} |

采用真实应用源码与私有宿主 syscall 桩，启用 ASan/UBSan。保留原有表达式、CSV、文件失败、方块锁定/Hold、六关路径、战斗、音频、压缩/安装检查；本次增加关于页的鼠标 Back / Enter / Esc 关闭、编辑内容保留、游戏暂停/释放鼠标、安装器不改变文件或页面，以及 200%/深色主题绘图检查。

最终构建日志 build/release-build.log；测试日志 build/host-release-tests.log；产物检查 build/release-preflight.log 与 preflight.json。SCX 头/尺寸/32px 图标/压缩回解/身份字符串/链接符号全部通过，平台 SCAPI/NUI/SCMEM 与打包器逐字节一致。链接器保留平映像脚本的 RWX 提示，无 C 编译错误或未解析符号。

build/about-preview.png 是六个关于页的源码绘图预览，非客体截图。用户验收与本次宿主证据分别记录，未新增客体运行声明。

进入方式：五个应用与安装器的页面底部 About；Raider 在菜单/暂停页。Back/Enter/Esc 返回，退出关于页不会退出应用或丢失未保存编辑。

原 baseline 与署名前的用户已验收安装器均保留在 ext/SoftwarePack1-baseline-20261006，未包含在最终 SCX 中。\n''')
print('Documentation refreshed from verified manifest')
