# 同一普通会话的一条 P6 GUI 验收路径

这是只读准备，尚未执行。没有启动 QEMU、打包、复制/写盘、改 main 或 Git。Root 在已有当前 Windows 双盘会话里复用外层 harness，所有操作是产品实际菜单、控件和 HMP/PS2 输入。

## 先避免三个错误入口

1. 真正工程文件是 **/SYS/SDK/EXAMPLES/RDESK/RDSTATS.SCP** 和 **RDESK.SCP**，没有 ResearchDesk.SCP。前者1 TU独立SCL，后者 MAIN.C/MODEL.C/VIEW.C/IO.C 四TU动态SCX。
2. Studio“工程 → 打开工程…”没有默认快捷键；Ctrl+O只是打开文档。F5构建、F6运行、F7调试。F7已经执行 /APPS/DEBUG2.SCX --launch ... 并暂停目标，不再Ctrl+F5重复启动。
3. ScuiInput.snapshot/F12、parse_snapshot、screen_point绑定SCUIDEMO的诊断和main/second角色，**不能直接套正式应用**。本流程没有测试专用按钮或命令注入。

## 一次进入和一个工作目录

使用已审定当前产物/原生来源及重新签发的可信 SCSHELL 摘要；确认实际普通 UID1000、NORMAL、SID/PID/gen，应用与桌面同SID。先让桌面/字体到现有稳定基点，记录一次owner/resources账。

从真实 Start 打开 Files2。当前桌面没有已实现Win键；点任务栏“开始”或桌面自己的Ctrl+Esc。Files2 Ctrl+L进入 /HOME，用“文件 → 新建目录”建立 **/HOME/P6GUI**，以下称D。若此名已有用户内容则换本人新名。确认 /HOME/RDESK、/HOME/RDWORK、/HOME/RDPLUGIN 可写，缺目录也用Files建立。SDK源保持只读，不预制输入夹具。

## 紧凑主流程

| 步骤 | 真正用户输入 | 必要事实 |
|---|---|---|
| 1 Notes保存→Files关联 | Start打开Notes2，在编辑区输入一行ASCII正文 P6 native GUI note。Ctrl+Shift+S保存为D/NOTE.TXT；等完成后关闭Notes。Files2 Ctrl+L进入D，选NOTE.TXT按Enter。 | Notes标题星号消失且状态“已持久保存”；实际文件同正文和本人权限。普通Enter走txt默认关联重新打开Notes2并显示相同正文，不能用显式Studio路由冒称默认关联。 |
| 2 菜单/剪贴板 | Files选NOTE.TXT右键，确认共享Open with/Properties是真菜单；取消。表内Ctrl+C，进入新建的D/CLIP，Ctrl+V。完成后选CLIP/NOTE.TXT Ctrl+X，进入新建D/RETURN，Ctrl+V。 | COPY原件保留、目标同字节；CUT发布意图时不删源，只有durable MOVE后CLIP源消失、RETURN目标有效。Properties核真实uid/gid/rw/revision；用Open with子菜单选一次实际Notes2，验证显式路由。 |
| 3 Files→Desktop真Drop | 露出桌面空白和Files详情表。在D选NOTE.TXT，左键按下，移动超过5px阈值，拖到桌面空白释放；实际“放置文件”对话框选“复制到目标”。 | 桌面实际desktop_path下出现NOTE.TXT文件，非.LNK、非仅图标布局移动；D原件保留。同字节/新revision之外，还须真实DONE+DURABLE结果。 |
| 4 Desktop→Files真Drop/取消 | Files显示空的D/DROPBACK，桌面NOTE.TXT图标仍露出。从该图标拖进Files表空白并释放，接收对话框选“移动到此目录”。再做一次Drop并选取消。 | desktop指针跨到外国真实窗口时自动由布局拖动切FILE transfer，无需伪造Drop。durable MOVE后桌面源消失，DROPBACK目标同字节；取消那一次两端原字节/修订保持，鼠标捕获和选择恢复。 |
| 5 Studio独立SCL | Studio点“工程 → 打开工程…”，选RDSTATS.SCP，F5。 | 树中RDSTATS.C是source，RDSTATS.H不是；输出有真实SCCC --object和SCLD --scl，退出0、objects=1；正常发布 /HOME/RDESK/RDSTATS.SCL及.map/.sdbg。SCL不单独Run。 |
| 6 Studio四TU→运行 | 同一Studio打开RDESK.SCP，F5；等当前构建完成、产物发布后F6。 | 四次SCCC --object和一次SCLD --dynamic @response，objects=4。当前构建资格属于本项目/代数，不借旧文件存在。实际RDESK窗口有树、双文档、四行数据和图；rows=4、sum=40、mean=10，真实加载同目录RDSTATS.SCL。stdout可能全缓冲，先看GUI事实，正常退出后再核Studio捕获的RDESK_ANALYSIS，不送F12“刷新”。 |
| 7 Studio→Debug2 | 先正常退出F6的RDESK并等Studio领完任务，再F7。等待Debug2暂停/模块/符号候选完整。Address清空填main，F9加断点，F5继续到断点，F8单步一次。 | F9=Add BP，F5=Continue，F8=Single step，F4=Pause。main须来自实际SHA绑定.sdbg；如歧义，从Symbols抄真实Module!Symbol，不猜地址。核同一target PID/gen、BP命中数、stop_generation推进及新EIP/寄存器快照。正常关Debug2，等它领完target，再等Studio领完Debug2。 |

共享文件对话框的输入顺序：**上方路径框填目录→Enter→等真实目录加载；下方名称框填叶名→确定**。例如目录 /SYS/SDK/EXAMPLES/RDESK，名称 RDESK.SCP。路径框Enter负责进入目录，不能把完整文件路径填那里后声称已打开。业务输入每次只发一次，不能因超时重放不确定MOVE/COPY。

## ACK与只读观察

Files2的 f2_jobs_step 只有 RESULT_DONE 且 flags&DURABLE 才计“已持久提交”；SOURCE_RETAINED/FAILED/UNKNOWN分列。短UI状态可能被刷新覆盖，要抓终态或同一作业的实际恢复日志，不能用图标出现、100%或字节相同代替ACK。

Files的实际 recovery_path 是本人 recovery_directory下的task_generation-serial.REC；desktop沿本人SHELLSTATE。可复用 guest.client.get(actual_path, local_path)只读取回，核SCSFJ1头/CRC、连续frame/CRC、item/operation/state/stage/status/flags及worker PID/gen。现有 audit_m10a2_file_job_results.py 的解析结构可参照，但它绑定旧131079B/3frame夹具和旧报告状态，不能直接拿本NOTE.TXT运行后称PASS。未取得ACK就保留未验证，不加测试按钮/新probe替代UI。

Notes须COMMITTED+DURABLE且正文revision仍相同才清dirty；Studio须受管任务真实退出、SCLD产物和正常发布都完成才有运行资格。辅助事实用现有只读PathQuery/FileInfo、文件字节、PID/gen、实际输出栏。外国控件没有通用公开跨进程枚举API，不持伪ScUiContext调用，不写进程内存、不发伪Broker command。

## 其余组件各一条代表动作

| 组件 | 代表动作 |
|---|---|
| Canvas2/Lens2 | Ctrl+N真512×320图，Brush拖一笔，Ctrl+Z撤回、Ctrl+Y恢复；Ctrl+Shift+S保存D/DRAW.SCB。Files Enter按scb关联开Lens，核原尺寸/笔迹和真实保存提交。 |
| Settings2 | 显示页选已支持的不同模式/缩放，点预览，再Revert或等系统超时回退；核真实恢复原尺寸/scale，不点Keep。 |
| Terminal2 | 默认/BIN/SH.SCX -i，同一tab输入 echo native-pipe \| cat，看到native-pipe；再 nano D/NOTE.TXT，全屏打开后Ctrl+X回同一shell。 |
| Sound2 | 通过实际菜单打开现有可读WAV/MP3/FLAC，播放后前进10秒或秒数框+跳转；打开另一现有音频准备时点取消，保留原有效音频。未知进度不造百分比。 |
| Session2 | Ctrl+Shift+L进入实际LOGIN_PANEL（作用于自身SID，非选中表行）；用Root已有普通凭据登录。按现源码成功登录创建独立普通SID，核实际新SID/旧SID后台事实，必要时Sessions切回原SID，不能预写“原SID解锁”。密码不进文档/physical_input日志，不做创建账户或SYSTEM绕过。 |
| Calc2 | (12+3)*2 Enter得30；点“编辑 → 复制结果”，粘贴到Notes未保存文档；1/0应错误而不崩溃。 |
| Palette2 | 原始DAC256槽选一槽，再主题24角色；点“复制 → 复制HEX”粘贴Notes，核颜色来源区分，旧SCB1–104不是硬件槽总数。 |
| Monitor2 | 选本人进程看PID/gen属性，切CPU历史，Ctrl+Space冻结/继续；PIT采样不称FPS，无需终止用户进程。 |
| 文件安装GUI | 如Root纳入代表动作，仅D内将NOTE.TXT安装到新叶名，核逐项结果/权限；不扩为包管理。 |

## 当前控件树与真实命令

尺寸均逻辑像素；节点ID运行时分配，command不是Node ID。点击用当前窗口/节点已发布矩形或真实截图，不能把下表累加成固定屏幕点。

| 应用 | 控件树角色 | 当前已核命令/键 |
|---|---|---|
| Desktop | desktop→canvas；独立bar→task_canvas；图标格128×112 | Ctrl+Esc Start，Shift+F10上下文；Drop Copy/Move/Link为0x7220/21/22。 |
| Files2 | root VBOX：menubar26、toolbar34、地址row32、过滤/搜索row32、隐藏progress5、body(sidebar218→tree + splitter6 + table)、status26；七列名称/类型/字节/UID:GID/权限/修订/完整路径 | Ctrl+L ADDRESS=0x8115；Enter OPEN=0x7100；表内Ctrl+C/X/V=0x7102/03/04；Ctrl+D EDIT=0x810a，Ctrl+Shift+D DEBUG=0x810b；Drop Copy/Move=0x8116/17。新建目录0x7108已登记Ctrl+Shift+N，本流程可先菜单创建、再实际按键确认。 |
| Notes2 | menu24、toolbar32、status24、可隐藏search92、Edit填充；needle/replacement/caseless | OPEN=0x8101 Ctrl+O，SAVE=0x8102 Ctrl+S，SAVE_AS=0x8103 Ctrl+Shift+S；UNDO/REDO=0x8106/07 Ctrl+Z/Y；COPY/PASTE=0x8109/0a Ctrl+C/V。 |
| Studio2 | menu24/toolbar32/status24，隐藏search92；上区tree_panel230+split5+文档Tabs；下区out_panel220→输出/诊断Tabs | PROJECT_OPEN=0x8607无键；BUILD=0x8618 F5；CANCEL=0x8619 Esc；RUN=0x861a F6；DEBUG=0x861b F7。 |
| Debug2 | Target commandbox+Browse/Launch；Address addressbox+Code/Memory/Add BP/Remove BP；Code/Registers，Memory/Stack，Modules/Symbols/Breakpoints Tabs | LAUNCH=100 Ctrl+F5仅独立空闲窗口；CONTINUE=200 F5；PAUSE=201 F4；STEP=202 F8；BREAK_ADD=302 F9；BREAK_REMOVE=303 Ctrl+Delete；STOP=103 Shift+F5。 |
| Canvas/Lens | menu/toolbar，左工具，Canvas view，右properties/history dock | NEW100 Ctrl+N；SAVE_AS103 Ctrl+Shift+S；UNDO106 Ctrl+Z；REDO107 Ctrl+Y；Brush=200，普通b。 |
| Settings | 六页Tabs，显示页模式/缩放/预览/Keep/Revert | PREVIEW0x5200、KEEP0x5201、REVERT0x5202，点实按钮。 |
| Terminal | menu/toolbar→Tabs→真实terminal，status | NEW1000/COPY1003/PASTE1004；C/V由SCTERM实接Ctrl+Shift+C/V，新建/关闭Ctrl+Shift+N/W现已由Root补登记，待客体按键确认。普通Ctrl+C属于前台终端。 |
| Sound | menu/toolbar，封面/metadata，seek/音量，status | OPEN0x6900、PLAY0x6901、FORWARD0x6905、SEEK0x6906、CANCEL0x690a，点实控件。 |
| Session | login(username/Secret/password/signin/cancel/progress)，sessions(table/details) | SIGNIN100；REQUEST_SIGNIN101 Ctrl+Enter；LOCK108 Ctrl+Shift+L；CANCEL102 Esc。 |

**快捷键接线状态已更新**：本次只读准备发现的四处应用helper（Files2/systemtools含Sound/settings2/Terminal2）缺少CommandSet，Root现已对非零command且shortcut.key补入公开ScUiCommandSet，正在统一重生成受影响8应用SCO、原生清单及HOST导入。SCUI核心/ABI不改。当前不再列已知断线；声明的快捷键仍须本轮真实客体按键确认，不能将显示键帽或本次源码接线直接记为PASS。菜单点击主流程仍可直接执行；Notes/Studio/Debug2/Session原本就有CommandSet。

## 复用输入、截图与owner账

flow_input.py仅薄封装已存在ScuiInput，导入没有VM/连接/写盘副作用：

    from scui_input import ScuiInput
    inputs = ScuiInput(vm, guest, symbols, report['physical_input'].append)
    # 将本草稿目录加入sys.path后：
    from flow_input import FlowInput
    flow = FlowInput(inputs, harness)
    flow.keys('ctrl-l', 'ctrl-a')
    flow.type_ascii('/HOME/P6GUI')
    flow.keys('ret')
    flow.drag(source_point, destination_point)
    flow.screen('p6-files-desktop-drop')

point由当前画面或同源只读观察取得。helper不点击command ID，不写guest状态。右键仍为inputs.click(x,y,which='right')。type_ascii只处理非秘密US-ASCII路径/正文，不能用来录密码。已有keys.py支持HMP键名，但其末尾直接main，勿import；mouse.py为相对QMP输入，优先ScuiInput.move读mx/my校正。

m10a2_shell_inspect.inspect可复用当前精确layout.json+SCSHELL map+PID/gen只读桌面观察；旧头/偏移不匹配就不用。m10a2_shell_desktop_flow.run只覆盖旧桌面布局拖动/保存，含其旧fixture条件，不能不改条件直接当本轮Files跨窗Drop通过。

建议少量核心截图：Notes关联再开；真Drop接收/完成画面；ResearchDesk图表及Studio真构建输出；Debug2断点/单步；Canvas/Lens保存图。不拼画，不截专用Demo替代产品，不扩主题/DPI/逐应用VM矩阵。

进入本测试SID前沿现有代码取 before, observed_before=harness.quiet() 和 resources_before=harness.resources()。所有应用正常关闭并领完各自任务后，Root沿原流程恢复原桌面、注销本测试SID，再取after，复用harness.ledger(before,after,resources_before,harness.resources(),cold_font)。cold/warm资格沿现有审定值，不能把新残留擅自列为缓存。异常/权限/owner/页回收复用已有suite及当前pending账，不再新建故障矩阵。缺少实际结果的行为仍标未验证。

修订：2026-10-16，当前正式源码与既有HMP harness的只读准备，未执行。
