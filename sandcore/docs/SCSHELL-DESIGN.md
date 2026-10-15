# SCSHELL 与桌面重制设计

> 2026-10-15后续：本文件下方“尚未实现/没有正式头”的表述是初始设计审计快照。现已有隔离的真实host/WM、FS lease freeze2、SCSHELL库/worker及桌面正文，见[SCSHELL-IMPLEMENTATION.md](SCSHELL-IMPLEMENTATION.md)最新段；尚未正式整合、构建或运行，原完整合同与验收门槛不变。

所属版本：**SandCore M10a2 — The Application Platform Era**。2026-10-15，Phase 5 的正式设计与现状审计。完整目标来自根 `M10a2RULE.MD` 第五、六、七、八至十三节；本文件没有修改这些目标。**SCSHELL 的库、服务和新桌面尚未实现，本次没有构建或运行。** 当前 SCUI 平台的局部证据与待验范围见 [SCSHELL-IMPLEMENTATION.md](SCSHELL-IMPLEMENTATION.md)。

## 1. 用户要得到的完整工作流

桌面、Files 2 和第三方程序共享文件身份、关联、菜单、对话框及文件操作规则。菜单的命令状态与执行走同一模型，不能由几个固定按钮分别猜权限。文件的完整路径是身份，省略文字只用于显示。

| 工作流 | 用户可见结果 | 必须成立的边界 |
|---|---|---|
| 登录、切换、注销 | 各登录会话有自己的桌面、选择、菜单、任务栏和剪贴板；切回恢复 | 同 UID 的两次登录仍是不同 SID；隐藏桌面不绘制，后台工作继续 |
| 选择与打开 | 单选、Ctrl 切换、Shift 范围、空白框选、键盘导航、双击或 Enter 打开 | 点击只选择；按真实输入边沿执行一次，不以丢捕获伪造 UP |
| 安排图标 | 自由拖动、多图标保持相对位置、网格吸附、自动排列；重启恢复 | 移动图标位置不移动其目标文件；失败或取消保留原布局 |
| 图标与快捷方式 | 改显示名称、改目标/参数/图标、移除快捷方式、查看属性 | “移除快捷方式”不删除目标；完整路径不截断；旧三行 LNK 可读 |
| 开始菜单与任务栏 | 多级菜单、可配置程序入口、窗口切换/恢复/最小化/关闭、排列窗口 | 动态模型容纳全部获准对象；只操作同会话获准窗口 |
| 文件打开与打开方式 | 默认关联、显式 Open With、未知类型选择程序；图标一致 | 注册不授予执行或读取权；普通 SCX 与动态 SCX 都能启动 |
| 文本剪贴板 | 不同应用 Copy/Cut/Paste，源退出后已提交内容仍可读 | 真实 session clipboard；Cut 先准备删除候选，剪贴板提交成功后才删除 |
| 文件剪贴板 | Copy/Cut/Paste，显示逐项进度、冲突与失败原因 | FILES 是意图，不是文件权；失败不能丢源文件或清掉别人新剪贴板 |
| 跨窗口拖放 | Files 内部、Files↔Desktop、合法第三方目标，预览/目标反馈/取消 | 真实 Drop 票与 sender；Send 或 Complete 调用不等于持久化成功 |
| 文件修改 | 新建、重命名、复制、移动、删除、属性、权限与冲突处理 | 按项提交；条件核对身份/版本；IO 不明时不承诺回滚或删源 |
| Shell 对话框 | 标准文件/目录选择、属性、确认、错误、覆盖冲突和进度 | 使用 SCUI 的真实控件/模态/结果对象，不复制 NUI 私有实现 |
| 设置与恢复 | 主题、壁纸、显示预览、菜单/关联/快捷方式、布局设置可保存 | 坏候选保留有效快照；保留 15 秒显示 Keep/Revert 与 VGA 回退 |

Files 2 的树、详细表格、表头排序、列宽、搜索/过滤、地址/工具/状态栏属于 Phase 6 的应用正文；Phase 5 必须提供其所需的完整 Shell 服务，不能用旧 Files 单选列表称为 Files 2。Studio、Debug、Notes、Canvas、Lens、Settings、Monitor、Calculator、终端及安装器的既有功能同样保留，随后逐项迁移。

## 2. 当前源码与必须保留的行为

| 现有入口 | 当前真实实现 | 重制时的保留与缺项 |
|---|---|---|
| `kernel/desktop.c/.h` | 每 SID 动态状态；菜单 `menu_items[32]`、图标 `icon_items[24]`、图片缓存 32 项；严格候选解析和配置重载 | 保留菜单/快捷方式、权限读取、图标回退、有效资源快照；新活动模型不能沿用 32/24 数量门槛 |
| `kernel/wm.c`、`wm_native.inc` | 动态会话 z 序、窗口和任务栏滚动；内核绘制桌面图标、Start 和上下文菜单 | 窗口池与同 SID 路由复用；桌面业务/布局/菜单迁到 Ring3；旧窗口 ABI 与关闭语义保留 |
| `wm_native.inc` 图标交互 | 按索引网格、分页，当前高分辨率图标按下直接执行；没有持久位置、多选/框选 | 新默认采用选择与双击；旧程序兼容不是强迫新桌面保留单击即执行 |
| `user/files.c` | NUI、单选、最多 512 行；导航、分页、打开/编辑/调试、新目录/改名/删除、七项私有菜单 | 完整路径、取消保原选择、UTF-8 显示、紧凑布局、实际错误必须保留；新表格动态化并共享 SCSHELL |
| Files 分派 | `.SCX` 执行/调试，源码/文本送 IDE，图片送 Lens；扩展名由应用写死 | 导入为关联种子；解码能力以真实 Lens 源码和证据为准；未知二进制不能当文本保存 |
| `user/settings.c` 与 CONFIG 合同 | 六页草稿/文本编辑；菜单/LNK/主题/壁纸、显示预览、坏候选保原 | 新设置继续保留全部入口；不得把 Shell 注册变成只有 UI 无真实持久保存 |
| `user/shell.c`、历史 `shell.asm` | 旧图形 Shell、提示符守卫、长行、异步 run、CLI 等待/输出 | 保留历史源码/SCX及旧异步行为；不以桌面服务取代命令解释器 |
| `user/m9/sh.c`、`CLI-M9.md` | 现无窗口字节流 Shell；变量、引号、管道/重定向、控制流、函数、脚本、作业、stdin profile | 保留已支持语法、返回码、真实端点继承、动态后台表和串口开发；SCSHELL 不重新解析这些命令 |
| `assets/desk`、`assets/m9/desk`、`tools/gen_m8_assets.py` | 三行 LNK、SMENU1MIO、旧 SCF/SCB 与主题 `@` 图标 | 只读导入现存数据；用户原配置与图标不批量覆盖，不复制大盘作归档 |

配置原语义保留：菜单候选 UTF-8、8191B/32 条旧格式；LNK 511B 内的 label/command/icon 三行，31/127/63B。这些是**旧格式**的约束，新注册和布局不能把它们当全桌面上限。新格式另设版本、严格算术/字符串校验及明确资源预算。

壁纸不能退化为纯色替身。当前 `desktop.c` 按会话权限分批读图、隐藏停止 IO、候选完整后替换、透明度一次合成；cover/中心裁剪、模式缓存、原 SCB 与压缩图片服务回退均有实现。迁移要保留用户原壁纸、同源资源、原生尺寸、Aurora/Classic/VGA，并在切换时避免内核与用户态各永久持一套重复大背景。旧 IMAGE 服务目前没有完整现代公开桥，SCUI 的 `ScUiIconLoad` 只解析 SCF1/SCB1/SCB2 图标；它不能直接冒充 PNG/JPEG/WebP 壁纸解码服务。

## 3. 真正的 Ring3 分工与有向依赖

正式产物分为三个职责，不向 SCL 授权 Ring0：

1. **`SCSHELL.SCL`**：公开 `SCSHELL.H`，实现客户端协议、关联/图标/菜单、Shell 对话框、剪贴板/拖放协调、文件操作状态机。库实例位于调用者地址空间，以调用者真实凭据调用 SC32，不通过系统进程替调用者读写文件。
2. **`SCSHELL.SCX`**：每普通登录 SID 一个真正动态应用，兼任桌面/任务栏 host 与会话服务。保存活动关联、菜单、布局、应用身份及获准窗口模型；SCUI 绘制和输入均在 Ring3。文件复制等长 IO 由 caller-owned worker 完成，服务消息泵不等整文件。
3. **`SCFILEWORK.SCX`**：每个被接受的文件作业按需启动的 Ring3 worker，继承发起者真实 UID/GID/realm/SID；动态工作链、64KiB 传输缓冲、实际句柄/lease及逐项结果归该 worker。作业数量受资源限制，不以固定槽替代任务上限。

拟服务程序位于受保护的正式发行路径 `SYS/SHELL/`；这是**待新增的受保护路径和可信启动接线**，现有 `/SYS/LIB` 保护不能自动证明该目录已经保护。ROOT 的 `scl_platform.c:system_library` 已包含 SCSHELL/SCGFX/SCRES 系统名；SCSHELL.SCL 仍固定从 `/SYS/LIB` 解析，不从应用目录取同名库。

当前最低基础为 ROOT（正式库名 `SCLDR.SCL`）ABI1.1/v2、SC32API ABI1.2/v3、SCCLIB ABI1.0/v1、SCLIB ABI1.0/v1；SCUI 的当前源码含 ContextRootNext，正式清单已有 158 入口，先前 build04 是 157 入口。SCSHELL 最终 manifest 必须按实现时真正所用入口和新增 SC32 minor/version列依赖/导入，不能沿用本表假称新增能力已存在。

箭头表示“左侧模块调用/依赖右侧模块”，与真实 manifest 方向一致：SC32API→SCLDR；SCCLIB→SC32API/SCLDR；SCLIB→SCCLIB/SC32API；SCUI→SC32API/SCCLIB/SCLIB。SCSHELL 依赖这些基础与 SCUI。未来 SCRES 提供资源/图标，SCGFX 提供图像/2D，二者只能依赖较低层，不依赖 SCSHELL；SCSHELL 可使用二者，SCUI 基础不反向依赖 SCSHELL。文件对话框和 Document 基础仍归 SCUI，SCSHELL 增加关联、文件事务和默认 Shell 行为，避免循环。

SCSHELL init只准备本实例空容器/内部状态，不在loader busy期间注册应用callback、创建桌面角色或连接service；这些在真实main/公开调用阶段完成。持久provider注册仍遵守ROOT PinCallback资格。fini先标quiet、撤本实例订阅/连接、取消尚归本实例的managed候选并释放菜单/资源/配置，不能再调用已结束应用的第三方callback，也不能销毁其它进程已提交对象或把文件失败改成成功。显式Destroy仍提供正常通知；异常退出最终由kernel owner/lease/PTE清理，不承诺用户fini执行。manifest init/fini只能列实际定义，静态helper不导出。

## 4. 必须新增的最小内核合同

下列为 Phase 5 必须的设计合同，**尚无正式头/selector/实现**。实现前由主代理统一冻结新的 SC32 版本化公开记录、私有 wire、cap 位及出口。不能在 SCAPI.H 或旧调用忽略寄存器里塞参数。

### 4.1 同 SID host、可信发现与工作区

内核在真实普通会话建立后，从固定受保护发行映像启动服务，并将 `(SID, session_generation, PID, PID_generation, endpoint)` 绑定为唯一 desktop host。角色来自可信启动事务；普通程序的窗口、uid=0、uid=-1、文件名、普通 IPC 名字或自报身份都不能任命自己。

角色没有文件提权、跨 SID 控制、SYSTEM namespace 注册或硬件管理权。身份仍为该登录会话真实普通主体；SYSTEM 管理桌面沿原外部授权链和隐藏边界，不由普通 SCSHELL 接管。`ScShellConnect` 经新增 typed discovery取得内核绑定的真实 endpoint/identity，然后沿 Broker 原 ACL/引用/队列连接。普通 SESSION 名称可以被占用，故不得仅按字符串连接“shell”便相信对端。

host 准备 desktop 底层与 taskbar 专层两个实际本人窗口，内核只管理层级、输入、可见性、geometry/visibility epoch和工作区保留，不调用用户控件过程。准备成功、首帧完整提交后原子激活 role；到这时才关闭该 SID 的旧图标/Start/taskbar业务路径。正常窗口、菜单、tooltip与模态仍按 WM 真实排序，desktop 层不能盖住普通窗口，taskbar 不能越过锁屏/模态边界。

工作区保留归 role PID/gen/SID：高度变更先验证完整输出和候选几何、内存成功后提交 epoch；失败保留原工作区。host 退出/异常/注销沿 owner 链撤 endpoint、host surfaces、保留区与订阅，恢复有限功能的旧桌面回退，然后由可信 session 启动器按有限重启策略恢复服务；不授权某普通进程抢占角色。注销不重启。回退只保可用性，不算新桌面完成。

新增 typed 窗口分页可复用旧 WINDOWPAGE 的过滤事实：只输出获准同 SID 窗口的 ID/gen、PID/gen、可见/最小化/geometry、标题值和应用身份值，无 canvas/procedure/内核指针。每批有预算，空过滤页不是结束，epoch变化允许重新同步；订阅事件丢失后重新取全快照。服务不能向 kernel 保存第三方 R3 指针。

跨窗动作只允许真实 host 对同 SID、仍活、gen一致且 WM 允许的窗口发 focus/restore/minimize/maximize/place/close-request。尊重 disabled、modal、style和应用 CLOSE veto；“关闭”不是 terminate。排列以实际工作区和最小客户尺寸作候选，失败返回逐窗事实，不宣称多窗原子回滚。其它普通应用的 ScWindowControl 继续只能本人窗口。

GUI 应用启动还须新增明确的 session-lifetime launch：继承发起者凭据/环境/CWD，应用可在发起窗口或 Shell 服务重启后继续；无隐藏 writer引用。现 ScProcessSpawn 是受管作业，只有 NO_STREAM_INHERIT 位，不能把它称为已支持 detached GUI。worker继续受管，取消/等待/退出码沿原真实 job 合同。

### 4.2 文件 lease、条件发布与条件删除

当前 ScPathCreate 已桥接 CREATEEX，原子无覆盖建名并在发布前设置 mode，必须复用。当前 modern open-pin 保护名称免旧 replace/remove/rename，但不禁止另一 modern writer；ScPathRename 不覆盖已存在目标，活 pin 又使其 BUSY。因此仅用当前接口 close→rename/remove，不能实现无竞态覆盖或 MOVE。

新增对象均动态分配、owner PID/gen绑定、单调编号不复用，所有输入/输出先完整 RO/RW 预验；路径只 resolve 一次，内部仅规范 rootless身份。最小合同如下：

| 待新增能力 | 输入前置条件与效果 | 生命周期/失败 |
|---|---|---|
| 原子 exclusive-create-and-pin | 规范目标名、kind/mode、完整本人输出；名字不存在时建独立对象并直接返回 owner句柄/lease | 不得在 path-create 与 open 之间被另一任务换成同名对象；OOM/拒绝无发布/无泄漏 |
| READ_STABLE lease | 本人实际 file handle、object_id、当前 revision、规范名；阻止内容写/截断/替换/改名/删除 | 读取不借永久旧权限；各 IO 重验权限；chmod/注销/撤权不被 lease屏蔽；错误或取消释放 |
| WRITE_EXCLUSIVE / NAMESPACE lease | 锁定本人候选及预期目标名称/对象，排斥冲突修改；目录树操作锁定相关命名空间 | 新旧 writer/事务和祖先改名均不能绕过；按规范名排序 try-acquire，失败撤本次全候选，避免持锁等待环 |
| 条件 publish/rename | caller候选handle+lease、源当前规范名/object/revision，目标预期“不存在”或 exact object/revision，必要父目录权限 | 持本人 pin完成元数据候选→提交，不通过 close制造空窗；已有 foreign pin/事务明确 BUSY，不偷换其对象 |
| 条件 remove | callerlease与当前规范名/object/revision完全一致、当前删除权限仍合法；目录还须真空与有效目录身份 | 同名重建/被改正文/权限撤回/外来 ACK不能删；失败保源，确认提交后才回收extent |
| lease release/abort/query | 只接受本人资源种类/owner/gen；query给当前状态/已知提交结果 | owner退出分步撤本人链；清理候选不回滚已发布文件，不全扫描所有句柄 |

权限应在每批推进及最终发布再次以真实原 owner 检查。仅“读稳定”不赋予删除权，只有复制目的地写权也不赋予源父目录写权。若权限已撤，取消未发布候选，不能让 background poll借PID0续写。

目录R负责遍历/列举，目录W负责建立/删除/改名，文件W负责内容；属性修改沿真实owner/root规则，不把GUI“可编辑”当授权。普通root及普通Ring3 SYSTEM仍不能经普通文件API改CORE/AUTH/RECOVERY/LIB；MOD继续仅外部串口授权链。祖先移动/删除、rename目标与worker批次均走相同保护，不通过shortcut、drop或服务代办绕过。用户名/家目录显示配置不改变真实UID/GID/realm。

发布沿现 v5目录双 bank/CRC/最后超级块，不加持久 birth-ID字段；namespace候选和旧extent在确定提交之前均保留。metadata写每步16扇区，增长/零洞/迁移沿现128扇区预算，IO不明进入现 faulted/uncertain。提交前的明确错误可释放候选并保原目录；超级块/原扇区结果不明不能声称回滚，拒绝继续修改且不给 durable ACK。

目录身份用当前元数据revision及 owner namespace lease，不伪装成持久 inode。目录revision本身不能证明子项未变，取得lease时还须在同一临界步骤核目录视图epoch并固定当时子树成员；同lease的分页枚举以已固定成员推进，不能因无关文件更新全卷epoch而拼入新成员。目录树 lease必须阻止受锁子树的命名/内容冲突，并允许无关路径与 WM推进；递归计划/冲突发现只一次遍历需要的实际条目，执行按有界批次，不每字节全目录扫描。v5真实目录8192/256MiB等容量不改成无界磁盘，也不成为应用/任务固定数量门槛。

lease权限绑定具体handle/operation，不是同PID下任意旧调用都可绕过锁。需要修改受锁命名空间的exclusive-create/publish/remove须带该operation的本人lease；旧path修改仍BUSY。file读写可绑定获准描述符，另一描述符即使owner相同也按冲突规则拒绝。返回“候选已撤/已知发布成功/结果不明”必须由内核真实状态决定，超时不替这三者选择答案。

lease持真实对象强引用，存ID/gen而非可能已回收的descriptor指针。关闭关联file/directory描述符使相应lease取消/失效，在有界安全点释放候选与强pin；进入提交临界状态时先完成确定提交或确定失败，再销毁对象，不能释放仍被poll借用的record。新lease Release与owner退出同样按此顺序；没有使用lease的旧Close路径保持原合同。已确定发布的文件不会因Close/Release被删除或回滚。

## 5. SCSHELL 公共 API 与真实授权对应

拟新增完全大写 `user/SCSHELL.H`。以下命名及语义用于实现分割，**不是已经存在的函数声明或 exports**；在写正文之前统一冻结 header、records及 manifest。i386 cdecl，公共描述/结果先size/version，跨进程只用值/编号/offset；opaque 指针只属于本进程库实例。

| 拟公开族 | 完整语义 | SC32授权/基础 |
|---|---|---|
| `ScShellConnect/Query/Step/Close` | 可信同SID服务、版本协商、消息泵、EOF后失效；不得自动重放有副作用请求 | typed trusted discovery→ScIpcConnect/Peer；ScBrokerWait原子等待 |
| `ScShellRegisterApplication/UnregisterApplication/EnumerateApplications` | 动态候选/epoch CAS，账户持久层与本SID活动层分开；真实退出撤临时注册 | sender/UID/realm/SID核验，配置写仍普通FILE/lease |
| `ScShellSetAssociation/QueryAssociation/EnumerateVerbs` | 文件类型、默认 verb、Open With、图标/显示名；冲突明确 | ScPathStat/Metadata/FileRead；不会赋予文件读取或执行权限 |
| `ScShellOpen/Launch` | 精确可执行路径+参数，目录→Files，unknown→Open With；返回真实启动结果 | session-lifetime typed launch；旧SCX与动态SCX真实loader，各自参数profile |
| `ScShellShortcutRead/Write/Create` | 旧LNK导入、新descriptor、相对目标解析一次、图标与用户label | ScFile/ScPath权限、exclusive候选/条件publish |
| `ScShellMenuBuild/RegisterProvider/UnregisterProvider` | 标准命令+扩展项目；选择快照和状态query，禁用有真实理由 | SCUI Command/Menu，持久本进程callback ScCallbackPin；IPC不传函数指针 |
| `ScShellClipboardCopyFiles/CutFiles/Paste` | FILES意图、每项真实观察、CAS提交、快照复制后Close；Paste创建实际作业 | ScClipboard*、ScFile/lease；不从clipboard内容继承权限 |
| `ScShellFileOperationBegin/Step/Query/ResolveConflict/Cancel/Close` | Copy/Move/Delete/Mkdir/Rename及逐项结果、进度、确认、取消 | caller-owned worker、ScJob、64KiB File IO、新条件提交 |
| `ScShellDragBegin/Accept/Query/Cancel` | 文件/shortcut/layout角色、真实Drop票/源目标、目标意图与持久结果 | SCUI Drag/Drop+ScDrop*+真实ScIpcPacket.sender；完整动作生命周期 |
| `ScShellFileDialog/Properties/Confirm/Error/Progress` | 统一Shell语义的SCUI真实对话框，不在内核写业务UI | ScUiFileDialog/PropertySheet/MessageBox，实际metadata/error |
| `ScShellDesktopLayoutQuery/Apply/Save` | 动态稳定itemID、选择/位置候选、显式持久提交；失败保原 | SCUI provider/model/selection，配置CAS、host工作区能力 |

拒绝时保留caller结果和已发布模型；部分文件作业须输出规范per-item结果，不以一个总0掩盖后项失败。保留 ScErrorInfo 的domain/raw/operation作为cause，Shell另给用户操作与目标；新Shell错误域的数值由公共header统一分配，不私自占SC32现域。所有长期callback先固定真实函数映像，登记失败不挂链；Remove/Disconnect只撤业务引用，不能承诺当前 ROOT pin立即卸库。

## 6. 会话服务协议

服务 endpoint真实身份由内核任命与发现；HELLO仍检查 version/feature/epoch及 ScIpcPeer。客户端每个 frame的可信sender只取 ScIpcPacket.sender 的PID/gen、UID/GID、realm、SID、subject与flags，与已连接peer和role事实匹配，不信正文自报PID。普通同SID客户端不得借服务owner修改别人的配置或窗口。

拟协议v1固定48B LE头，12个u32：`size,version,opcode,flags,request_lo,request_hi,body_bytes,offset,total_bytes,state_epoch,status,reserved`。opcode由正式private wire统一编配；HELLO/QUERY/PAGE/WATCH/REGISTER_CANDIDATE/COMMIT/ABORT/RESOLVE/LAUNCH/JOB_PROGRESS/JOB_RESULT/CANCEL分族。长度、乘加、chunk顺序/重叠、offset表、counted UTF-8及唯一末NUL先验证；单Broker frame含头不超65536B，每次枚举最多64项是服务预算而非模型上限。总量受真实动态budget/OOM约束，超限明确失败，不静默截断。

同一connection的双字request序号单调不复用，service PID/gen+活动epoch区分重启；每个有副作用请求先准备全部候选，再按expected epoch提交。reply能记录已知提交事实；peer EOF、超时或重启后“结果未知”不自动重复注册/Launch/Move。客户端重新Query/对账后由调用者决定恢复。活动状态不保存外进程VA；共享大正文如后续确需使用，只能ScMemoryCreateShared/Grant/map真实RO及本人生命周期，首实现复制型frame已经可用。

服务维护动态客户端/注册/观察者/作业引用链与索引。背压AGAIN不丢frame、不consume不足容量Receive；Peek后申请候选再Receive。每次Step限制消息/条目工作量，然后原子ScBrokerWait同等INPUT/STREAM/必要deadline；无请求不持续poll。即时ready消耗实际返回事件，stale通知只重查状态一次，不能形成常驻busy loop。

## 7. 应用注册、关联与快捷方式

应用descriptor包含版本、稳定application ID、显示名、绝对可执行路径、旧/动态SCX参数profile、图标spec、分类/菜单parent、verbs及支持类型。ID是用户态注册身份，不是kernel权限。所有字段先copy成拥有候选；无执行路径、坏UTF-8、未知动作、重复ID、菜单环、超FS路径/实际参数容量明确拒绝。

系统种子只读，来自正式发行数据；账户层是本人config路径的显式持久覆盖，SID层是活动会话快照/临时登记。同账户多个SID保存用条件revision/epoch，冲突展示“配置已改变”，不覆盖另一实例刚保存内容；其它SID不自动改活动快照，显式Reload取得新默认。系统应用同名覆盖只能更改用户可见选择，不能换系统SCL或获得可信服务角色。

关联优先次序为用户明确选择→用户默认→系统默认→Open With；目录为Files，SCX为执行或Debug，源码/文本为Studio，图片为真实Lens能力。注册安装可执行文件不调用它的init来探图标。未知二进制不会直接作为文本；扩展名不能当正文有效或安全签名。

Launch保存exe与argv语义，普通打开路径不拼到Shell代码串，不执行eval或命令替换。当前 ScProcessSpawn只有最大1087B命令文本，现代参数正文1024B，旧EXEC旧程序127B；SCSHELL在各实际profile下严格编码引号/反斜杠/空字段并完整检查，不截断、多文件超限改为逐个打开或明确拒绝。应用端仍使用其正式参数API；旧GETARGS文本兼容保持。

旧LNK读取为独立拥有对象，label/command/icon原字节不自动改写。新shortcut可指应用ID或精确path+args；rename label与改目标分开。主题 `@NAME` 先查主题iconroot再正式SYS/ICONS，blank保留SCX嵌入图标规则，literal保持SCF/SCB，缺失/坏资源给同风格回退。SCX icon偏移/长度仍按真实容器边界检查，不执行载荷；SCRES实际接入前由明确Ring3解析正文承担，不能让ScUiIconLoad假支持未有格式。

扩展上下文菜单采用选择快照、provider ID/gen和scope：应用本地SCL provider仅在本进程callback运行，跨进程provider以typed消息给menu descriptor，不给函数/foreigncontext指针。关闭/退出/重启撤注册，菜单打开期间引用快照，执行前重新核选择revision/权限，确认后不能对被替换同名文件执行旧动作。后台插件不能发SYSTEM菜单绕过角色。

## 8. 文件操作与逐项事务

### 8.1 计划、复制与覆盖

Begin固定完整选择、操作、来源、目的地和发起者；原路径由caller CWD解析一次成绝对值，worker不再次叠cwd。确认“3个文件”等同时允许展开完整清单/目标，删除目录先明确recursive，不把整个祖先当一项静默删。总字节数未知时显示已处理字节/项目，扫描完成后再显示确定总量。

工作树动态、迭代遍历；去重父子选择，禁止向自身/后代移动，SandFS无链接不伪造链接语义。目录页STALE先保原模型再重开；READ_STABLE/namespace lease保证copy看到稳定来源。各lease按规范名排序try-acquire，失败释放本次所获并返回BUSY或等真实deadline，无全局持锁睡眠。

每个普通文件：核来源kind/object/revision/权限→exclusive-create-and-pin私有候选→每步最多64KiB读取/写入→处理确定prefix并继续后缀→真实Flush→条件Publish→记录该项已持久提交。候选名字在目的父目录，创建即mode3；最终mode放入publish元数据候选，与目标发布一并提交，不先chmod公开半成品。默认不保留源uid/gid或借源权限授予新owner。必要extent迁移只沿现FS真实增长路径，不整文件R3缓存。

overwrite必须带预期目标对象/revision或预期不存在，确认后目标被改则重新冲突；不能先删除目标再rename。外来活pin/事务返回BUSY，用户可关闭该文件或选新名称。目录merge按项目/文件分别提交，后项失败不回滚已经成功的全部目录树。默认不自动覆盖，冲突给替换/跳过/改名/取消，批量选择只作用本作业已核对的同类冲突。

### 8.2 MOVE 与持久 ACK

同卷且条件允许的MOVE优先条件rename，以一次真实目录提交改变路径，不为移动重复复制全部正文。source lease、来源revision、目的条件、祖先权限/完整后缀长度均核；成功记录RENAME_COMMITTED，不再对旧路径执行第二次remove。目录的整棵rename采用有界metadata候选，不能沿原同步长PIO阻塞WM。

需复制的MOVE先COPY_COMMITTED，再条件remove来源，最后SOURCE_REMOVED。跨窗口target提供的是目的地/动作意图，**不是文件提权或删除许可**。发起者的真实worker负责发布目标和删源，保持来源lease以及自己成功publish的ledger；target UI的ACK表示其实际接收提交/视图状态，与worker已知持久结果匹配才推进。

逐项结果含request nonce、真实worker PID/gen、source规范名/object/revision、destination规范名/新revision、动作、确定bytes、status/cause、commit阶段。保留目的地write/namespace保护直到删源阶段结束，防止目标刚提交即被换掉后仍删源。任何第三方仅发COMPLETE/自报“保存成功”，不能进入文件delete路径；自定义应用接收字节要由自己真实commit后ACK，文件源若无法取得可信worker的提交事实，保源并报告“已复制/移动未完成”。

票据取当前144B ScDropInfo真实source PID/gen和target identity，核Peer/Packet.sender、ticket/nonce/window gen/geometry/visibility。MOVE中的最终UP冻结handoff，30秒期限不是磁盘事务必须在30秒完成：短票只验证接收方/本次目的意图，建立独立owner job后显式Complete/Cancel消费协议轮次，后续长copy用job进度协议。ScDropComplete本身不构成文件commit凭证。失捕获不当UP、离开目标/撤登记/退出/session变化终止尚未handoff的候选。

文件CUT剪贴板用现16B ScClipboardFiles+20B row+绝对path/NUL/alignment；PIN_OBSERVATION仅是仍活open-pin观察，不是持久birthID，源退出后clipboard正文仍有效。Paste必须重新开源并核当下权限与revision；观察已结束不能假称永久STALE或原对象仍在。Cut源内容已变化时提示重新确认，不能删同名新文件。完成后只以原snapshot epoch CAS更新/清理仍属该CUT的已完成项，不清掉用户期间复制的新数据；部分失败保留未完成清单。

### 8.3 取消、失败与恢复

未发布候选可撤空间/句柄/lease，原目标保持；已经publish不能说“取消后全恢复”。作业Cancel在下一个有界安全点停，不在最后确认提交中截断。MOVE目标已提交、源remove失败时保两个副本，状态明确“已复制，源未删除”，允许重新核身份后Retry，不谎报Move成功。目录Delete逐项postorder，不宣称整个目录原子删除。

每个worker仅持本人动态owner链；退出先取消候选/通知peer，再释放pin/lease/资源，完成缓存不含用户VA。服务死去不能根据旧ACK删文件；worker也死去则只有kernel真实已提交事实和磁盘对象，不承诺用户析构执行。

持久恢复记录是普通版本化用户文件，使用exclusive候选+条件publish自身事务；写开始/发布阶段/结束，用于显示未完成操作及清理本人可确认的临时文件。突然断电后的记录可能滞后，不根据日志自动重放source删除、覆盖或认定同名对象仍是旧文件。重新启动只重新核当前真实对象/权限并让用户恢复或保副本。现v5并非完整掉电日志，扇区撕裂/超级块不明按FS faulted处理，CRC不证明原子或durable ACK。

## 9. 桌面布局、菜单、任务栏与绘制

图标模型使用动态array/hash/provider，item ID在活动桌面单调不复用；选择储存稳定ID，与显示顺序无关。桌面文件来源为会话Desktop目录，旧DESK/LNK只读投影保原。当前位置不是filesystem birthID：持久layout为拥有配置记录，按规范shortcut路径/自有登记ID匹配，删除重建/rename由真实Shell结果显式更新，未知外部改名仅新增未布局项，不能误认旧观察pin。

拖起时固定当时选择和原坐标，预览只改变候选；UP才整体发布新坐标，Esc/失捕获/输入epoch变保原。空白框选用空间索引查询相交项，多选拖动只重绘旧/新图标与选区损伤；Grid/AutoArrange以当前工作区、字号、标签高度排布，空间不足有可滚动/分页视口，图标不丢失。Save按交互提交合并写入，不每鼠标包写盘。

位置以96基准逻辑坐标和layout版本保存，实际DPI/工作区变更重新布局及clamp；保存原位置以便回到更大显示恢复，不把裁剪后的屏幕坐标永久覆盖全部记录。badlayout/IO/OOM保有效活动候选并给可恢复错误。账号持久默认与各SID活动快照分开，注销收活动引用，重启只读通过完整校验的已提交配置。

桌面内部拖动是LAYOUT；真实FILES拖到文件夹执行Copy/Move；Files拖到Desktop默认展示明确选择“复制到桌面/移动到桌面/创建快捷方式”，不把操作混为位置移动。Shortcut自己拖进Files可复制快捷方式文件，不能默认搬其目标。所有Drop target按真实visible/enabled与kernel角色登记撤销。

Start是SCUI Menu/Command动态树，配置分类/子菜单和应用注册合并，无全局32项数组。桌面菜单按上下文给Open/Open With、Cut/Copy/Paste、Rename/Delete shortcut、Create、Arrange/Snap、Refresh、Properties、Settings；不可用项有统一状态/原因。保持助记符、方向键、Esc、焦点、外点关闭不穿透，菜单布局与hit同一geometry。

任务栏由获准窗口分页+变化订阅构成动态模型，按application ID可分组但保留每个真实窗口；旧程序无ID用真实exe/PIDgen回退，不截为固定按钮数。Overflow可滚动或展开列表，状态以WM事实为准。关闭先发CLOSE请求，取消/未保存文档veto保窗口，不能为taskbar方便强制kill；需要强制终止时单独确认且按真实进程权限。

壁纸解码/图标解析/大目录计划是有界后台工作，GUI线程只接受完整资源候选；所有cache以resource revision、theme/font/display epoch、尺寸、SID、read权限为key，失败候选释放，旧画面可保留。隐藏不画、不提交、不生成绘图timer，业务/IPC/文件作业继续；切回按代数invalidate。图像公开服务/SCGFX/SCRES实际接通前不声称资源迁移完成。

Classic为真正矩形凸凹边、选中/焦点/disabled、固定像素图标；Aurora共享行为和布局，颜色取GFX/THEME已登记角色，字体用原Phoenix12/16真实mask/metrics。初始布局采用96基准菜单/工具/状态区，按SCUI metrics及DPI测量，不给中文姓名固定8px宽；窄/高缩放可以重排和滚动，仍保留动作。桌面、文件夹、文档、shortcut overlay、Cut弱化状态使用自有资源和命名角色，新增色先入GFX规范。

首版空间安排如下，数值是96基准逻辑尺寸的设计起点，最终以真正font metrics、theme和客户尺寸验算；不是把旧截图拉伸到窗口：

| 区域 | 初始排版 | 状态与交互 |
|---|---|---|
| Desktop工作区 | 左上24留白，自动网格沿用128×112节奏；icon48，label最多两行，文字按整标量省略 | 选择框包围icon+label；键盘focus矩形独立；Cut仅弱化所选内容，目标高亮只实际可接收区域 |
| Taskbar | 下沿实际theme.taskbar_height；左侧“沙核/Start”，中间弹性窗口区，右侧时钟/会话状态 | Overflow弹出/滚动仍可达全部窗口；当前/最小化/closing有不同状态；时钟仅变化时损伤自身矩形 |
| Start/上下文 | Menu行不低于24且容纳真实字高+上下padding，子菜单贴parent行，边缘翻转/滚动 | icon/label/shortcut/箭头按列对齐；separator不导航；disabled不执行；无空间也保键盘可达 |
| 文件属性/确认 | 上方目标名/路径与icon，正文按组显示kind/bytes/owner/rw，底部主动作及Cancel | Delete/MOVE展示源与目标，默认焦点优先安全取消；错误cause可展开，不用小状态栏吞关键失败 |
| 文件进度 | 当前完整目标、已处理items/bytes、明确总量或不定量条、冲突/取消入口 | 文件提交完成才推进commit状态；“已复制，源未删除”独立显示，不以100%暗示Move成功 |

壁纸位于desktop底层；图标与文字保留层次、contrast和留白。真实文件图片是内容色，不受UI角色限制，overlay/框选/反馈仍只用登记角色。自有Classic资源与Aurora资源分别按目标像素设计，布局/命令/权限相同。初版截图由后续真实host提交取得，文档排版不冒充运行画面。

## 10. 性能、容量和退出约束

活动图标/应用/菜单/关联/窗口/作业/lease/client均动态，只有IO chunk、消息quantum、枚举页和绘图资源的明确尺寸是预算。内存耗尽先撤候选；不能把固定图标数组、固定连接表或“最多16操作”当取消任务上限的替代物。相同SID内身份/窗口/注册变化使用索引和订阅，不每tick扫描所有PID或整个卷。

绘制缓存、空间索引、可见表格/provider与damage避免每mousemove全1080p重生成；SCCLIB malloc子分配，不用ScMemoryAllocatePages逐条当malloc。IO worker按真FS短步让出，metadata阶段锁不跨无限用户等待，IPC队列背压可见，不因WM好看丢作业结果。

后续源码须统计真实include/macro/source/token/pool/code/load/BSS/stack，不以估计称能自编译。现SCCC SOURCE为一次取得且借用期间不移动的1MiB heap；SCL与旧SCX代码/映像限制分别检查。工作区增大没有放开旧SCX Stage262144B或其装载/栈预算，也没有放开SCL16MiB边界。SCSHELL正文应按职责放进真实独立SCL；若实际新source超预算，依据真正规模扩工具链或拆独立模块，不能删桌面/文件功能换通过。

预算只有实现理由，性能达标需要以后实际同分辨率/主题/输入/典型大目录及真实文件规模的PIT、QEMU一核CPU、输入长尾、绘制/提交量、PF与owner账。复用未受影响历史证据，不新增理论排列矩阵；先完成实现与接线，再按具体风险统一Windows双盘定向验证。

## 11. 设计交接与出口

实现先冻结三个最小底座（trusted host/discovery与窗口动作；file lease/conditionalpublish/remove；session-lifetime launch），再正式SCSHELL.H/private协议及独立库/服务/worker，最后默认桌面与Files2接入。任何NOT_SUPPORTED分支只代表实际缺能力，不可注册为已支持功能或置cap。

交付要实际运行GCC与系统内SCCC/G1生成的新SCL/SCX，核导入导出与独立加载；普通真实登录完成选择/布局重启、关联打开、多级菜单、任务栏旧新窗口、跨窗口文件COPY/MOVE/Cancel和权限失败，核真正磁盘正文/目录以及close/exit/logout资源。还须观看原尺寸Classic/Aurora截图、保旧VGA入口和原用户配置。设计、初版源码或Phase4局部87检查都不是Phase5完成。

修订：2026-10-15，依据现Desktop/WM/Files/Settings/M9 Shell、现代SC32/SCUI/Broker/Drop源码建立Phase5完整工作流、可信用户态服务和文件条件事务合同；仅设计与审计，所有SCSHELL新增实现/构建/客体验证待推进。
