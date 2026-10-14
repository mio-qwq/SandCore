# SandCore M10a2 — The Application Platform Era：组件现状审计

> 2026-10-14。审计基线为 `main` 的
> `8e1c27abfe1b0c14ddff6d23464a961eabfd8cc0`。本页记录源码与规范审阅，
> 没有启动旧组件、重新构建系统盘或进行本轮 GUI 验收。
> 本轮新增 SCL 宿主验证独立记录在末节，不能替代实际 Ring 3/QEMU 证据。

## 1. 阅读范围与证据边界

已完整阅读 `APPS.md`、`WM.md`、`GFX.md`、`FILES.md`、`STUDIO.md`、
`DEBUGGER.md`、`NOTES.md`、`CANVAS.md`、`LENS.md`、`MONITOR.md`、
`SHELL.md`、`BUILD.md` 和本轮 `M10a2RULE.MD`。直接阅读 Files、Studio、
Debug、Notes 的完整源；核对其它组件的入口、功能函数、依赖与构建接线，
并阅读 NUI 帧提交/事件/可见性、桌面布局/命中和 Monitor 分页实现。

审计时这些既有应用源与基线无差异；并行 M10a2 新源、内核改动和构建规则
不作为旧组件的运行证明。历史规范中“未构建”“继续 M8”等叙述属于沿革，
应结合最后修订及 `M8-FROZEN.md`、`M8A-RELEASE.md`、
`M10A1-ACCEPTANCE.md` 解释，不能重新启动封存任务。

本页中的“已有”表示源码中存在对应路径，不表示本轮实际运行已通过。
历史成功只沿用其原有输入与范围；未变化的证据可复用，新增行为必须另有证据。

## 2. 现有入口、功能与迁移优先级

| 组件 / 当前源与产物 | 已有功能、需要保留的行为 | 现状与 M10a2 所需能力 | 迁移顺序 |
|---|---|---|---|
| Files：`user/files.c` → `APPS/FILES.SCX` | 直接子项导航、上级/路径、五处快捷位置、单选/双击/分页、打开/编辑/调试、新目录、改名/移动路径、删除、取消、真实错误与核心保护提示 | 私有 512 项列表与单个 `selection`；文件分派硬编码。Files 2 需目录树、多选、多列表头排序/列宽、属性、搜索、批量复制移动、剪贴板与拖放进度 | 首批，依赖 SCUI + SCSHELL |
| Studio：`user/ide.c` → `APPS/IDE.SCX` | 单文档 65535B、任意 UTF-8 边界编辑、私有行索引、滚动、保存/打开/新建、C/ASM 内部编译、诊断、运行/Debug、本次文档版本与输出身份 | 单文档；私有编辑/菜单/滚动；旧 CPUINFO 的 PID 下标访问越界风险已静态确认。Studio 2 需工程/文件树、多文档、语法高亮、查找替换、Undo/Redo、错误导航、停靠布局、SCL/SDK工作流 | 首批，先完成共享编辑控件及进程对象 |
| Debug：`user/debugger.c` → `APPS/DEBUGGER.SCX` | 拥有目标、首指令暂停、真实 TF/INT3、88B现场、受控128B内存、逐4B暂停栈副本、重启/继续/暂停/撤断点、未知字节 `db XX`、静态 `.map` 标签、窄视图分页 | 私有面板与上下文菜单；单个界面断点记录；旧 CPUINFO 的 PID 下标访问越界风险。Debug 2 需断点列表、可调分割面板、模块/运行时符号与地址导航 | 首批，依赖 SC32API 调试对象及 SCL 模块信息 |
| Notes：`user/note.c` → `APPS/NOTE.SCX` | 65535B、任意编辑、整字节编码移动/删除、行索引、原 TTF 字宽、打开/保存/重载/新建/另存、未保存确认、失败保留作品、初始缺文件空文档 | 私有编辑器/菜单，缺统一选区、Undo/Redo、查找和跨应用剪贴板；同源测量可作为共享 Edit 设计的参考 | 共享编辑器成熟后 |
| Canvas：`user/canvas.c` → `APPS/CANVAS.SCX` | 512×320作品、SCB1/SCB2、直通ARGB与软边画笔、连线、RGB选择、快照Undo/Redo、清空/取消、候选打开回滚、保存失败保留作品、视口采样 | 作品不能被窗口缩放回写；现有整张撤销副本不是多步编辑历史。需统一文件对话框、工具栏、属性、快捷键和资源生命周期 | SCUI/SCGFX/文件服务后 |
| Lens：`user/lens.c` → `APPS/LENS.SCX` | SCB/BMP及异步压缩图请求、完整原图缓存、Fit/100%/Center、物理像素平移、透明正确采样、候选失败回滚、视口尺寸缓存 | 高端原图与Fit缓存归进程；依赖 IMAGE 服务。需统一文件对话框/菜单/状态和图像对象，保留原生像素与失败旧图 | 图像对象与 SCGFX 后 |
| Settings：`user/settings.c` + `SETTINGS.inc` → `APPS/SETTINGS.SCX` | 六页：显示预览确认/回退、主题/壁纸、菜单/快捷方式、显示配置、环境、文本配置；跨页草稿、逐项校验、会话目录与登录入口 | 私有页签/表单/路径框；显示配置不等同内核身份。需 PropertySheet、统一配置模型与错误展示 | SCUI 属性页与 SCSHELL 后 |
| Monitor：`user/monitor.c` → `APPS/MONITOR.SCX` | PIT快照CPU/RAM历史、任务列表、卷真实字段、冻结/恢复/刷新、分类/图线/任务与字段滚动 | 已通过 PROCESSPAGE 动态收集任务，不能误称仍限八任务；两份缓冲复用、PID/代数匹配、失败不冒充0%。需 TableView/图表对象与事件驱动采样 | SCUI 表格后 |
| Calculator：`user/calc.c` → `APPS/CALC.SCX` | 两个有符号32位整数的四则、31B输入、鼠标/键盘、退格/清除、除零/溢出/INT_MIN除法守卫 | 不是科学计算器或通用表达式引擎；需共享按钮、菜单、复制结果与明确数值范围 | 基础 SCUI 后 |
| Palette：`user/palette.c` → `APPS/PALETTE.SCX` | 原DAC快照、RGB十六进制、选择/翻页、三种程序壁纸、主题图片优先提示与真实保存错误 | 读取256槽但当前只显示/选择1–104；APPS旧叙述与源码不同。需明确完整槽表与已登记/未登记状态 | 基础 SCUI 后 |
| SandAudio：`user/audio/player.c` → `APPS/SOUND.SCX`；`user/m9/soundplay.c` → `BIN/SOUNDPLAY.SCX` | MP3/WAV/FLAC、封面/曲目信息、播放暂停/重播、跳转、音量、迷你/展开、后台续流与退出回收 | 解码核心与宽松许可证完整保留；播放器后台音频不能因隐藏窗口停止。需统一按钮/进度/文件选择，不能随GUI迁移改解码边界 | 音频对象与 SCUI 后 |
| 登录/会话：`user/session.c` → `APPS/SESSION.SCX` / `SYS/LOGIN.SCX` | 普通身份登录票据、密码清除、会话分页、切换、确认注销；SYSTEM只从外部管理进入 | 隐藏桌面不能暂停认证后台工作；需共享输入框/PropertySheet，不暴露凭据或提升普通/root权限 | 系统对象与 SCUI 后 |
| 桌面与图形终端 | 桌面图标/菜单来自文件；独立会话、主题资源、窗口任务栏分页、窗口右键、终端字节流/历史滚动 | 桌面布局固定网格；图标点击直接启动，无持久位置/多选/框选/拖动协议；公开跨进程剪贴板/拖放接口尚未在现有源中找到 | SCSHELL 与跨进程对象成熟后 |
| 命令行开发工具：SCCC/SandAsm、nano、scdbg | 原生编译、汇编、全屏源码编辑、命令行调试与串口工作流保留 | GUI Studio/Debug迁移不能替换或删除CLI入口；SCL生成/链接能力需正式工具链与新头协议 | 工具链并行保留，平台先完成 |
| 安装：`user/m9/install.c` → `BIN/INSTALL.SCX` | 文件复制、目录创建、UID/GID与mode设置；真实失败码 | 当前是CLI文件安装工具，不能把它记为现成GUI安装器。Windows SandFS镜像编辑器是独立宿主工具 | 如重制GUI安装组件须先明确包与事务对象 |

### 构建清单的完整口径

基线 `Makefile` 的 `USER_APPS` 有20个声明入口：

```
files note palette calc mines probe ide debugger lumen race world
settings lens canvas monitor rgbprobe themeprobe imageprobe sysprobe session
```

其中10个普通工具为 Files/Notes/Palette/Calculator/Studio/Debug/Settings/
Lens/Canvas/Monitor；1个会话入口；4个历史游戏 Mines/Lumen/Race/World；
5个探针 probe/rgbprobe/themeprobe/imageprobe/sysprobe。探针不能计为完成的桌面软件。
SandAudio、登录别名、终端、编译器/汇编器由另外的规则安装。

基线顶层还有19个旧CLI声明；`m9.mk` 按 `user/m9/*.c` 枚举155份
CLI源（不含由独立音频链处理的 `soundplay.c`），其中有覆盖同名旧CLI的规则。
`m10.mk` 明列19个网络工具：

```
ip ifconfig ifup ifdown route arp arping ping traceroute netstat
nslookup hostname dnsdomainname ipcalc udhcpc nc wget tftp curl
```

这些是源码/规则数量，不是本轮产物数量或行为覆盖分子；旧19项与新同名命令
不能相加冒充独立程序。网络HTTP/HTTPS边界、`nc -e`、外部串口最高权限和
历史nano/ed（用户排除vi）的合同不因新GUI变化。

## 3. 桌面与 Shell 的真实实现位置

当前桌面 Shell 快捷方式来自 `assets/m9/desk/shell.lnk`：
`Shell` / `bin/sh.scx` / `@SHELL`，其实现为 `user/m9/sh.c`。
`user/shell.c`、`user/shell.asm` 和 SHELL.md 的早期段落保留历史，
不应用来描述当前默认字节流 Shell 的功能范围。

`kernel/desktop.c` 仍有每会话32个菜单项、24个图标项和32个图片缓存项。
原生图标坐标由 `wm_native.inc:icon_position()` 按索引/页号计算；
图标命中后在按下沿直接执行命令，窗口拖动代码操作的是窗口。
VGA旧路径显示前四个图标并直接启动，须继续保留回退行为。

开始菜单已经有文件配置与分页，右键可打开/设置/刷新/删除快捷方式；
仍不能将这套路径称为多级菜单模型、自由图标布局、文件拖放或剪贴板。
SCSHELL的新数据模型应包含应用身份、关联规则、快捷方式、布局、选择集、
文件操作事务与拖放状态；各会话拥有各自对象，内核只保留必要协调和权限。

## 4. 静态确定的问题与公共框架需求

### 4.1 动态任务与旧八槽身份查询

`SCAPI.H:91` 的 `SC_CPU_WORDS` 固定为64。旧CPUINFO头占16字，
每槽6字，只提供旧八槽；`16 + pid * 6 + 2` 在 `pid == 8` 时为66，
已经超过 `cpu[64]`。当前源码中的访问点：

- Studio：`ide.c:300`、`:309`、`:316`。
- Debug：`debugger.c:207`、`:213`、`:393`。

这属于源码可确定的越界，不依赖额外QEMU测试才能确认。M10a2重制应使用
版本化进程对象/分页行中的PID与代数，不扩大旧CPUINFO缓冲或更改SCAPI.H。
需要保留STATUS前后同代数确认、目标owner检查和退出资格失效，不能只删除代数检查。
Monitor已经使用PROCESSPAGE动态缓冲，应复用其身份思路，避免把旧八槽问题泛化到它。

### 4.2 字体度量尚未统一到共享编辑模型

Studio的 `column()`、`move_column()`、横向裁剪仍将所有非ASCII按两列处理；
Notes的 `character_cells()` 使用同源 `ui_scalar_width()`。共享Edit必须
把UTF-8字节位置、字符边界、实际字形宽度、显示列/像素及选区分开，并让
绘制、光标、键盘上下移动和鼠标命中使用同一测量结果。
不能沿用固定两列假定造成拉丁扩展/希腊字母/标点光标偏移。

### 4.3 菜单与控件缺少共同对象

Files `context_geometry()`、Studio `menu_geometry()`、Debug `menu_geometry()`
分别计算列数、尺寸、屏蔽背景事件、绘制与命中。Notes/Monitor也有类似私有路径。
NUI是每个SCX复制的源片段，提供即时绘制和按下命中，并非保留控件树的SCUI。

SCUI必须提供统一控件所有权、焦点/键盘导航、菜单命令ID/快捷键/子菜单、
浮层模态阻挡、滚动/布局、文本选择/撤销、表格/树与生命周期。
现有短点击、菜单外点击只关闭、不穿透、动作优先保留队首字符的行为要写进共同合同。

### 4.4 性能底座已有，但应用提交仍是完整帧

`NUI.inc:ui_present()` 仍提交实际客户区 `width * height * 4` 的FRAME32。
已有hover矩形缓存、按键/按钮边沿即时唤醒、事件等待、隐藏停帧与切回重画，
不能把当前工具描述为持续无条件整帧轮询。现有合成器已经能按损伤区域呈现；
客户端完整帧生成/复制与合成器局部提交是不同成本。

SCUI的脏区/文本/资源缓存先明确失效和归属，再用同分辨率/主题/输入/数据规模
比较实际重绘/复制像素、页数、装载时间、客体PIT、宿主单核心CPU与输入长尾。
本页的静态结构分析或末节微型运行时夹具不证明整机性能优秀。

### 4.5 Palette的文档与源码差异

APPS.md的旧M8叙述称“全部256个原DAC槽”；源码虽然读取 `raw_colors[256]`，
`palette.c:select_slot()` 却夹到 `PAL_SKY..PAL_UI_LINE`，绘制循环也限定
`<= PAL_UI_LINE`。`kernel/palette.h` 的 `PAL_UI_LINE` 为104。
因此当前实际可查看范围是1–104；不能把读入快照等同完整UI可达。
重制时补齐显示口径、既有颜色角色和未登记槽的状态，新增UI颜色仍先登记GFX规范。

## 5. 迁移时必须保留的合同

1. SCAPI.H原字节与旧入口冻结；SCX/SCF/SCB、旧FRAME/FRAME32、VGA回退、
   用户提供字体和已发布应用保持兼容。新API在大写SC32API.H/SCUI.H等公开头中定义。
2. 文件真实身份不取视觉省略文本；63B身份与128B候选分开，超长整条拒绝。
   取消、短读、坏格式、OOM、保存失败保留当前作品/撤销/脏标记，权限由内核判定。
3. Studio只有本次源码版本/本次输出/同一编译任务实际成功才可运行；
   Debug只操作拥有目标，暂停副本明确标记，未映射内存不能显示伪零。
4. 字体统一来自原凤凰TTF，字符宽度与缓存版本一致；主题不改作品颜色，
   Canvas作品与Lens原图尺寸不随窗口改变。Classic/Aurora共享行为与布局。
5. 隐藏GUI停止生成帧，Shell/网络/编译/认证/音频后台继续；剪贴板/拖放按
   会话隔离与文件权限处理，不把普通/root桌面变成SYSTEM管理入口。
6. 旧源码以该提交和既有历史包保留，旧已发布SCX保留原字节；不复制整树或
   巨型磁盘创建冗余归档。基线Makefile含M8游戏，不能用泛化全目标重编重启封存游戏。

## 6. 本轮 SCL 宿主定向证据

命令：

```powershell
wsl.exe sh -lc 'cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore && python3 tools/test_scl_runtime.py'
```

输入为真实 `user/runtime/scldr.c` 与 `scl_format.c`，夹具为
`tests/m10a2/runtime_host.c`；结果在 `build/m10a2-runtime-host/report.json`，
编译与运行日志在同目录。报告记录每份源码/头/构建器/夹具的SHA256和本次执行期间
输入未变化；WSL64位C宿主用低32位真实地址映射、mprotect与UBSan。

20个代表性case、1169次断言通过，输入库共11596B。覆盖：

- 四库多级/菱形依赖、函数/数据导入，内部与外部ABS32/PC32独立预期、数据/BSS。
- 重复open、首次与常驻库的ABI/minor/release拒绝、缺库/符号、错误kind/导出宽度、
  环、格式/范围/文件声明身份错误；输出句柄与资源回滚。
- init失败严格逆序fini、保留先前成功依赖；create/文件缓冲/模块工作区/元数据/
  映像分配故障与保护失败，失败后同一运行时可重新打开与回收。
- 依赖引用不能被无显式所有权的Close偷减；查询地址固定模块至Destroy；
  两个独立runtime的重定位与可写数据隔离；回调重入返回BUSY。
- 先装SIDE，再MID，再ROOT的实际全局初始化逆序，避免只验证单次DFS清理顺序。

初始化回调只记录次序与模拟失败，不执行库中的i386指令。此证据不是
SandCore Ring3调用、客体异常回收、内核映射/权限、共享物理代码页或GUI验收；
这些仍由对应阶段的真实客体证据确认。

## 7. 修订记录

2026-10-14：建立基线组件/入口/依赖/保真清单，记录旧CPUINFO越界、
字体测量/私有菜单与Palette口径差异；新增绑定输入的SCL宿主验证范围，
不宣称本轮桌面运行或M10a2整体完成，不重启封存M8。
