# SC32 GUI 三环过程与消息桥

所属版本：**SandCore M10a2 — The Application Platform Era**。本切片在同一个`SC32API.SCL`中新增20个真实GUI入口，源为`user/guiapi/sc32_gui.c`。公开`SC32API.H`底部包含`SC32GUI.H`；私有`SC32GUIWIRE.H`由内核GUI组件与库共同使用。旧65个基础入口保留；此处不修改旧SCAPI、旧窗口/整帧调用的参数或行为。

**当前主代理已统一构建与实际Windows双盘运行。** 客体生成的SC32库/根库及客体三窗口应用实际通过有序Shift通断/文本/三按钮/capture、模态与单窗关闭、显隐后台模型、旧geometry/paint拒绝和正常回收，见 [SC32证据](../evidence/m10a2-sc32/README.md)。完整SC32API、SCUI控件树、四库及M10a2范围继续，不把本次列明出口或函数计数当作整个版本完成。

## 1. 公开调用与ABI

应用只包含完全大写`SC32API.H`，由动态SCX导入需要的正式函数。公开头只有cdecl声明、常量与结构，没有软中断、寄存器布局、内核canvas/窗口/过程地址。窗口过程签名为：

```c
typedef ScStatus (*ScWindowProc)(ScWindow window,
                                const ScMessage *message,
                                void *context);
```

过程仅由当前进程的Ring3栈调用；kernel保存真实窗口状态和拥有关系，库保存procedure/context。context不由库解释或释放，应用必须保证它在回调期间存活。SCX函数随进程存活；SCL函数地址通过正式导入或ScGetSymbol取得时遵循runtime地址固定/依赖闭包，不允许手工卸载仍被过程持有的代码。

| 公共结构 | v1尺寸 | 合同 |
|---|---:|---|
| ScWindowDesc | 64B | 标题、期望客户几何、样式、owner引用、cdecl过程和context；四个保留字零 |
| ScMessage | 64B | ID、目标/代数、双字sequence、tick、来源flags、epoch、六字payload |
| ScWindowInfo / ScInputSnapshot | 96B / 96B | 本人窗口事实；精确窗口输入资格和按键位图副本 |
| ScPaintInfo | 64B | geometry/paint token、真实损伤及客户尺寸，不借内核像素地址 |
| ScSubmitDesc / ScSubmitRect | 64B / 24B | 本人源像素/stride及矩形表，直通ARGB32 |
| ScQueueInfo | 32B | 真实队列条数/页数/损失/epoch副本 |

输入记录精确size/version1，保留字段零；输出先填size/version1，可声明更大合法范围，成功仅写该版本前缀。旧20B WININFO、24B POINTER与其它历史输出不扩写。库转换/消费前验证最终out；kernel再次预验完整声明范围，参数或映射失败不消费消息、不改半幅画布。

公开的20个入口为WindowCreate/Destroy/Query/Place/Control/SetTitle/SetOwner/RejectClose/Invalidate，PaintBegin/End，WindowSubmit，InputQuery，PointerCapture，MessagePeek/Get/Post/Dispatch，DefaultWindowProc，MessageQueueQuery，全部使用`Sc`前缀。动态导出清单只列实际函数正文；私有分配器、过程表、桥及收束函数不导出。

## 2. 创建事务

`ScWindowCreate`先检查caller out、完整Desc、标题NUL长度（最多127B）、过程的本人可读映射、owner引用及保留字段，再取得小型metadata和必要索引。NULL procedure使用默认过程；可读代码检查不声称现非PAE分页提供NX或同进程库沙箱。

真正的顺序为：

1. 新kernel CREATE只创建隐藏CREATING窗口，返回稳定句柄/代数，不启用输入或客户PAINT。
2. 库记录procedure/context及owner图，同步调用一次CREATE。其payload给期望客户宽/高、style、owner ID/代数；实际裁入工作区的几何可在过程内WindowQuery取得。
3. CREATE必须返回0表示接受。非零拒绝，或回调已销毁本人窗口，创建不会成功返回；caller out保持。
4. 回调返回后重新验证out，防止init样式回调释放/保护了原应用缓冲；随后才ACTIVATE。
5. ACTIVATE成功，库标ACTIVE、写出句柄，后续输入/可见性/PAINT才按实际队列发生。

拒绝/激活失败会关闭候选及其真正owner子树，撤销metadata；已经进入CREATE过程的窗口在回滚中最多收到一次DESTROY，便于以同一生命周期释放模型。回调自行创建的无关独立窗口不被擅自关闭。CREATE中重入Destroy本人窗口返回后仍安全：constructor持有metadata直到确认CANCELLED/回滚结果。

合法DESTROY在kernel消息页预留不足时仍释放真实窗口/画布，缺失的异步副本计入真实QUEUELOSS；它不能因准备通知失败而阻止创建回滚或退出收束。库只在内核确实关闭成功后同步退役子树，若出现其它实际gate错误则保留候选记录用于收束并报告其domain/raw/operation，不伪称回滚成功。

CREATING窗口可以查询/设置标题/准备损伤和owner，但移动、窗口控制及尚不可见的绘图依内核状态返回BUSY/AGAIN。不要在其CREATE中把早取的非CREATE消息强行分发回该窗口；Dispatch对此返回BUSY，不能越过构造门槛。

## 3. metadata、查找与重入

过程记录在4KiB slab内复用，页内槽数由真实结构宽度计算。桶索引起初一页，以实际记录数量增长；输入分发按稳定ID查hash，不逐消息扫描全窗。页/桶数量没有固定窗口门槛，实际内存不足明确失败。

索引扩容保留旧桶到创建事务结束。创建失败且剩余记录能放回原容量时，无额外分配地重建原索引，归还候选页；嵌套CREATE确实留下更多独立实例时，新容量才有实际归属。slab空页归还，最后一条记录释放后桶也归还；未把失败页藏成无限缓存。

每条记录有depth、destroying和retired状态。外层调用/过程分发先持有记录；回调可重入创建、销自己或销其它窗。实际内核Destroy成功后，先给整个已关闭子树打destroying标记，再叶先通知；孩子DESTROY重入销父时返回0，不会把尚未通知的兄弟解除而漏回调。收束/退役即停止接输入和修改owner，节点仍留到depth归零；遍历owner图或队列损失广播时也暂缓回收，防止保存的next指向已释放页。

owner图采用显式父/子/兄弟链。SetOwner成功后才更新三环镜像，环引用在修改前拒绝。控件树、菜单、control ID、布局和命令传播仍属于后续Ring3 SCUI，未进入kernel或此顶层窗口表。

## 4. 销毁、关闭拒绝与fini

`ScWindowDestroy`关闭指定消息窗口及其owner子树，不结束整个进程。内核先迭代释放真实子树；库叶先同步发一次DESTROY，再处理父过程，避免先释放父model再调用孩子context。多窗口应用的其它根窗口保留，何时退出进程由应用决定。

用户DESTROY过程开始前记录已retired/notified；过程内重复Destroy不会再次进入该过程。内核稍后queued DESTROY副本只用于确认/忽略，不能再次调用已释放context，也不为此永久保留每个历史句柄的metadata。

真正由外部/旧WINCLOSE关闭产生的DESTROY，在Dispatch中使用已有匹配过程一次退役；分发前确认该代数在内核已STALE，手工构造DESTROY不能撤销仍活窗口的过程。旧单窗WINCLOSE可能由内核解除其孩子owner而非关闭孩子；库对应解除镜像owner，不伪造仍活孩子的DESTROY。显式Destroy发现目标早已STALE时同样只退役目标，保留仍活的孩子；该调用仍返回STALE而不伪称此次已关闭子树。应用若混用旧调用，应自行管理这些仍活窗口。

默认过程对CLOSE调用WindowDestroy，对其它通知返回0；它不会因为没有绘制就确认PAINT，也不会在关闭一个窗后自动退出进程。自定义CLOSE处理可调用`ScWindowRejectClose`（独立CANCEL selector），明确清closing/pending，从而继续接收下一次关闭请求。单纯返回0表示过程已处理，不自动清closing，允许保存确认等异步工作；不把返回值或轮询伪造成内核CLOSE。

正常main返回时，`Sc32Finalize`首先静默收束GUI：停止新创建和消息分发，尝试关闭所有真实句柄、退役过程记录、归还页和索引。**fini不调用用户DESTROY**，因为SCUI或其它依赖库可能已释放model。随后才释放原基础库路径缓存。异常/ProcessExit不依赖用户fini，kernel owner链及队列清理是独立兜底；正常runtime清理须在main/回调栈已结束的原合同下执行。

## 5. 消息等待与分发

Peek无消息返回PENDING且不消费。Get先尝试真正GET；out直接交kernel，完整写回成功后才消费，不能先领临时消息后发现用户out坏了再丢输入。timeout0为非阻塞PENDING，1..7FFFFFFF为有限tick期限，FFFFFFFF无限；有限期限始终相对最初start，不被无关唤醒重置。

空队列只用新的GUI_WAIT。kernel把检查和特殊等待合成一次操作，恢复可给OK/TIMEOUT/PENDING；库对取消/过滤造成的PENDING重查后重新等待，不用持续YIELD poll。有限等待以u32差分覆盖一次PIT回绕；长期真实单调查询仍使用基础库宽时钟，不把100Hz称为FPS。

Post只允许USER（ID>=8000）或COMMAND，拒绝硬件/lifecycle ID与伪KERNEL/REPEAT等flags。公开来源flags接受0或APPLICATION；桥清除flags、epoch、sequence和tick，再由kernel生成本次APPLICATION来源/队列顺序，旧时间戳不作为授权或顺序。库核对本地目标/代数，kernel再核实际owner/会话。window0仅用于process级USER消息，应用可在调用Dispatch之前自行处理；此切片没有注册线程过程，Dispatch此类消息返回NOT_SUPPORTED。

Dispatch先复制完整消息，核对本进程过程记录与代数，再对普通窗口消息确认实际kernel窗口仍活。CREATE只由创建事务同步调用，手工Dispatch CREATE拒绝。过程在普通Ring3栈运行，回调返回状态按其ScStatus合同传给应用。

QUEUELOSS为process级事实，Dispatch把同一损失/epoch通知广播给当时的ACTIVE窗口。遍历时新窗只插链头，回调销其它窗不会产生悬空next，本次也不追逐回调不断新建的窗口。广播不制造KEYUP/按钮释放细节；应用重查InputSnapshot并撤其控件拖动/选择状态。

## 6. PAINT、矩形提交与输入

WindowInvalidate登记真实客户损伤。Get PAINT只领取投递机会，不确认损伤；PaintBegin取得当前geometry/paint token及损伤。应用据此绘制自己的私有像素，用WindowSubmit提交一批1..64个矩形，成功后才PaintEnd确认。其间新invalidation会保留新的pending；旧geometry/token返回STALE，不能把旧缓冲或旧ack当成功。

窗口层不会为每个app分配8MiB framebuffer，PaintInfo没有kernel像素指针。Submit携用户源bytes/width/height/stride和24B矩形表；库先验证本人只读输入，kernel检查完整源、所有乘加/边界、格式和epoch后一次发布损伤。直通ARGB/alpha与VGA设备回退保留；只绘局部并不许可暗降分辨率。

隐藏PaintBegin/Submit返回AGAIN；损伤仍有窗口归属，恢复后的真实PAINT请求完整重画。失焦、被另一窗口覆盖、最小化和隐藏会话分别按实际状态判断，后台模型/音频/网络/CLI工作不由本库冻结。

InputQuery是精确window的96B快照，不消费旧键鼠沿，物理键/修饰/按钮的真实事件由新raw输入源提供。PointerCapture是正常客户坐标的绝对捕获，失焦/隐藏/故障/退出真实撤销并发CAPTURELOST；不能拿旧冻结指针的相对模式代替编辑器选择或拖放。库不从轮询快照合成KEYUP、MOVE、FOCUS或CLOSE。

## 7. 静态交接与验证出口

本子任务只改自己的GUI源、用户规范及既有owned public include/aggregate/manifest/fini接线。SC32GUI.H、SC32GUIWIRE.H由内核GUI代理拥有，键鼠原始驱动由主代理拥有，不并发覆盖。

基础65+GUI20个声明/定义/导出在静态清单中匹配，但不据此宣称运行通过。实际include头已静态数出340个不同宏，尚有GUI/FILE源宏和预定义；超过原SCCC320宏槽，主代理按真实首编诊断处理容量并验证，不删除接口或隐藏warning。

统一定向出口：真实SCCC caller→GCC/SCCC SCL过程ABI与栈；两个同进程窗口的CREATE/KEYDOWN/KEYUP/按钮/FOCUS/MOVE/SIZE顺序；CLOSE拒绝后再关闭单窗；回调重入销本人/owner子树、构造拒绝out保持；隐藏后台推进和恢复PAINT；stale几何/坏rect/只读out无消费/无部分提交；正常静默fini与异常owner兜底、slab/索引/队列/画布真实暖页回收及原尺寸截图。达到这些具体条件结束本轮，不重跑无改动旧矩阵或称整个Phase 2完成。

## 修订

2026-10-14：收齐同SC32API.SCL的20个实际GUI过程/消息/paint/input桥、动态页内metadata/hash、CREATE两段激活/回退、可重入销毁/静默fini及公共接线；源码静态阶段，实际编译/Windows双窗口输入/绘制与回收由主代理统一验收。

2026-10-15：主代理统一构建、实际客体生成的库/公开应用与三窗口P0运行通过；首Shell背景表一页单列，暖PF及GUI池精确恢复，证据见evidence/m10a2-sc32；完整平台继续。
