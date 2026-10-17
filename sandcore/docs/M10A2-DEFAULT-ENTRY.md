# M10a2 默认启动入口与文字显示修正

2026-10-17。用户报告默认启动文字破碎，并指出QEMU快捷键和硬件加速问题。本页记录已经查明的原因、实际修改和仍待验证的范围；不是整版验收声明。

## 文字为何破碎

本机Windows实际应用DPI为144，即150%。对旧入口使用的同一Windows QEMU i386/SDL前端作无盘暂停观察，`GetProcessDpiAwareness=0`、窗口DPI=96。旧窗口没有声明DPI感知，宿主会对其位图再缩放。细点阵文字在这条显示链路上会失去清晰的像素边界。不能要求用户自行改系统设置来补偿启动器的缺陷。

修正入口使用Windows QEMU x86_64的GTK前端，关闭zoom-to-fit，并仅向子进程传入HIGHDPIAWARE兼容层。本次真实窗口观察为`GetProcessDpiAwareness=2`、窗口DPI=144。相同试玩盘在临时写入层中冷启动，默认1920×1080、150%，真实鼠标选择Settings并由Enter启动，再以Ctrl+O打开配置文件对话框。原始screendump中的中英文、表头、按钮和标题笔画完整。

原故障图片、默认设置与文件对话框原尺寸截图、QEMU命令和DPI观测在[evidence/m10a2-default-entry-01](../evidence/m10a2-default-entry-01/review.json)。该结果证明这一次默认入口的客体画面；其它分辨率的应用布局仍需定向检查。不能把窗口截图工具自动缩小后的预览当作原始像素验收。

## 启动器实际变化

- 自动优先WHPX，初始化不可用时由QEMU回退TCG；可用`--accel whpx`要求明确失败，或`--accel tcg`选择软件执行。原入口强制TCG的选择已撤销。
- 实际执行一次无盘暂停WHPX初始化与QMP关闭，退出0；随后已用修正后的WHPX优先命令真实启动双盘并看到设置/文件对话框，日志没有WHPX初始化失败或回退记录。
- 保留GTK菜单、页签与图形monitor VC。旧入口的`-monitor none`已经撤销；Ctrl+Alt+2/1对应monitor/客体显示，Ctrl+Alt+G切换输入捕获。原生自动输入工具没有取得可确认的快捷键切换画面，因此不把按键实测写成PASS。
- 不改宿主注册表、Windows缩放、系统安全设置或全局环境。每次fw_cfg熵只进入临时目录并回收。

本地试玩目录仍严格只有`run.py`、ASCII/CRLF的`run.bat`、`sandcore.img`和`sanddata.img`。只更新启动脚本，用户已运行过的数据盘保持原样。当前盘摘要见review.json；它与首次交付摘要不同，不能用旧盘覆盖用户变更。

原始命令调用后的诊断helper收到QMP quit成功，但错误地再发送一次quit，最终helper退出1、QEMU实际退出0。原报告保留此边界；helper已修正重复关闭，不将其写成客体崩溃，也不重启整套流程来制造绿色结果。

## 应用修复的独立状态

SCUI快捷键现在先解析绑定，再由执行入口动态查询禁用状态；Debug在状态发布时刷新有限命令，避免工具栏保留上一暂停/目标的旧禁用位。设置/安装错误框保留多行正文，状态栏改为单行。安装器设置区使用独立滚动布局，11个操作使用标准工具栏及溢出菜单，计划表不再被多行按钮挤走。Files退出日志增加公共ScUiErrorQuery的原始cause/raw/operation/node，尚未凭此宣称取消拖放故障已修复。

SCCC宿主IO实际检查14中SCUI/Debug/Files/Settings通过；Installer首次因误写不存在的控件名编译失败，修正为正式Panel+SCROLL_LAYOUT后检查15通过。统一HOST13退出0、41产物齐全、构建期间源码变化0。真实客体刷新07已完成这五个改变标签的编译/链接/取回，其余来源沿完整原生闭包复用；最终owner账增加16页、整轮FAIL，不能提升为已接受SDK来源。新增页已由PTE定位到常驻三环桌面heap，原因待查；新布局/快捷键回归尚未完成。

修订：2026-10-17，记录默认SDL的DPI缺陷、GTK/WHPX入口修正、冷启动原尺寸证据、快捷键实测边界与五目标应用修复。

2026-10-17后续：用户实际确认“我试了现在可以快捷键”。代理本次暂停无盘窗口检查被用户Esc中止并正常关闭，未取得可引用的自动按键结果；用户的实际确认与代理自动化证据分列。新HOST14/native08/final08已有实际构建/生成与零差owner，尚未覆盖用户试玩盘，修后GUI09继续。

2026-10-17本轮收束：HOST15/native08/final09的Windows WHPX GUI10实际完成1024×768/150%预览、Keep、关闭重开查询预览0；1920×1080/150%恢复及预览0。Settings文字/响应式按钮实际截图已查看；Debug F5/F4/F9有独立真实GUI证据。最终资源零差、driver退出0；四文件用户盘未覆盖，完整M10a2继续active。参见[PIO与GUI10](../evidence/m10a2-pio-gui10/README.md)。


2026-10-17全屏输入后续：用户报告GTK全屏后鼠标停止，并要求停止computer-use、避免过度验证。已停止该工具，仅读启动器、PS/2驱动和上游源码。GTK的gd_motion_event以宿主位置差生成相对量、到屏幕边缘靠gdk_device_warp回中；上游Windows GTK捕获缺陷见https://gitlab.com/qemu-project/qemu/-/issues/2225。该机制与现象相符，但没有本机真实输入证据确认就是同一缺陷，不能称根因已证。

试玩run.py默认改为SDL，明确Raw Input、关闭SDL额外相对量缩放、采用物理DPI坐标和最近邻；HIGHDPIAWARE及WHPX优先保持，--display gtk可作显式诊断。SDL的Ctrl+Alt+F切换全屏；Ctrl+Alt+G只在窗口模式释放捕获；Ctrl+Alt+2切换monitor窗口。已经对实际安装QEMU支持的后端及两个--check入口核验，未启动新试玩VM、未写两盘/bat；实际全屏行为仍待用户或后续获准的输入验证。具体脚本摘要和检查记录见evidence/m10a2-fullscreen-entry-01/delivery.json。

修订：2026-10-17，记录全屏相对输入的源码推断、SDL规避交付及尚未实测边界；不将上游master等同本机精确提交源码。
