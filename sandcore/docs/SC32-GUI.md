# SC32 消息窗口、输入与损伤协议

所属版本：**SandCore M10a2 — The Application Platform Era**。本规范对应 `kernel/sc32_gui.c/h`、`wm.c/h`、`wm_native.inc` 及公开 `user/SC32GUI.H`、私有 `user/sc32/SC32GUIWIRE.H`。三环过程表、同步回调与 cdecl 桥见 [SC32-GUI-USER.md](SC32-GUI-USER.md)；整体范围见 [SC32API-DESIGN.md](SC32API-DESIGN.md)。

本次状态是**主代理统一构建与真实Windows双盘P0运行通过**。客体生成库与三窗口应用、全部27个约定行为、暖态PF6878→6878、GUI owner/窗口池归零证据见 [m10a2-sc32](../evidence/m10a2-sc32/README.md)。首次Shell背景表+1页已单独实测归属；未把初次差额直接改PASS。此P0协调层不代表完整SC32API、SCUI、四库或M10a2已完成。

## 1. 分层与兼容边界

公开入口全部为 cdecl 声明，应用通过 `SC32API.SCL` 调用；公开头没有 gate、寄存器桥或内核地址。窗口过程 `ScWindowProc(window, const ScMessage *, context)`、控件树和应用 context 只在本进程 Ring3 保存、调用。IRQ、WM、消息队列和退出清理均不进入三环回调。

新消息窗口与既有窗口共享画布、会话桌面和合成器。旧 `win_t` 的 80B 前缀保持，新增状态位于动态 `window_record_t` 私有尾部；旧 `task_t` 168B 和所有旧 SCAPI 结构不扩写。新窗口持有实际 PID/代数、会话、owner 树和输入资格；其 handle 使用现有不复用的正 31 位编号，v1 generation 等于 handle。编号域耗尽与物理资源不足明确失败，不引入固定任务、窗口或队列数量门槛。

旧 WINRGB/WINOPEN、GETKEY、FRAME/FRAME32、鼠标快照和旧 Close 杀 owner 行为保留。新模式由新 CREATE selector 明确建立，不能借旧调用忽略的寄存器加入选项。新 Close 是可取消请求；Destroy 释放指定窗口及其 owned 子树，进程和其它独立根窗口继续。VGA 路径保留，真彩合成继续使用主题槽位与直通 ARGB。

## 2. 私有 gate 与缓冲合同

统一私有 `SYS_SC32=0x300`：EBX=selector、ECX=request 用户虚址、EDX=output 用户虚址。真实调用 PID 由陷入分发器提供，应用不能伪造。状态来自独立 SC32 域：0 OK，1 PENDING，2 TIMEOUT；错误为 `SC32API.H` 定义的 -101 起枚举，不混用旧 syscall/SCL 负值。

所有输入先用 `sc32_core_input` 复制到可信局部记录：size 必须恰好等于该 v1 输入尺寸，version 必须为 1，完整输入可读，保留字零。输出先用 `sc32_core_output` 检查 8B 前缀、version=1、size 至少为规范尺寸且最多 65536B，**整个声明范围必须可写**，然后才检查请求、申请候选或改变状态。失败不写输出、不消费消息；成功只发布规范 v1 前缀，并写回规范 size，未知尾部原样保留。没有输出的操作要求 output=0。

| selector | 操作 | 输入 v1 | 输出 v1 |
|---|---|---:|---:|
| 0x100 | CREATE 候选 | Sc32WindowCreate 64B | Sc32WindowRef 16B |
| 0x101 | ACTIVATE | Ref 16B | NULL |
| 0x102 | DESTROY | Ref 16B | NULL |
| 0x103 | QUERY | Ref 16B | ScWindowInfo 96B |
| 0x104 | PLACE | Sc32WindowPlace 40B | WindowInfo 96B |
| 0x105 | CONTROL | Sc32WindowAction 24B | WindowInfo 96B |
| 0x106 | OWNER / 样式 | Sc32WindowOwner 32B | WindowInfo 96B |
| 0x107 | TITLE | Sc32WindowTitle 24B | NULL |
| 0x108 / 0x109 | PEEK / GET | NULL | ScMessage 64B |
| 0x10a | WAIT | Sc32MessageWait 16B | NULL |
| 0x10b | POST | ScMessage 64B | NULL |
| 0x10c | INPUT | Ref 16B | ScInputSnapshot 96B |
| 0x10d | CAPTURE | Action 24B，action=0/1 | NULL |
| 0x10e | INVALIDATE | Sc32Invalidate 36B | NULL |
| 0x10f | PAINT_BEGIN | Ref 16B | ScPaintInfo 64B |
| 0x110 | PAINT_END | Sc32PaintEnd 24B | NULL |
| 0x111 | SUBMIT | ScSubmitDesc 64B + Rect 数组 | NULL |
| 0x112 | CLOSE_CANCEL | Ref 16B | NULL |
| 0x113 | QUEUE | NULL | ScQueueInfo 32B |

输入 Ref 为 size/version/window/generation 四字。不存在、旧编号或错代数返回 STALE；真实窗口属于其它 PID/代数返回 ACCESS。窗口查询和 owner 关系仅限本人；不返回内核窗口、画布、链表或过程指针。标题为最多 127B 加 NUL 的合法 UTF-8，拒绝控制字节、坏标量、跨坏页和缺 NUL；显示省略不截断 UTF-8 标量，旧 12B 标题副本仍可被旧快照读取。

CREATE / PLACE 的 width/height 是客户尺寸，允许 94..1920 × 40..1080；实际几何裁入当前工作区，以 QUERY/SIZE 返回为准。x/y 是屏幕外框坐标。CONTROL 为 SHOW=0、HIDE=1、MINIMIZE=2、MAXIMIZE=3、RESTORE=4、FOCUS=5。样式 CLOSE/MINIMIZE/MAXIMIZE/RESIZE/MODAL/HIDDEN/DISABLED 为位 0..6；HIDDEN 是创建时默认值，后续显隐通过 CONTROL，改变 owner/样式不会暗中重置显隐。

## 3. 创建、关闭与 owner / modal

CREATE 完整申请记录、画布、z 序容量和通知候选，然后返回隐藏的 CREATING 窗口；内核不排 CREATE、不暴露可调用用户地址。SC32API 根据创建成功事实，在 Ring3 同步调用一次 CREATE。回调成功后 ACTIVATE 才解除 creating 并按照 HIDDEN / owner 可见性决定显现；拒绝、回调期间 Destroy 或 ACTIVATE 失败时销毁候选及其 creating owned 子树。

标题 Close、上下文 Close 在真实点击点保存窗口内 CLOSE 待办。窗口仍存活，重复点击不无限排同一请求；GET 只领取一次消息，不清 closing 状态。CLOSE_CANCEL 直接清待办，无须申请通知页；默认三环过程调用 Destroy。关闭最后一个窗口是否退出由应用决定。

显式 DESTROY 经合法 Ref 校验后保证释放，不因队列 reservation OOM 拒绝回收。子树按叶先、保存父边的迭代遍历收齐，不递归耗尽内核栈；正常 owner 遍历 O(k)，z 序/创建序数组压缩另按现有 WM 成本计。三环桥同步发一次 DESTROY，内核 queued DESTROY 是辅助事实，桥忽略已经退役的重复通知。队列因实际 OOM 丢失辅助通知时记录真实 loss。

owner 必须为本人真实新窗口，禁止自环/祖先环；每个 owner 同时最多一个 modal child，嵌套模态可形成动态链。隐藏或禁用祖先通过子树缓存阻断后代显示/输入，真实改变时失效 top 命中缓存和 visibility epoch。整个传播完成后再发布最终焦点，避免中间父窗临时得焦。点击被 modal 阻断的父窗只激活实际对话框，不把标题 Close 或按钮转投给对话框。可见模态窗保持可激活关系，控件级 Tab 和菜单策略属于 Ring3 SCUI。

旧 WINCLOSE 仍可能直接关闭某个新窗口，此时剩余孩子解除 owner 引用并更新后代资格；不能保留已释放窗口地址。旧 Close 的 owner 终止和异常 EXIT 继续走内核 owner 链兜底。

## 4. 消息布局与顺序

`ScMessage` 恰好 64B/16 字：size、version、id、window、generation、sequence_lo/hi、tick、flags、epoch、data[6]。sequence 是每 GUI 进程的真实 64 位投递顺序，以双字无符号比较；tick 保留 IRQ 生成输入的原 PIT 低字，普通状态事实取提交时 tick。epoch 的含义按类别明确，不能把 geometry / input / visibility / raw route epoch 混为一个计数。

| 消息 | epoch | data[0..5] |
|---|---|---|
| PAINT | geometry | x、y、w、h、paint_epoch、0 |
| SIZE | geometry | client_w、client_h、outer_w、outer_h、0、0 |
| MOVE | geometry | outer_x、outer_y、0、0、0、0 |
| FOCUS | input | 0失焦/1得焦、另一真实 handle 或0、0、0、0、0 |
| KEYDOWN / KEYUP | input | physical、symbol、text scalar、modifiers、repeat、0 |
| TEXT | input | scalar、modifiers、0、0、0、0 |
| MOUSEMOVE / BUTTONDOWN / BUTTONUP | input | client_x、client_y、buttons、changed-button mask、modifiers、0 |
| CLOSE / DESTROY | input | 六字零 |
| CAPTURELOST | input | 原因、0、0、0、0、0 |
| VISIBILITY | visibility | 实际可见、本人 hidden、0、0、0、0 |
| THEME / DISPLAY | 对应主题/显示计数 | 六字零 |
| QUEUELOSS | raw route | software_lost_lo/hi、reason bits、raw_lost_lo/hi、同步 route epoch |
| COMMAND / USER>=0x8000 | 0 | 本进程应用定义的六字值 |

flags：KERNEL=1、COALESCED=2、REPEAT=4、DEFERRED=8、APPLICATION=16、RAWLOSS=32、PULSE=64。POST 仅允许 COMMAND 或 USER 域，目标为本人真实窗口；无窗口仅 USER。请求中的 flags/epoch/sequence/tick 必须零，内核生成 APPLICATION 和新序号，不能伪造 KEYUP/FOCUS/CLOSE。应用 data 是自己的值，内核不解引用它。

旧 FOCUS-loss 先于新 FOCUS-gain；只同 PID 也严格区分 handle。SIZE 在新画布发布后产生，然后建立对应 PAINT；未变客户几何的纯 MOVE 不改 geometry，以便复用画布。虚拟 CLOSE / PAINT 与实体队列按 sequence 合并领取，不能跨这些待办合并移动。仅真正相邻、同窗口/代数/input epoch/buttons/modifiers 的 MOUSEMOVE 合并；键、按钮、焦点、capture 或其它消息都是顺序屏障。

## 5. 原始输入、快照与 capture

键盘和鼠标 IRQ 共用 `Sc32RawEvent` 64B 有序 ring：kind、tick、route epoch、session、physical、symbol、text、modifiers、down、repeat、legacy、x、y、buttons、flags、reserved。raw flags 为 COALESCED=1、PULSE=2、BUTTON_EDGE=4。IRQ 只解码/写有界事实，不分配、不回调、不改 WM；255 条可用容量是中断突发预算，不是 GUI / 进程数量上限。

启动先让既有字符/鼠标队首消费至空，再启用 raw producer；之后旧字符仅由 task0 按同一 FIFO 的实际目标路由，不让新旧消费者争队首。每次 task0 pump 至多处理 256 条；旧窗键队满会保留 raw 队首施加背压。raw 运动只能合并同会话/epoch/modifiers/buttons 且两条都无按钮边沿的相邻运动，不覆盖首次按下/松开的原坐标。

产生时保存会话与 route epoch；用户调用改变焦点、几何、可见性、销毁可见目标或切会话时更新资格，旧 raw 不得重投新位置/新焦点。task0 内依 FIFO 发生的点击、焦点和拖动使用同一顺序路由，pump 不为每一条正常事实改共享 epoch；后续键按已提交的真实焦点接收。

KEYUP 来自真实断码，Ctrl/Alt/Shift/Caps/Num 修饰快照分别为位 1/2/4/8/16；重复来自真实重复 make。physical 为非扩展扫描码或 `0x80|扩展码`，箭头/功能键 symbol 见公开常量；text 是单独可打印标量，控制组合保留 KEY 事实而不伪造正文。Print 的 fake Shift 不进入 held；Pause 只有真实 pulse make，physical=255/PULSE，不造不存在的 KEYUP，不设置 held 位。

鼠标每个变化按钮分别投递，按钮位左=1、右=2、中=4；同包多个变化按位序 0..2 投递，包之间保留顺序。坐标是实际客户坐标，capture 下可为负数（data 按 ScI32 解读）。系统标题、边框、resize、菜单点击不泄漏为新的客户 Button；纯客户移动不触发无效整帧合成。

INPUT 的 96B 快照精确核对 handle，返回当前 input epoch、焦点、可见性和 capture。只有该 handle 真正聚焦且有输入资格时返回全局 physical_down[8]、modifiers、客户坐标和 buttons，否则这些输入值为零；不能凭 owner PID 让同进程 A/B 都报告得焦。

新 capture 保留绝对光标和客户坐标，仅本人当前真实聚焦/可见/有资格窗口能取得，不与旧 relative capture 同时持有。失焦、隐藏/会话切换、故障、销毁、Escape、真实 loss 或显式关闭会撤 capture。CAPTURELOST 原因为 1焦点、2隐藏、3销毁/退出、4Escape、5loss、6显式释放/替换。显式释放不因通知分配失败拒绝。旧 relative 模式、硬件 totals 和旧接口含义保持。

## 6. 动态队列、损失与等待

仅 CREATE / POST 等真正需要 GUI 的进程按 PID/代数申请动态 owner 元数据，查询/装载库本身不申请 GUI 画布或队列。队列用每 PF 页 63 条消息的块链扩容，63 是分配粒度；正常消费归还旧块，只缓存一块空页至进程退出，避免每事件借还物理页。系统调用先保留通知候选，失败归还新块和新 owner；CLOSE 和 PAINT 待办直接保存于存活窗口，不依赖新页。

ScQueueInfo.pages 计实际队列 PF 页（包括空页缓存），不冒充 owner / WM 元数据或画布全部页数；messages 是缓冲实体、就绪 CLOSE / PAINT 与 QUEUELOSS 数，已取消目标或丢失屏障前输入可在下一次领取时清除，查询不强制全队列扫描。software lost 用真实失败记录的双字累加；raw lost 是共享 IRQ FIFO 的真实全局计数，不能说每个窗口各丢了同样数量。

QUEUELOSS reason：bit1=真实 raw ring 缺口（RAWLOSS，data[3/4] 为累计 raw loss），bit2=本人消息队列实际失败，bit4=另一队列 OOM 推进共享 epoch 后的会话重同步。bit4 不增加虚假 software lost。raw 缺口统一撤旧 epoch，并通知当前会话 GUI owner；软件 OOM 首次也让当前会话其它 GUI 明确重同步，正常热路径不扫描全部 GUI 进程。

缺口先建立 sequence 屏障，再撤 capture/拖动与 held 模型。屏障前缓存的设备消息不重放；应用领取 QUEUELOSS 并用 INPUT 重同步。尚未领取期间的新 device 事实明确计失败，其新按住位也阻断至真实释放；不能在后来的 repeat 中复活拖动/按住，不能伪造 KEYUP 或 BUTTONUP。CLOSE/PAINT 仍保留，普通状态事实按可用资源排队。

WAIT 在同一 IF=0 中检查可领取消息并安装现有 INPUT 等待，非空立即 OK；空且 timeout=0 返回 TIMEOUT，正 timeout 或 `SC32_INFINITE` 返回 PENDING 由 task 分发器安装睡眠。合法有限 timeout<=0x7fffffff，单位为 PIT tick。唤醒重新查真实队列，ready 为 OK、实际超时为 TIMEOUT，其它失效通知为 PENDING；Ring3 GET 循环会重新睡眠。旧 EVENTWAIT 原 bit 返回值和旧忽略寄存器保持，不能把 GUI 状态混入旧合同。

## 7. PAINT 与矩形发布

INVALIDATE 校验非空且完整位于客户区的矩形，合并未确认损伤并推进 paint_epoch。隐藏/CREATING 时只累计，不排 PAINT、不唤醒绘图；真实恢复先 VISIBILITY 后完整 PAINT。GET 不确认损伤，Begin 领取 geometry / paint 双 token 和当前损伤，已在 painting 返回 BUSY，无损伤或隐藏返回 AGAIN。

End 只有匹配活窗口、当前 geometry 和 Begin claim 的 token 才成功；无新损伤时清 pending，有新损伤时保留并再次确保 PAINT 就绪，即使重入 GET 已提前领走新消息也不会漏重画。隐藏、尺寸、主题或显示使旧 claim 失效，旧 End 返回 STALE；恢复和新状态建立新的完整绘制。Begin 输出整区预验在 claim 修改之前，坏输出不会占住 painting。

SUBMIT 先把 1..64 个 24B Rect 完整复制到可信数组，并验证整个声明 pixels.bytes 为本人 RO 范围（可为只读 SCL 数据），再检查 window/geometry/visibility、四字对齐、width/height、stride、全部乘加溢出、实际末行覆盖以及每个 source/destination 边界。64 是单次服务预算，不限制总提交数量。任一失败均在第一笔画布写入前返回，矩形错不部分发布。

像素按直通 `0xAARRGGBB` 复制；发布前固定当前任务/CR3并开放 IRQ，WM 不会合成半幅或释放正在复制的画布。只检查本次矩形中实际变化的像素，末尾一次发布变化并集；相同像素不制造客户损伤。部分透明立即撤 opaque 快捷资格，仅完整覆盖且全部 alpha=255 才可升级整窗 opaque，不能由最后一块错误推断全窗不透明。

SUBMIT 成功不暗中确认 PAINT，应用完成提交后显式 End。源 surface、控件布局和栅格属于 Ring3。VGA 设备继续既有映射/全屏回退，旧 FRAME32 完整提交仍存在；当前非 PAE 无 NX，不把只读或 source 检查说成新 GPU/共享映射/W^X 能力。

## 8. 初始化、退出与成本边界

`wm_init` 内调用幂等 `sc32_gui_init`。main 在旧 mouse/key 消费后、合成前 IF=0 调用 `sc32_gui_pump`；IRQ 读取 O(1) route epoch/session getter。task 对 WAIT 做原子安装，并在用户页目录释放之前调用 `sc32_gui_stop_step`；不增加公开任务字段。

stop 首轮撤真实 capture、拖动和焦点；随后每次至多 32 个 attention / 页块操作，逐窗解绑窗口内待办链，释放全部队列页和动态 owner。之后旧 `wm_close_owner_step` 沿已有 owner 链释放画布和窗口；不能在 owner 已归还后留下 attention 指针。正常 fini 静默撤窗，异常退出不依赖 Ring3 回调。

高频输入只按实际目标/owner 入队；消息块按页复用，相邻普通运动合并，静止/隐藏窗口不持续生成客户帧。owner 隐藏传播按子树，Destroy owner 遍历线性且不递归；z 序数组插入/关闭仍有原 WM 压缩成本。全尺寸画布初填和 resize 交集复制在持有当前任务时开放 IRQ，复制结束才提交候选，OOM 保留旧窗/旧帧。以上是实现边界，不是未经测量的性能达标结论。

统一定向验收只需解决新行为：同进程双窗精确焦点、真实键通断及 Ctrl/Alt/重复、每按钮顺序、Close 拒绝/单窗销毁、几何旧提交拒绝、PAINT token/隐藏恢复、暖态页回收和旧窗口兼容。宿主构建、系统内 SCCC 编译、Windows 客体输入/截图分别记录；未受改动的有效证据复用，不另开理论全矩阵或复制整盘。

## 修订

2026-10-14：落 P0 消息窗口/动态队列、统一 raw 键鼠、owner/modal/capture、真实状态消息、PAINT token 与 ARGB 矩形提交；保持旧窗口/SCAPI ABI，完成静态复核并交主代理统一构建与定向客体验证，尚无本切片新运行 PASS。

2026-10-15：主代理统一构建、实际客体生成的库/公开应用与三窗口P0运行通过；首Shell背景表一页单列，暖PF及GUI池精确恢复，证据见evidence/m10a2-sc32；完整平台继续。
