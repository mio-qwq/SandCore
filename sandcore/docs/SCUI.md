# SCUI 原生框架

所属版本：**SandCore M10a2 — The Application Platform Era**。完整范围和公开接口设计见 [SCUI-DESIGN.md](SCUI-DESIGN.md)。完整框架和OS协调正文已统一构建，实际客体G1生成158入口SCUI及应用；普通会话平台、官方3013个Unicode边界与双向结构体ABI已通过。**真实GUI流程仍在修复验收，SCUI和M10a2尚未验收通过。** 当前证据见 [原生平台检查点](../evidence/m10a2-ui-platform/README.md)；每项旧证据按实际源码闭包复用，不冒充新行为通过。

公开入口是完全大写 [SCUI.H](../user/SCUI.H)，158个正式接口由Ring3 `SCUI.SCL` ABI1.0/library_version1实现。DocumentBoundary与ContextRootNext是真实SDK入口。直接最低依赖为SC32API ABI1.2/library_version3、SCCLIB1.0/v1、SCLIB1.0/v1；ROOT通过既定有向依赖图引用。新增Font/Broker/Popup必须实际接线再置cap。应用没有寄存器/中断桥，没有复制NUI/库正文，没有Ring0权限。

[SCUI.C](../user/scui/SCUI.C)聚合同一职责正文，[SCUI.json](../user/scui/SCUI.json)仅列真实158公开定义和实际导入，内部ScUiFinalize是fini，helper不导出。SDK将自有大写头与标准小写头安装到正式/SYS/INC；仓库聚合构建提供user、user/sclib、user/stdlib三个include目录。

## 共同ABI与归属

所有公共callback是i386 cdecl；输出先size/version1，未知尾保留。ScUiContext是拥有引用的不透明对象，节点ID在本人context内单调不复用；query的布局/代数是值。内核仅保存真正window/session/input/clipboard/IPC对象，控件树、layout、provider/document、menu/command都在Ring3。

WidgetDesc/菜单help等可省略的Text全零表示空；有正文的Text必须16B/version1并为合法counted UTF8。UI标题保持系统127B/NUL/控制字符限制。CopyText/DocumentCopy/DialogSelectedPath返回拥有ScString，调用者Release；借用data只到相关修改/Release。Provider INDEX_OF的reply.total是同parent显示index，SelectionRange两端是stable ID且同parent；Tree跨parent范围由可见分段索引构造一次候选IDs。

创建使用SC32真正hidden CREATING→一次R3 CREATE→ACTIVATE；拒绝/失败out保持并撤候选。context_drop只在成功提交时转移，失败候选可一次DESTROY撤用户在CREATE取得的临时资源，但不drop仍归caller的context。候选ID可以有间隙，永远不复用。销毁先标退整个子树、撤ID与timer，再用无分配逆序工作链叶先Destroy；重入销祖先跳过已退役分支，retire_children依赖保证父通知与内容回收不会抢在子之前。正在callback的node/control/view延后释放，ID立即失效；节点订阅随节点退役，drop重入不释放外层借用的next。所有持久应用callback先PinCode；当前代码pin到进程退出，不因Disconnect伪称能立即卸库。

String/Value/Menu/列/Selection/Document/配置/目录都先备候选再发布，容量与算术溢出明确失败。后置通知失败记录notification_error，不能谎称已提交模型回滚；Combo选择通知或应用自行执行的跨对象回调副作用也不承诺回滚外部对象。

late fini静默，先内容后header两遍，不进入已释放的应用Destroy/drop；正常显式销毁仍调用用户生命周期。外部Model在quiet时只撤内部UI订阅，保留到SCLIB自身quiet fini统一回收，不能盲调用其用户drop。异常退出由OS owner/PTE/broker等兜底，不承诺用户析构执行。

ScUiContextRootNext(context,after,out)只沿本人Context的活原生root链枚举，包含独立Window、真实Dialog及Popup/Float；after0取链首，非零以hash核对仍活root再取root_next，O(1)，无分配或callback。OK写节点ID，END3或错误保留out，错误after明确STALE；迭代期间应用若销after，须重新0开始，不冻结跨回调拓扑。节点ID是值，不返回private指针、不扫历史ID或以count限制内容。

## 原生绘制与主题

字体仅通过正式SC32FONT查询原Phoenix12/16的真实mask/advance/baseline/bearing，像素随DPI重新栅格；SC32首版请求像素1..64，超出明确RANGE，不换字体或放大位图冒充新栅格。缺字沿同脸.notdef合同显式计数。颜色只用Theme24登记角色，Classic有真实凸凹边/default/focus/selected状态；Aurora共享布局和行为、按Theme.radius使用实际圆角span及同规则hit。没有额外裸RGB或字体替身。

surface只分配当前客户尺寸，尺寸候选完成才替换旧区；不会每app先留固定8MB。只绘可见dirty区域，child clip跳过无交集子树，一批真正ScWindowSubmit矩形→成功End；失败调用PaintCancel保留旧kernel canvas与损伤，有限deadline重试避免OOM busy loop。OS先保存invalidate事实，私有细分缓存OOM不让已提交正文永远停在旧画面。hidden停止绘图timer/提交但业务model可继续。

ABS/HBOX/VBOX/Dock/等分Grid与跨度/Flow/Overlay/Scroll布局measure/arrange全候选后发布，重入修改代数则保旧布局。Grid行数只扫描一次，比例用双字整数乘除。Tab top26、GroupBox的8/22/8/8逻辑像素是固有保留区，用户padding额外叠加而不覆盖它；child clip与客户区一致。LayoutDesc长度以96基准逻辑像素计，NodeInfo/measure/arrange/Painter为实际物理坐标。临时cell editor使用私有viewport placement，避免长表内容坐标与DPI取整破坏屏幕内编辑位置。

## 数据、菜单与输入

Tree用连续siblings范围分段，collapsed十万根项不创建十万控件或数组行；EnsureItem以完整候选展开祖先。flat表只查询viewport行，水平列有display索引与双字位置，也只遍历可见列。UPDATE保留树段索引并损伤实际变更行，结构通知才重建。cell缓存按item/field/revision/format失效；借用provider Icon在下一query前Retain，绘制后Release。

provider/data-cell同真实revision，异步sort保旧序并BUSY，Notify实际提交后刷新，箭头只表示已提交sort。列ID/field/稳定itemID分明，COMMIT_EDIT与FIND_PREFIX的DataReply.value是明确in/out候选，其余query纯输出。错误保旧cell与editor，跨行/滚动先尝试提交。F2使用当前或首个可编辑列。FORMAT_BYTES对U32字节数用ScFormatBytes，对BYTES用hex；HEX为32位十六进制，COUNTER为HHHHHHHH:LLLLLLLL双字形式，无假long long或浮点格式。

Model field0是label，>0是field%u，enabled是bool，icon为真实UTF8路径。INDEX_OF parent0查询真实parent及其显示index，指定parent须匹配。SelectionRange两端stable ID且同parent；Tree跨parent范围一次构造候选IDs。全选保存当时的stable-ID universe和exceptions，后来的INSERT不伪选中，SORT/MOVE保持身份，RESET重验。

Menu/Toolbar/Button/Shortcut共用Command。MenuItem拥有label/help/caption并引用icon/submenu，候选拒绝环和重复ID；助记符&与字面&&分明。真实borderless popup/noactivate由owner保持focus/capture，外点击关闭并消费，左右切menubar/子菜单，Esc关当前链，上下/Home/End跳separator/disabled，hover submenu用deadline。Toolbar capture后只在对应真实UP执行一次，键盘左右及Space/Enter保持通断。

COMMAND_QUERY有真实生产者：显式Query、MenuOpen及执行Command前刷新，目标为Context订阅node0。data借用ScUiCommandDesc，仅flags是in/out；label/icon/shortcut只读。查询保持label/icon引用，重入CommandSet/Remove优先新发布记录，同链不递归Query。paint热路径只读发布状态，避免每次绘制都进用户query。设备触发Command保留tick/epoch/sequence/message，普通调用为程序来源。

只消费真实64B ScMessage。KEYDOWN导航，TEXT插Unicode，KEYUP真实释放。BUTTON data[2]是当前buttons、data[3]是changed mask；仅真实left边沿开始/提交left动作。WHEEL data[2/3]竖/横，与按钮布局不同。祖先hide/disable也撤后代focus/capture/hover；loss不补UP。hover转目标先Arrow reset再设Editor/Splitter/列边形状，focus恢复与按下再次确认；非聚焦ACCESS不算设置成功，capture/drag期间不覆盖它们的cursor。

ScBrokerWait原子等GUI INPUT+broker STREAM；无活事务不10Hz轮询端点，活事务/timer给有限deadline。Timer用动态最小堆与节点拥有链，周期保持原deadline相位，TimerEvent.missed为真实双字计数，DRAW_ONLY按node effective visible及native root visible移出激活堆，后台timer继续。quantum是公平服务预算，不是对象数量上限。

ContextStep对正常控件操作失败保诊断并一次通知error_procedure，继续有界消息泵，是否退出由应用ContextStop决定；公共Dispatch仍返回精确错误。系统Get失败返回，不递归ERROR。CLOSE被veto或保存失败拒绝时显式清kernel closing，使其可再次关闭。

SC_UI_EVENT_VISIBILITY=37由真实SC32_MSG_VISIBILITY产生，只有native root的实际可见值改变才通知该root；先更新可见性缓存、绘图timer和输入/菜单资格，handler的公开query见新状态。event.message保留原64B消息及其visibility epoch，data借用原data[6]，不是可取消事件，handler返回值不回滚已发布状态。业务BACKGROUND timer可继续，DRAW_ONLY停止，通知本身不请求隐藏画面。沿已有root/context hold派发，重入销root安全，不向已退役root派发。

## 标准控制族、文档与协调

真实正文包含Button/Label/Checkbox/Radio/GroupBox、TextBox/Edit/Combo/Spin、Tab/Tree/List/Table、Status/Progress/Tooltip、Scroll/Splitter/Dock/Grid，以及同树Window/Panel/PropertyPage。MessageBox、FileDialog、PropertySheet创建真实modal owner窗口，目录分页以候选发布，过滤/选择/权限/overwrite及typed validate/apply不由薄标签代替。Dialog OK/CANCEL/APPLY为1/2/3，结果对象拥有路径/配置，完成后可查询再Release。

FileDialog和PropertySheet的未知COMMAND保持未处理，不关闭dialog；直接ScUiDialogClose须命中本dialog真实可完成按钮集合。非法command返回INVALID，按钮节点已失效返回STALE，隐藏或禁用返回CANCELLED。MessageBox保存实际动态(node,command)表，自定义command保持；重复command返回EXISTS，显式default/cancel须在集合中，default0取首个enabled按钮，cancel0优先2否则末项。权限/校验失败保modal和候选，不能因未知诊断快捷键完成dialog。

Document为严格UTF8 piece树与Undo/Redo，保留CRLF/LF/Tab/有效NUL，offset指原字节。编辑候选同时准备正文、索引、selection与Undo，ReplaceAll先确定匹配集；WRAP开启合法分段视觉换行，原硬行号保持，默认Edit水平滚动。Find ASCII_CASELESS只承诺ASCII折叠，不冒称完整Unicode大小写。

ScUiDocumentBoundary(document,byte_offset,kind,direction,out)复用真实piece/source分段，GRAPHEME1/WORD2、direction±1，失败不写out。Unicode15.1来自ROOT固定且校验的/SYS/UI/UAX15_1.SCU，供Studio导航/补全及公开API直接比较官方fixture，测试不复制私有算法。范围不扩到IME、任意字体或BiDi/复杂脚本整形。

Copy/Cut/Paste使用真实session clipboard事务及immutable snapshot，源退出可保留快照；Cut先备文档候选和成功Commit剪贴板再发布删除，readonly/权限/失败保正文与选区。Drag使用实际Drop Register/At/Validate/Connect/ticket及IPC，sender是OS身份副本，visible/enabled/WM资格变化真实登记或撤销。Offer只谈类型/动作，Transfer交字节；目标实际commit后ACK才产生COMPLETE，源收到真实Complete后才可Move/CUT删除。Send、超时或对端退出不算提交，取消/loss/ticket失效/fini关闭端点并恢复状态，不能以本地BSS模拟跨应用。

## 收齐与验证出口

核心所有者负责公共/私有头、aggregate/manifest、base/tree/dispatch/layout/paint/theme/menu/view-provider；控件所有者负责controls/edit/dialog/data与独立SCUIDEMO。冻结接口合作，不并写。158公共定义和private hooks已静态核对，实际imports及include/source容量随冻结摘要交主代理，真正构建仍须核实际符号/ABI与容量。独立示例、公开Boundary官方fixture、跨应用clipboard与ticket拖放/真实input/dialog/长表/Classic与Aurora和100/150/200%DPI/回收及必要静止和损伤性能对照由主代理统一执行，不扩大无改动旧矩阵；没有新证据不称性能达标或完整SCUI完成。

## 修订

2026-10-15：正式157入口、完整框架与控制/模型/文档/对话框/真实协调正文及manifest收齐；补重入退役、候选归属、混合等待、绘图输入与接口边界，状态SOURCE_ONLY，待统一构建与客体验证。

2026-10-15：统一ui-build-02实际在layout先调用、paint后定义的ui_rect_right/bottom产生implicit/static声明冲突；只在私有共同头补精确static前置原型，原日志保留，未自行重建，修复结果待主代理统一验证。

2026-10-15：为真实内部modal/覆盖确认及开发者root发现新增ScUiContextRootNext第158实体入口，O(1)活链值查询、END/STALE不改out；本批尚未发布，ABI1.0/v1保持，未自行构建或运行。

2026-10-15：同批补真实Dialog按钮命令校验合同；追加VISIBILITY=37业务通知以真实OS消息驱动隐藏后台事务，先发布状态、只通知实际变化、不新增入口或结构。仅源码静态收齐，交主代理统一构建及隐藏两样本/恢复重画验收。
