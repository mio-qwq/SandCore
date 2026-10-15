# SCUI 独立控件与工作流展示

所属版本：SandCore M10a2 — The Application Platform Era。

2026-10-15：四职责正文和独立 `SCUIDEMO.C/.json` 已收齐并进入真实定向验证。GUI01的端点ACL启动失败已窄修；build12仅ui_data/SCUI变化，refresh01用原实际G1生成656796B SCUI，137imports/158exports与owner账通过。GUI02安装五份native后平台/3013条文本与页账通过，Demo实际READY为PID4/gen6/SID6、72节点；整轮仍因宿主READY正则误转义及异步SYSTEM提示符前缀未匹配而FAIL，尚未执行GUI操作验收。**不得据此宣称SCUI或Phase4完成**。原失败/原盘/来源证据保留，GUI03仅改接收主控并按相同CORE/五native/App/manifest/源码依赖复用上述两组旧子结果，旧PID/页/时序不贴成新VM值。完整设计见[SCUI-DESIGN.md](SCUI-DESIGN.md)，文本边界资源见[SCUI-TEXT.md](SCUI-TEXT.md)。Root/core分别拥有内核协调、公共头、聚合清单与最终验收。

## 1. 实现分工与真实依赖

| 源 | 本切片职责 |
|---|---|
| `user/scui/ui_controls.c` | 独立基础/输入/导航/信息/布局控件状态机、默认measure/paint/input、Tab/Spin/Combo/Check/Radio/Progress/Status/Tooltip |
| `user/scui/ui_edit.c` | 拥有piece索引的Document、UTF8/grapheme/word边界、独立view选区/光标、分组Undo/Redo/savepoint、Find/ReplaceAll、真实会话剪贴板 |
| `user/scui/ui_dialog.c` | MessageBox、完整分页FileDialog、PropertySheet候选/validate/apply/cancel与完成重入 |
| `user/scui/ui_data.c` | Provider/ScModel adapter/共享Selection、Icon、Accessible、Scroll/Dock/布局保存恢复与授权拖放接入 |
| `tests/m10a2/SCUIDEMO.C/.json` | 只使用正式SDK与SCUI动态库的独立原生应用 |

核心代理独占context/node/event/focus/layout/painter/menu/timer以及 `ui_provider.c` 的View/Table/Tree/List虚拟化。表格/树/列表的控件默认paint/event调用同一backend钩子，不再造第二套字段或控件。平台所有者负责typed凤凰字体、PaintCancel、popup/cursor/wheel、palette和broker/drag-target协调；SCUI不包含SCAPI、软中断或内核结构。

Provider的field0映射ScModel的`label`，其它field映射`field%u`，`enabled`为BOOL；icon的UTF8资源路径实际加载拥有对象，不把U32强转指针。COMMIT_EDIT和FIND_PREFIX是DataReply.value的in/out特例：caller持有候选，wrapper先复制后调用provider；其它操作为纯输出。SORT flags为descending bit0与sort_kind bits8..9。SelectionRange两端都是稳定item ID，再依据provider顺序取范围。全选保存原ID集合的4B/项数组和取消例外，INSERT不会意外选中新项目；这笔数据内存明确计入应用成本，不建每行控件。

Unicode15.1属性只从统一 `/SYS/UI/UAX15_1.SCU` 读取，先验证完整头、连续闭区间、枚举、长度和CRC再提交。分段规则不复制第三方实现、不把ScUtf8Previous重命名冒充grapheme；原数据、完整许可证和生成器由主代理统一归档。

## 2. 原生布局与视觉

主窗请求1280×840物理客户像素，副窗请求500×600、x=1380；实际Query几何为准。旧900×600/360×280在真实150%截图中截掉Toolbar/Resources及副窗Exit，现为当前1920×1080桌面的完整200%字形与操作区留空间，副窗说明真正换行。布局描述采用96基准逻辑值，Menu24、Toolbar32、Status24、Tree192、Splitter4，剩余空间由实际布局分配。100/150/200%请求对应凤凰原生字号，不把整张客户图放大。Tab/GroupBox标题区由核心作为intrinsic保留，公共padding是额外留白。Aurora控件使用Theme.radius的真实整数圆角span，命中与绘制同一规则；Classic保留凸凹矩形。

六页保持内容优先、紧凑层次与足够留白：Controls、Data、Editor、Layout、Dialogs、Resources。颜色全部取已登记Theme24角色；Classic使用真实凸凹边、default/focus框和连接tab边，Aurora使用FACE/FACE_ALT/PAPER层次、薄LINE与克制阴影。禁用、错误、选择同时有文字/框/标记，不只靠颜色。

## 3. 完整展示流程

- Controls：Label关联TextBox、单/三态Check、Radio组、真正Combo下拉、Spin范围与非法输入、Button/default、确定/不确定Progress、Status与Tooltip；Tab/Shift+Tab和鼠标走同一命令/验证。
- Data：ScModel树、Combo列表和可排序五列表格；10万项provider按可见区查询，不建10万个Label。异步merge-sort每timer最多4096步，完整候选包含order与id→rank索引，rank也分批准备，发布前不替换旧顺序。INDEX_OF在初始顺序及排序后均为O(1)；排序进行中编辑明确BUSY，保留输入。
- Editor：同一Document的两个view，中文/ASCII/重音/组合序列/CRLF/Tab/长行；选择/插入/删除、Ctrl导航、Find/ReplaceAll、Undo/Redo回到savepoint以及密码边界；测量、绘制、上下移动、HitTest使用同一Phoenix布局。
- Layout：真实Dock/Grid/Flow/Splitter，DockFloat与内存Config布局保存恢复。当前Demo没有另写工作区管理器或自动重建已关闭panel；原root布局快照遇到被移动到其它root/已关闭的节点返回STALE，保留当前布局。
- Dialogs：共享标准MessageBox、FileDialog和PropertySheet；真正modal阻挡owner，Esc/默认动作与取消保持文档；目录完整分页、过滤、权限错误、覆盖确认、Apply成功与失败反馈。
- Resources：Phoenix Dense/Heading角色、自有Theme24图标、Classic/Aurora临时应用与F12真实诊断。Context/Document/Selection统计为公开query结果，不能把逻辑缓存或Undo收费字节冒称kernel PF数。隐藏停止客户绘制，恢复领新PAINT。

菜单File/Edit/View/Tools/Help与Toolbar共享Command定义。两个独立root共享同一Document和各自选区/滚动；副窗提供wrap view、真实拖放文本源与Exit。关闭最后root按context策略退出，Exit显式停止context。保存只在应用fwrite、fclose全部成功后MarkSaved；Save dialog不声称完成文件写入。FileDialog使用候选目录分页、UTF8 glob过滤、路径/文件名、创建目录、排序、多选和覆盖确认。PropertySheet schema支持必填/readonly/min/max，Combo候选读同section的`key.choices`分号表，缺省为初始项；Apply先完整Config候选，再validate/apply，成功更新取消保存点。

Combo弹出高度采用真实provider条数×当前Font行高，并先将可见行数截到半屏物理预算；数据数量不受此可见预算限制，更多行通过现View滚动。3项不再请求固定180逻辑像素而产生大块空白。原生popup客户矩形通过Overlay填满，避免其物理尺寸被子layout再次按DPI缩放；provider/selection在候选期间强引用，COUNT回调改变绑定则失败回滚。原字体没有的emoji/组合附标仍显示明确回退字形，文本边界通过不等于原字体含这些glyph。

Copy/Cut/Paste使用真实session broker。public ScClipboardOffer已有大正文Begin/Write/Finish事务，UI不额外削成64KiB：预算包含正文和格式元数据，Paste用64KiB读批。含有效NUL选区Copy/Cut按TEXT合同返回NOT_SUPPORTED且不删除；密码禁止Copy/Cut。Cut在clipboard commit前已准备正文、Undo和view holds，成功后无分配点提交删除。拖放通过真实target、144B票据源PID/gen核验、IPC分帧和实际UP handoff；副窗至主窗的文本Drop只有DocumentReplace成功才返回ACK，source只在真实ACK与ScDropComplete成功后发COMPLETE。Demo不执行文件MOVE或宣称其删除语义已验证。

## 4. 自动化入口和真实日志

客体编译入口：`sccc SCUIDEMO.C SCUIDEMO.SCX --dynamic SCUIDEMO.json`。应用不依赖额外mode，启动进入真实UI循环；F6..F11分别选择Controls/Data/Editor/Layout/Dialogs/Resources，F12为开发者Snapshot查询命令。Ctrl+N/O/S/Z/Y/C/X/V/A对应实际文档命令。HMP在选页、布局或主题变更后按F12，依据当前query坐标发送输入，不假定构造时声明矩形。副窗外框请求位置仅初值，实际位置以WINDOW行取证。

158项公共API中的`ScUiContextRootNext`沿真实root链枚举活窗口，包括FileDialog内部覆盖确认；Demo不扫描Node ID、不解释opaque Dialog。普通节点按公共children/nextSibling/parent链遍历。主循环保留`ScUiContextStep`的真实无限等待，在步间为新root登记高优先KEY_DOWN CAPTURE/TARGET观察者；模态窗口中的F12先消费真实按键再查询，Num/Caps锁状态不阻断此命令。观察者记录随root销毁清除，无固定root数量上限。

隐藏时F12没有输入路由，因此另用真实`SC_UI_EVENT_VISIBILITY=37`根通知启动窄业务观测。该事件在SCUI发布真实原生可见性、输入/菜单/拖放资格后投递，记录布局与158项API数不变。Demo先补真实新root观察，再查询Context：全部活root隐藏时安排BACKGROUND oneshot，延后2个PIT tick取sample1，再从该次真实单调时间加200 tick取sample2；两次只Query、打印并fflush，不绘画、不Invalidate。任一root复显立即取消未完成timer并重置；完成观察不继续周期计量，退出撤所有观察timer/listener。round区分复显中断后的新轮次，不配对不同round。

仅在`ContextStep`返回后观察会错过首次定时：当前Step消费最后隐藏消息后可能先进入无限BrokerWait。这里使用上述真实变化事件，在该步骤安装有限期限，再由库原等待机制唤醒；没有100Hz轮询、猜测FocusOut或人工输入。时间取`ScSystemMonotonic`的真实PIT双字ticks，UTC时钟不参与。

View菜单的100/150/200%命令为128/129/130。它们先`ScDisplayQuery`读取实际分辨率，再`ScDisplayApply`预览所选全局scale并成功后确认；权限或其它错误保留真实负状态，确认失败保留内核15秒安全回退。Metrics来自通知后框架的真实查询，不把菜单请求值当作实际字号。

| 日志 | 成立条件 |
|---|---|
| `SCUI READY roots=... nodes=... virtual=100000 font=.../... scale=...` | 创建/绑定/布局请求成功；后面立即Snapshot并fflush |
| `SCUI SNAPSHOT BEGIN count=N` / `SCUI SNAPSHOT END count=N` | F12或启动实际查询序号，BEGIN/END相同后fflush，不能作为行为PASS |
| `SCUI WINDOW role=... root=... window=... generation=... pid=... pidgen=... session=... x=... y=... w=... h=... cw=... ch=... state=... geometry=... visibility=...` | root取实际NodeInfo.root，native_window再真实ScWindowQuery；role为main/second，其它实际modal为dialog-ROOTID，其它root为root-ROOTID |
| `SCUI NODE role=... id=... root=... parent=... kind=... x=... y=... w=... h=... state=... textBytes=... textStatus=... textHex=...` | 实际NodeQuery客户坐标；固定控件role涵盖menu/toolbar/status/tabs/table/edit/check/spin/combo/shared-edit/drag-source/exit，其它root子树为dialog-NODEID/root-NODEID。AccessibleName为空才读NodeCopyText；UTF8原字节小写hex，无正文为`-`，错误记录textStatus |
| `SCUI METRICS role=... root=... scalePercent=... fontFace=... fontPixels=... fontEpoch=... themeEpoch=... displayEpoch=... rowHeight=... menuHeight=... toolbarHeight=... statusHeight=...` | 实际ScUiMetricsQuery，包含所有已枚举root |
| `SCUI THEME style=... generation=... radius=... border=... titleHeight=... taskbarHeight=...` | 实际ScThemeQuery；style按公共Classic/Aurora枚举 |
| `SCUI DISPLAY action=snapshot scalePercent=... w=... h=... backend=... generation=... previewSeconds=...` | 实际ScDisplayQuery；DPI命令另记录query/preview/confirm/observed及真实status |
| `SCUI EDIT_SELECTION role=edit/shared-edit id=... root=... anchor=... caret=... affinity=... revision=... flags=...` | 两个view分别实际ScUiEditGetSelection，不能用文档全局选择替代 |
| `SCUI DOCUMENT ...` / `SELECTION ...` / `CONTEXT ...` | 实际公开query，记录当前文本代数/保存点/选区/绘制和内存计数 |
| `SCUI COMMAND id=... result=...` | 真正共享命令执行结果；F6..F11为121..126，F12为127，DPI菜单为128..130；modal观察F12直接Snapshot，不伪造COMMAND |
| `SCUI SORT rows=100000 revision=...` | merge与rank全部准备后一次发布，非启动排序即宣布结束 |
| `SCUI DIALOG root=... stage=... command=... selected=... flags=... error=...` | Snapshot实际ScUiDialogQuery；完成回调另记录stage/command/selected/error，validate/apply另有实际回调日志 |
| `SCUI QUERY_ERROR kind=... root/id/after=... status=...` | 活root/metrics/树遍历实际查询失败，保留失败不虚构坐标 |
| `SCUI HIDDEN sample=1/2 round=... tick=... tickHi=... visible=0 paints=hi:lo submitted=hi:lo` | 同round两次真实ContextQuery与Monotonic；sample2时间至少比sample1晚200 PIT，visible均为0。主控核两个真实双字计数相等，日志自身不宣布PASS；复显另观察真实重画 |
| `SCUI DROP bytes=... sender=pid/gen` / `SCUI DRAG COMPLETE action=... sender=pid/gen` | target正文提交/source真实ACK；传输失败不写成功日志 |
| `SCUI SAVED bytes=... path=...` | 文件写入和关闭完成后保存点更新 |
| `SCUI ERROR op=... status=... cause=... domain=... raw=...` | 实际失败，不能从覆盖统计中删除 |
| `SCUI EXIT commands=... changes=... drops=... failures=... status=... code=...` | UI循环结束和显式资源清理；无失败正常返回0，发生未预期错误返回1 |

正文使用持久AVL piece树与最多4096B的拥有chunk；seek累计字节/CRLF行数，不每键重拷全文。各view有稀疏Phoenix行度量与字素wrap行索引，局部改变撤后继缓存；Find使用线性KMP，ReplaceAll先定原匹配集后一次事务。Undo保存内容身份、piece和各view前后选区；收费字节为本事务真实chunk/path分配与保留删除段的保守上界，超预算按整事务淘汰，实际进程页仍从OS另行统计。完整Unicode15.1官方边界oracle由Root统一测试 `ScUiDocumentBoundary`，本文件不伪造其通过。

guest02已由主代理实际完成平台案例和页账、GCC SCUI库的官方3013条边界oracle，G1与SC32原生编译亦有成功子证据；整轮仍因G1→SCUI失败而FAIL。宿主GCC接受C99复合字面量，不证明本阶段SCCC profile支持它。本次三正文移除全部24处该语法，数组index++/used++/n++仍各一次、矩形各分支与16字段ScDragEnvelope顺序保留；不增加编译器特性、不修改导入导出或降低警告门槛，原生修复尚待下一轮实际编译。

## 5. 生命周期、失败和待验证证据

持久callback/drop/provider先分配候选、PinCallback再提交；Node与opaque对象有独立depth/retired保护，回调销自己/owner不再访问已退役payload。显式释放正常执行拥有drop；late SCUI fini静默，不在应用或模型结束后重入用户callback。Model adapter撤自身内部订阅后，quiet时把外部Model引用留待SCLIB自己的quiet fini统一回收。

Dialog只处理自己的命令：FileDialog为内部导航/建目录/过滤/排序及OK/CANCEL，PropertySheet为OK/CANCEL/APPLY；未知全局COMMAND返回未处理，不关闭窗口。`ScUiDialogClose`仅接受对应合法集合，未知值返回INVALID并保持dialog。MessageBox保存真实按钮node与自定义command，合法命令必须属于该集合；打开期间节点已销毁/移出root返回STALE，隐藏或禁用返回CANCELLED。未指定default选首个启用按钮，未指定cancel优先命令2，否则取最后按钮；显式default/cancel须属于实际集合，重复按钮command返回EXISTS。完成后的合法重复Close保持幂等；最后Release仍正常撤销资源。

后续只组织一个有序真实Windows QEMU工作流：宿主bootstrap与客体SCCC生成字节分别标来源，再运行那个实际生成的SCUI/Demo。截图、真实键鼠、modal/clipboard/drag权限、隐藏paint计数、原尺寸/主题/缩放与应用/用户库/kernel资源分别留证。旧P0、字体和Phase3未改部分直接复用；没有数据不称性能达标，没有客体执行不称实现已验收。

## 修订

2026-10-15：收齐四职责正文、157入口中的DocumentBoundary、双root/shared Document Demo、异步order/rank、F6..F12真实输入与日志协议；更正public大ClipboardOffer能力，记录明确成本和布局快照边界。交主代理统一构建/运行，尚无本批客体PASS。

2026-10-15：依据ui-build-03完整真实日志，文档复制核对NULL/零长span并释放失败候选；拖放源单调时钟失败原样传播，不制造零时钟；Demo清单删除mkdscx证实未引用的`__scc_errno_location`。未降Werror、未自行重建或运行，交Root下一轮验证。

2026-10-15：依据真实GUI观测缺口追加158项API的RootNext诊断、modal F12观察、活Dialog子树UTF8 textHex、真实Metrics/Theme/Display与两view选区；View菜单加入全局100/150/200%请求/确认。修复Dialog未知COMMAND误关闭并核验MessageBox实际自定义按钮集合。本次增量静态收齐，交Root统一build05与定向运行，未自行构建或VM。

2026-10-15：依据guest02的G1→SCUI实际语法错误移除controls/edit/data共24处C99复合字面量，保字段、自增与功能；依据已授权真实VISIBILITY37加入2/200PIT两次隐藏业务样本，复显/退出撤计时，不绘画不轮询。平台/3013条GCC文本边界/G1及SC32原生子证据与全轮FAIL分列；交Root build06，未自行构建或VM。

2026-10-15：保留实际GUI01启动失败/VM终态。Drop端点改用默认ACL，严格绑定本人SID/subject UID/GID/realm，未放开PUBLIC或跨会话；Create/Listen及该新端点候选失败在清理前保存UI cause/domain/raw，已映射注册失败不二次覆盖cause，零endpoint不Close。仅ui_data.c本路径和本文更新；其它源码/导入导出/ABI保持，交Root仅重新客体生成SCUI并复用未变证据，未自行构建/VM。

2026-10-15：refresh01实际原G1重新生成SCUI通过，新库SHA5de54249fea6cbdf65d4a083e2a254045585706193833e611b6cfe224e635950；GUI02五native平台/文本及owner账通过、实际Startup成功，但接收正则未匹配READY，原整体FAIL和截图保留。修宿主两接收模式，验证能匹配原实际stdout；新增仅两组native子结果的严格复用分支，host工具差与真实运行依赖分开，GUI行为尚待实际完成，不重编库或重跑旧整批。

2026-10-15：GUI03实际READY已接收，初始公开快照触发宿主NODE数值kind与记录类型kind重名，原FAIL/普通PID4代数2/SID4及截图保留。快照records改存node_kind，nodes仍保真实kind；用本轮实际stdout完整解析核对，仅宿主接收/解析工具摘要可独立变化，库、App、SDK、CORE与旧两组native执行依赖继续逐项相同。下一轮只继续GUI行为，未把启动或解析修复称验收通过。

2026-10-15：GUI04真实UID/GID、SESSIONPAGE、PROCESSPAGE和两窗PID6代数2/SID4一致；身份主控在READY前截掉UID行末换行而误报，原FAIL保留。修接收切片并以该完整stdout一次复算切换桌面前全部断言；相邻DROP/COMPLETE、EXIT/子码的接收起点保留同一个换行，仍要求真实后续标记。只三份宿主GUI输入/解析/工作流工具可与旧native平台/文本证明分开变化，CORE/SDK/App/五库完整闭包仍严格核对；未新增客体组合。

2026-10-15：GUI05真实普通PID6代数4/SID4、五native字节及桌面切换通过；首张可见截图证实F12被WM旧全局登录快捷键接管，启动Login/ses覆盖SCUI，应用未收到诊断键而超时。保留本轮FAIL、stdout和截图；将真实现代焦点路由冲突交Root协调，尚无本轮GUI功能或完整画质PASS，不改称库启动失败。

2026-10-15：Root保存main恢复点0d78cd4并统一build13通过，仅kernel/wm.c和四Core产物变化，现代有资格焦点F12进入raw、旧窗口入口保留。GUI主控新增固定`--core-route-only-reuse`：逐项核432输入/17输出的同集合及精确单WM/四Core差，核归档build12的Core/ELF/SYM和128B头后旧payload；生成与历史平台/文本执行保原Core来源，本轮新Core另记真实GUI。当前五库、G1、三App、SDK、启动字节和全部include闭包的只读preflight通过，未重生成库；GUI行为仍待续验。

2026-10-15：GUI06实际普通PID4代数3/SID4、现代F12及Checkbox输入通过，三张原尺寸截图均已检查；全轮仍FAIL，保留真实布局裁剪、Combo空白和F7不切页证据。只读原native机器码确认Demo的switch default未进入分派链，Root已定位并窄修Compiler reserved default标签解析，未在Demo绕过。收齐控件真实Count/行高/屏幕预算与物理Overlay、Demo完整viewport/说明换行；导入导出和公共ABI未改。下一统一构建及实际新G1/受影响库/App来源待验证，本次源码改动不继承旧GUI PASS。

### 实际后续故障与本批窄修

GUI12 已真实运行当前 G1 的 SCUI 和 Demo，普通 PID4/gen3、SID4。100000项全选、完整正文输入/Undo/Redo/剪贴板、真实两窗 ticket/IPC DROP/COMPLETE、属性校验/Apply、文件取消/覆盖保存/权限错误、两主题、DPI 数值、resize还原与局部损伤均留下实际证据；整轮仍 FAIL，没有正常退出或最终 owner 收束证据。200%实际图存在工具栏末项和Resources文案裁切，不能以字号数值正确替代视觉通过。

当前框架在 Toolbar/MenuBar 空间不足时保留Chevron，将隐藏原Menu范围显示在真实Popup中；保留原菜单引用、命令、disabled/checked、图标、快捷键与submenu，未复制用户context/drop。鼠标边沿及键盘方向/Space/Enter可以访问该范围，滚轮/Home/End不越回可见前缀。`UiPopup.range_first`仅是本库私有范围下界，公共158入口及ABI保持。

Label的自然宽度保持原语义，高度按实际分配宽度进行换行测量；布局在同一候选内先得到Splitter/Dock/padding后的宽度，再自底向上更新高度及VBox flex余量，最终才发布。custom arrange只调用最终一遍，回调修改revision或失败仍整树回滚。Demo Resources首行显式声明 `SC_UI_WRAP`，窗口尺寸与命令不削减；因此须真正重新生成Demo，不能只换新SCUI后继续使用旧Demo字节。

业务printf保持SCCLIB正常管道全缓冲。宿主先保本次业务输入前的绝对console起点，再用已有有限真实F12的正常fflush观察DROP/COMPLETE、COMMAND、SAVED和APPLY；不会重发业务操作或把旧次标记当新结果。Menu/Property截图等待真实Window的PAINT标记清除，不再以固定0.3秒猜首帧。

GUI12原console同时包含隐藏sample1 tick27323和sample2 tick27523，visible均0、paints均0:320、submitted均0:450029368。第二行带真实SYSTEM提示符前缀；旧finditer还会消耗相邻行共享的换行，导致遗漏。新解析使用真正行首锚及可选提示符，已在原字节上读出两条、差200 PIT；不修改原FAIL报告、不修改定时器实现、不在隐藏时发送F12。恢复重画与退出回收仍待后续真实运行。

2026-10-15：收齐GUI12实际高DPI裁切对应的框架overflow、两遍候选换行布局与Demo单行WRAP，修正真实日志/隐藏行解析和首Paint截图前置条件；源码仅静态审阅，新库及新Demo客体字节、视觉/恢复/退出验收仍未验证。

2026-10-16：build23/refresh09实际补齐node0上下文CAPTURE/BUBBLE，并由原G1生成SCUI及Demo；GUI21真实Drop/ACK已成功但主控行首边界误超时，原失败保留。只借紧邻start前的一个换行修正主控，实际原日志回归及旧标记拒绝通过。GUI22最终DECLARED_NATIVE_FIVE_LIBRARY_ORDINARY_GUI_SECOND_ROUND_PASS，共91项，GUI_behavior_verified/source_disk_unchanged/current_inputs_unchanged均true；两主题、100/150/200%实际字宽/换行、溢出菜单、文件保存/错误、Drop提交及Complete、静止/隐藏零提交、窗口恢复、正常return与最终owner/PF归账通过。主代理已查看Classic与200%原尺寸截图。公开158入口/137导入未变，SCAPI原SHA保持。静止200PIT实际2.032秒、QEMU一核CPU0.171875秒；checkbox观察127PIT包含F12/串口，不冒充纯输入延迟。证据evidence/m10a2-p4-context-and-p5-generation/index.json，不代表整版完成。
