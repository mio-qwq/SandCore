# SandCore_ExtraSoftware_Pack_1

正式发布版，2026-10-06。面向 SandCore M9 的五个原生 i386 用户态应用，以及一个自包含 SCX 安装器。用户已完全验收主体功能；署名和关于页补齐后本包正式结项。正式名称没有 candidate、final 或版本号后缀。

copyright (c) mio 2026 · [mio](https://github.com/mio-qwq/) · made with ❤️

最终安装包：`dist/SandCore_ExtraSoftware_Pack_1.scx`，124,825 字节；装载正文 120,661 字节，低于平台 262,108 字节上限。SHA256：`e9b5623511e7a5f62085cdb8c712076d9e2e71bd9d2bec00b80dc4c796a4034b`。

用户于 2026-10-06 声明“完全验收”。本次署名与关于页面补充通过 freestanding 构建、442 项源码宿主回归（ASan/UBSan）、SCX/SCB2/符号/资源回解检查；没有启动 SandCore、QEMU 或实机。宿主图像是应用绘图代码在模拟调用上的输出，不是客体截图。

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
