# SC32API 基础合同与源码审计

对应 **SandCore M10a2 — The Application Platform Era** 的 Phase 2。本文是可落地的合同建议和缺项清单，**不是 SC32API 已实现或已验收声明**。本次只读审计与文档编写，没有构建、客体编译或 QEMU 运行。完整范围仍以根 `M10a2RULE.MD` 为准；Phase 1 完整出口后再实施这里的接口。

审计于 2026-10-14 开始，工作树 HEAD 为 `d43bdd10ee859a72e120eed6eebec6a4f95d0cef`。Phase 1 的动态 SCX、引导根和库目录保护正在并行修改，所以本文用函数名定位源码；这些在途源码的出现不代表运行通过。`user/SCAPI.H` 当前 SHA256 为 `81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1`，与开工冻结值一致。

## 1. 不可变边界与引导依赖

`SC32API.H` 只公开 cdecl 函数声明、常量、明确尺寸的类型和版本化结构。它不包含 `SCAPI.H`、内联软中断、寄存器约定或内核头，不要求应用复制 `sc_call`。系统库正文独立保存在 `/SYS/LIB/SC32API.SCL`，运行期装载、解析和调用；不能把旧 `.inc` 原样复制进应用后称为新平台。

依赖顺序为 `SCLDR → SC32API → SCCLIB → SCLIB → SCUI`。SC32API 底层保持 freestanding：不用 `malloc`、`FILE`、`errno`、`memcpy` 等 SCCLIB 导入；元数据用自己的小型页内分配器或直接复用 SCLDR 的引导能力，有限的私有复制函数不作为标准 C 导出。窗口控件、菜单、布局、文本编辑和文件操作工作流留在 Ring 3。

主代理已确定 Phase 1 引导根为无外部依赖的 `SCLDR.SCL`，拟导出 `ScApplicationStart` 与 `ScApplicationRuntime`。运行时接纳借用的根实例并固定至进程退出；SC32API 的 `ScLoadLibrary` 等必须使用 `ScApplicationRuntime` 返回的同一上下文，不能偷偷另建 runtime，导致一进程有两份系统库 BSS、两套引用和不同的析构顺序。这是接入合同，实际出口仍按 Phase 1 证据登记。

SC32API 私有桥可用独立 `sc32_call.asm`，或库实现内的私有汇编桥；保存 EBX/ESI/EDI/EBP，整数/指针按 i386 cdecl 传参。SCLDR 保留独立的最小引导桥，避免装载 SC32API 先依赖 SC32API。桥函数不得成为普通应用的必需导入或公共 SDK 入口。公开回调同样采用 cdecl，内核不会直接跳到用户窗口过程。

系统库保留名称必须固定从 `/SYS/LIB/` 获取，第三方库只从显式绝对包目录获取，不搜索 cwd/PATH，不回退到同名用户文件。审计起点的 `fs.c::protected` 尚未含 LIB；并行 Phase 1 已补入该目录及祖先操作保护，当前仍须随 Phase 1 核对实际构建/运行版本。root 绕过普通 rw 不等于可替换正式系统库。目录受保护也不使 SCL 获得 Ring 0 或 SYSTEM 资格。

## 2. 公共 ABI、错误与对象

- 字节序为小端；`ScU32`/`ScI32`/指针均 32 位，公共记录按 4 字节对齐。不公开 C 位域、编译器枚举宽度、宿主 `long` 大小或内核地址。
- 不按值返回结构，也不以变参作为基础 API。宽计数用 `{lo,hi}` 两个 u32，避免要求当前 SCCC 尚未支持的 64 位整数返回。普通当前卷偏移可用检查过的 32 位值；不能因此削减 SCCLIB 最终标准语义。
- 可演进公开记录以 `size,version` 开头；v1 必须列出精确偏移和字节数。查询调用者先初始化这两个字段，输入 size/版本/保留位错误在分配和修改前拒绝。输出只写该版本的定义范围；更大缓冲的未知尾部保持。固定数值对如ScWideCounter属于明确尺寸的值类型，不按宿主结构填充解释磁盘记录。新增字段或含义用新版本/新导出，不能扩写老缓冲。
- 状态和结果分离：函数返回 `ScStatus`；字节数、位置、句柄、退出码写独立 out 参数。成功为 0，等待/超时等非错误状态有具名常量；负值为稳定 SC32 错误。不能把合法指针或 u32 tick 转成有符号数后当错误。
- 最小错误域包括 INVALID、VERSION、ACCESS、NOT_FOUND、EXISTS、NO_MEMORY、NO_SPACE、STALE、AGAIN、BROKEN_PIPE、IO、NOT_SUPPORTED、BUSY、RANGE、CANCELLED、OPERATION_FAILED。v1 数值在正式头落地时统一分配，不与 `SC_SCL_*` 原值混用。
- `ScErrorInfo` 保存 `size,version,SC32 code,backend domain,raw result,operation id` 和保留字段。旧 syscall 的 -1 常把路径/映射/IO混在一起，桥只能报可证实的粗错误并保留 raw；不能凭猜测把它细分成 ENOENT 或 EIO。SCCLIB 自己维护标准 `errno` 并做明确映射，SC32API 不依赖它。
- v1 错误状态为每进程 SC32API 实例所有；当前没有用户线程/TLS合同，不宣称线程局部错误。所有有状态 API 同时返回错误，调用者不必依赖全局 last-error。错误查询和错误文本格式化不覆盖原错误，不写密码、令牌或私密正文。

建议的基本声明形态如下；这是设计样例，所有实际类型和导出仍须写入正式头与 SCL 清单：

```c
typedef int ScStatus;
typedef unsigned int ScWindow;
typedef unsigned int ScFile;
typedef struct { unsigned int size, version, pid, generation; } ScProcessRef;
typedef struct { unsigned int lo, hi; } ScWideCounter;
typedef struct ScMessage ScMessage;
struct ScWindowDesc;
struct ScFileOpenDesc;
typedef ScStatus (*ScWindowProc)(ScWindow, const ScMessage *, void *);

ScStatus ScProcessSelf(ScProcessRef *out);
ScStatus ScWindowCreate(const struct ScWindowDesc *desc, ScWindow *out);
ScStatus ScMessageGet(ScMessage *out, unsigned int timeout_ticks);
ScStatus ScMessageDispatch(const ScMessage *message);
ScStatus ScFileOpen(const struct ScFileOpenDesc *desc, ScFile *out);
ScStatus ScFileRead(ScFile file, void *out, unsigned int capacity,
                    unsigned int *transferred);
```

新公共整数类型需与 `SCLDR.H` 已有 `ScU32/ScModule` 协调，避免当前 SCCC 的重复 typedef 边界；可用共同的大写纯类型头，或使用无冲突类型名。不得为此改旧 SCAPI 类型或字节。

复用旧快照时，旧精确缓冲只能位于库的足量临时区，再转换到新结构。例如建议16B的 `ScProcessRef v1`不能直接传给旧32B PROCESSSELF。最终out范围在消费状态/分配之前通过新的typed-query内核入口验证，或由SC32私有的本人buffer-range检查能力先核验。仅在Ring3取旧快照再盲目写out，会把非法/只读out变成#PF，不能登记为“整区拒绝”。新创建入口把结果out纳入完整预检；range检查不代替内核每次实际访问时的权限检查。

对象身份必须区分三个概念：

| 身份 | 合同 |
|---|---|
| 进程引用 | `PID + task_generation`，管理操作在同一内核临界区核对；不能先查代数再调用旧 KILL2 |
| 资源句柄 | 非零 opaque ID，内核/库表检查资源种类、实际 owner PID/代数、存活状态；不能把 fd/窗口/作业/库号互换 |
| 状态代数 | visibility、geometry、theme、clipboard、input epoch 等用于缓存/迟到事件；它们不代替对象寿命代数 |

现窗口、作业、会话、截图和文件事务已有单调编号或代数绑定。新对象继续动态分配，不设置新的全局固定任务/窗口/端点上限。可以采用动态的进程能力表，将旧句柄封装为正式对象；不得只给旧裸 fd 换 typedef 就宣称具备新生命周期。编号域耗尽明确失败，不能回绕接管旧 ID；类型/owner/代数核对在内核，库前置检查不能作为安全边界。

正常关闭对象先撤销订阅、捕获、排队回调和相关引用，再释放正文；进程异常退出由内核释放其对象，不执行可能损坏的 Ring 3 回调。库函数/数据地址和注册的窗口过程、定时回调不得在仍可被调用时卸载；沿用 SCLDR 的地址固定及依赖闭包合同。

正式公开接口族至少保留以下范围；具体参数以相应版本结构承载，不能用“已有一个查询函数”代替整族完成：

| 接口族 | 建议入口与完整职责 |
|---|---|
| Process / Job | Self、Query、Enumerate、Spawn、Exit、Yield、Sleep、Arguments、环境/cwd查询与设置，受管Job的Query/Wait/Close/Cancel，带代数Terminate |
| File / Directory / Path | Open/Read/Write/Seek/Tell/Flush/Close/Query/Truncate/Duplicate/GetStandard，目录Open/Next/Close，Resolve/Stat/Create/Remove/Rename/Permissions |
| Memory | AllocatePages/FreePages/Query，CreateShared/OpenShared/Grant/Map/Unmap/Close；页、小对象与模块映像归属分明 |
| Window / Paint | Create/Destroy/Query/SetTitle/Move/Resize/Show/Minimize/Maximize/Restore/SetOwner/SetModal/Invalidate，PaintBegin/End与Surface/Submit |
| Input | QueryKeyboard/QueryPointer/Capture/ReleaseCapture/QueryCapture，精确窗口资格、修饰/重复/通断与原始事件；输入法不自动加入本轮 |
| Message | Get/Peek/Post/Dispatch/DefaultWindowProc，同进程Send与明确重入，用户消息来源、完整窗口消息和取消；跨进程使用受控IPC |
| Timer | Create/Change/Cancel/Query，GUI与后台语义、missed计数、epoch与单调deadline |
| Theme / Display | Query/Apply/GetPath，显示Preview/Confirm/Cancel和真实变化消息，统一角色与会话/全机边界 |
| Clipboard | Begin/Offer/Commit/Abort/Open/Formats/Read/Close/Clear/Query，文本/文件类型、快照寿命、权限与会话隔离 |
| IPC | Create/Listen/Connect/Accept/Send/Receive/Query/Close，连接授权、真实发送者、复制或授予的数据对象、背压与退出通知 |
| System / Errors | QueryCapabilities/Memory/Cpu/Storage/Clock/Wait，ErrorQuery/Name/Format，稳定错误域、raw后端诊断和版本范围 |
| Library / 现有服务 | LoadLibrary/GetSymbol/FreeLibrary/LibraryQuery，同一启动runtime；正式映射异常/调试、图像、音频、认证和已批准网络接口 |

除不返回的`ScProcessExit`等明确例外，入口遵循前述status/out约定；英文短名按`Sc`前缀归属相应接口族，正式导出表统一拼写。表是完整职责建议，不是声明已生成这些导出。

## 3. 真实已有能力与复用限制

下表的“已有”指已核对实际分发和函数源码；沿革文档中的旧运行记录不自动证明新 SC32API。SYS编号均为十六进制。

| 领域 | 真实源码/旧调用 | 返回与生命周期 | SC32API 尚需承担或新增 |
|---|---|---|---|
| 进程 | `task.c::task_page/task_spawn/task_stop`；22D SELF、22E KILLGEN2、240 PROCESSPAGE | SELF恰32B；KILLGEN同次核对代数，陈旧-8；分页16字头+16字行，FFFFFFFF起止 | `ScProcessSelf/Enumerate/Terminate/Query`；新按代数查询，不能用旧 STATUS 查询已复用 PID |
| 作业 | `streams.c::streams_exec/wait/job_snapshot`；218、219、23A | SPAWN2正票据，父/子代数绑定；WAIT2完成消费票据，JOBINFO只peek；父退出撤受管孩子 | `ScProcessSpawn/JobQuery/JobWait/JobClose`区分EXEC异步与受管作业；闭环保留退出码与取消 |
| 环境/路径 | `userspace.c::userspace_query/resolve/chdir/env_set`；70、72、73、21C、224/225 | RESOLVE输出根相对名，根为空；args新1024B；env写时复制；USER/HOME/PWD不能伪造身份 | 所有新路径API统一cwd语义；解析一次后传旧根接口时加显式根斜杠，避免再次解析叠加cwd |
| 身份/会话 | `auth.c::auth_info/auth_can_mod`、`session.c::session_control/page`；200、242/243 | UID/GID及realm是内核凭据；普通SYSTEM登录拒绝；同UID两次登录仍两会话 | `ScIdentityQuery/SessionEnumerate/SessionControl`仅暴露副本；系统对象不能借同UID跨桌面 |
| 内存 | `paging.c::private_alloc/free/module_protect`；100/101、280..282 | 图形堆/模块区各64MiB，页清零、失败回滚、精确块首释放、退出释放；模块RO/RW无NX | `ScMemoryAllocatePages/FreePages/Query`、后续带能力的共享映射；页分配不是malloc；图形堆暂无公开protect/query |
| 文件元数据 | `fs.c::fs_metadata/fs_permissions/fs_create_ex`；208/209、239及30..38 | rw六位；目录r穿越、w创建；新对象代数；CREATEEX -8存在/-6忙/-4资源/-5权限 | 正式结构、排他创建、目录完整枚举、错误域；保护目录继续经过内核gate |
| 字节流 | `streams.c::streams_open/read/write/seek/close/ready`；210..217、247 | read/write最多64KiB，-6重试/-7断管/-8陈旧；旧fd每进程16；dup共享游标 | 全功能普通文件对象、动态新句柄、seek/tell/保持打开的flush；不能把替换事务当stdio更新流 |
| 窗口 | `wm.c::win_create/owned_window/wm_user_info`；28、11、19、25、23C | owner PID/代数，单调handle，canvas独占；WININFO恰20B；定位只本人原生窗 | 新消息窗口创建/销毁模式，真实handle焦点、标题/owner/模态属性、完整生命周期 |
| 绘图 | `wm_native.inc::wm_user_frame_rgb/fill_rgb/text_rgb`；29..2B | FRAME32精确cw×ch×4，整区校验/原子发布；相同帧仍扫描全幅，改变行才局部合成 | 私有绘图上下文/脏矩形、带geometry epoch提交、矩形源验证及原子发布；保留整帧行为 |
| 输入 | `keyboard.c::keyboard_isr`、`wm.c::wm_key_input`、`wm_native.inc::pointer_snapshot`；14、17、22、77/78、81/82 | GETKEY/PEEK读当前顶窗字符队；17仅held状态；pointer沿为OR汇总；相对捕获有生命周期 | 真实KEYUP/修饰键/重复标记、目标窗口和有序鼠标按钮事件、普通绝对捕获；不能靠快照推导所有边沿 |
| 等待 | `task.c::task_event_wait/task_notify/wait_poll`；241 | 仅8类bit合并，消费匹配bit；FFFFFFFF无限；40000000超时；动态定时堆 | 作为睡眠基础可复用，不能当消息队列；新队列就绪与等待须原子协调 |
| 可见性 | `wm.c::visibility_changed/wm_user_visibility`；244 | 恰8字；可见查询消费repaint，声明整个任务visibility-aware；可见为当前会话且未最小化，不按失焦/遮挡判断 | 新窗口独立无消费状态查询和PAINT确认；隐藏停画同时保留后台事件 |
| 主题/显示 | `theme.c::theme_info/theme_load`、`wm_session_repaint`；20/21、2C..2E | 主题32字角色/代数，显示32B全机；主题按会话快照；保存失败不切快照 | `ScThemeQuery/Apply/Path`、`ScDisplayQuery/Preview/Confirm`、真实变更消息，复用已登记角色 |
| 时间 | GETTICK、`rtc.c::rtc_snapshot`、`task.c`等待堆；02、22F、241 | PIT100Hz/u32回绕；CLOCK2恰32B，-6更新中；没有独立消息定时器对象 | 用户态最小堆定时器与消息泵、宽单调查询；RTC与单调时钟分开，不称100Hz为FPS |
| 剪贴板/通用IPC | task/wm/streams现接口未提供此类broker/公共协议 | 管道和图像代理有各自能力，不能当通用会话剪贴板 | 必须增加真实跨进程对象、会话隔离、复制/替换/释放与IPC就绪；纯应用字符串缓存不满足 |
| 动态库/系统信息 | `SCLDR.H`、SIMDINFO、CPU/存储快照 | 已有库句柄/引用/地址固定和版本拒绝；旧8/32行快照固定；PF统计不能当FPS | `ScLoadLibrary/GetSymbol/FreeLibrary/LibraryQuery`用同一启动runtime；系统能力/资源/错误查询 |

旧 MONITOR=192B、CPUINFO=256B、PROCESSINFO2=2080B、CPUINFO2=800B、STAT=8B、WININFO=20B、POINTER=24B、VISIBILITY=32B继续保持。特别是 GETKEY 的全部入参、POINTER 的 EDX、WINCONTROL 未列寄存器、YIELD的全部入参仍忽略；不能用这些空位开启新消息、扩展焦点或添加输出。

新`ScPathResolve`建议返回带根斜杠的正式路径，根为`/`；旧RESOLVE的根相对表示只作为内部适配。SandFS名字最多63B，因此根斜杠+名字+NUL的新完整路径缓冲最多65B，不能沿用旧64B容量偷偷截断。所有相对输入只结合cwd一次；已规范化结果不再作为相对输入送回旧RESOLVE。

## 4. 新窗口与消息合同

### 4.1 为什么现有快照不足

`task.c::task_notify`仅保存 `events |= events & 255`；同类事件会合并，没有窗口、时间、顺序、payload或计数。`win_raise`仅通知旧/新owner的INPUT bit；普通鼠标拖动直接写x/y。两次WININFO之间的往返移动、焦点往返或尺寸变化可以完全消失。

`keyboard_isr` 的普通断码和方向断码只更新held，不进队列；Shift单独更新，Ctrl/Alt没有完整事件与公开状态。快照轮询即使100Hz也可能漏掉一个tick内的按下/松开，无法正确实现KEYUP、快捷键和拖放取消。

`wm_user_visibility` 的focused取 `wm_focused(pid)`；`pointer_snapshot`也按pid判断。因此同一进程A/B两窗口，B在顶时A的上述快照也可能报告focused。WININFO检查具体顶窗，但没有同时纳入故障卡/系统菜单阻断，且仍只是当前状态。SC32API需要一次返回精确handle焦点、可见性和输入epoch的新快照。

`wm_native.inc::native_mouse`标题叉号和上下文Close直接 `task_stop(w->owner)`，VGA路径亦终止owner，随后全部窗口被清理。应用没有机会保存/拒绝，也不能只关闭第二个窗口。必须新建消息模式窗口；不能给现有WINRGB加隐藏寄存器选项，或通过轮询抢在任务结束前伪造CLOSE。

### 4.2 内核协调层

新增版本化消息窗口创建入口，其描述包含期望客户几何、样式、owner-window/modal关系和消息投递模式。窗口过程/应用context仅由SC32API实例保存，不放进内核可执行回调表。内核记录真实PID/代数、会话、窗口对象代数、geometry/input/visibility epoch和消息队列关联，保持旧80B窗口前缀。

每GUI进程默认一个动态消息队列，多窗口事件带目标ID，不必开多任务模拟对话框。CLI/后台进程不因装载库就分配8MB画布或整套GUI队列。记录建议固定64B（16个u32，首字段size/version），包含message ID、target handle/代数、sequence、tick、flags及按message解释的payload。payload不能含内核地址或另一进程的用户指针；复杂正文用拥有者数据对象复制接口。

建议的`ScMessage v1`字偏移为：0=size=64，1=version=1，2=message ID，3=target，4=object generation，5/6=sequence lo/hi，7=PIT tick，8=source/合并标记，9=该类状态epoch，10..15=六字payload。KEY使用physical/symbol/text-scalar/modifiers/repeat/保留；鼠标使用client-x/client-y/buttons/changed-button/modifiers/保留；SIZE使用client-w/client-h/outer-w/outer-h/保留/保留。MOVE、FOCUS、CLOSE等未用payload字置零。按无符号双字比较sequence，不要求SCCC直接支持long long。

新`ScWindowInfo v1`建议96B/24字：size、version、handle、对象代数、owner PID/代数、session、outer x/y/w/h、client w/h、format、state flags、geometry epoch、visibility epoch、input epoch、theme epoch、display epoch，最后四字保留零。focused必须为该handle且输入资格有效，不能从owner PID推导。此结构与旧20B/32B输出分别调用。

主循环处理PS/2输入和WM几何时，在实际状态变更点产生事件。IRQ只记录原始事件，不分配复杂对象或调用用户过程。旧字符队列与新输入记录使用各自明确路由，新消息窗口消费新队列不会抢走另一窗口/旧程序字符；不得依赖两名消费者竞相读取全局字符队首。

队列用页内记录块按需扩容，不每事件分配一页、不全扫描任务。Peek/Get先验证完整输出范围和版本，再复制/消费；输出不足、只读页或非法payload不消费。批量提取用显式capacity，末尾护栏不写；处理预算是公平性参数，不是消息/窗口总数限制。

新等待入口在同一IF=0临界区检查队列非空并安装等待，非空必须立即返回；出队后仍非空必须保持可等待就绪。可复用现有稳定任务帧、就绪FIFO和动态超时堆。旧EVENTWAIT继续只接受原8类bit，新队列状态单独保存或通过独立新等待描述暴露，不能把第9类bit偷偷塞进旧合同。等待只保留可信内核帧，恢复用户输出时在其CR3重新校验。

programmatic create/resize/post须先保留记录及候选资源，再发布状态，失败回滚。设备输入在真实内存耗尽时不能假装永远无丢失：保留可查询溢出计数和QUEUELOSS记录，取消按住/拖动/capture状态，以新input epoch要求重新同步；CLOSE待办和PAINT损伤保持于现存窗口对象，不能因队列满静默丢弃。普通移动可合并，但必须明确合并规则；不得把队列溢出伪装成正常KEYUP。

### 4.3 消息及顺序

| 消息 | 产生位置与合同 |
|---|---|
| CREATE | 新窗口内核创建成功后，由SC32API在Ring3同步调用一次过程；拒绝则销毁候选并回滚其SC32资源。由创建返回事实产生，不由轮询猜测；创建期间不运行PAINT/输入回调 |
| PAINT | CREATE成功后、应用invalidate或内核resize/restore/主题/显示使画布需重画；窗口损伤保留到成功EndPaint，领取消息本身不清损伤 |
| SIZE | 完整新canvas分配/交集复制并发布后，携客户尺寸、geometry epoch；失败不发成功SIZE，相关PAINT随后发生 |
| MOVE | 实际外框位置提交时，携屏幕外框x/y；拖动中可合并连续同窗MOVE，但不能越过焦点/按钮/关闭等顺序屏障 |
| FOCUS | 实际窗口失焦/得焦两条记录，旧失焦先于新得焦；只同PID也须分窗；包含原因和input epoch，系统菜单/模态阻断另给明确输入资格 |
| KEYDOWN / KEYUP | 原始通/断码形成完整事件，携物理键、键符号、修饰快照、重复标记；文字标量可另发TEXT。不把ASCII编码当物理键ID |
| MOUSEMOVE | 内核实际命中/capture后的客户坐标、按钮/修饰状态；只合并同target/epoch且中间没有按钮/焦点/capture屏障的连续移动 |
| MOUSEBUTTONDOWN / UP | 必须新增，分别保存按钮、坐标、target和seq；旧pressed/released的OR集合无法给出多次点击的顺序 |
| COMMAND | SCUI菜单、按钮或快捷键在Ring3产生，携稳定command ID、control ID及通知码；无需内核控件树；不能伪装成系统输入消息 |
| CLOSE | 新窗口标题叉号/任务栏Close产生请求；窗口仍存活，过程可以保存/取消。默认过程销毁该窗，进程是否退出由Ring3应用策略决定 |
| DESTROY / CAPTURELOST | 销毁后Ring3一次性清理通知；取消所有相应排队回调/定时器/capture。失焦、切换、故障或销毁会真实撤capture |
| VISIBILITY / THEME / DISPLAY | 实际代数变化消息；隐藏仍可处理模型/后台事件，PAINT延后合并。恢复时请求完整重画 |
| TIMER / QUEUELOSS | Ring3定时器由消息泵投递；QUEUELOSS由真实计数/同步边界产生，不编造丢失事件细节 |

按同队列sequence保持不可合并事件顺序。先CREATE，后输入/PAINT；SIZE发布新geometry后才能产生对应PAINT；旧FOCUS-loss先于新FOCUS-gain；旧input epoch的迟到输入不能送进新焦点或新会话。完整键按下/释放在同一个消息泵周期到达也必须可区分。

### 4.4 Ring3窗口过程与SCUI

`ScMessageGet/Peek/Post/Dispatch`、`ScDefaultWindowProc`和`ScWindowDestroy`形成正式消息泵。Dispatch从本进程窗口表取得过程，只在普通Ring3栈调用；应用无需处理寄存器或返回EXCRETURN。回调所属SCL在可调用期间固定，过程变更/销毁核对对象寿命。

同进程同步发送可以直接分发，并明确重入状态；跨进程只允许受权限控制的异步用户消息/IPC，第一版不提供可能死锁的跨进程同步Send。用户Post只能使用用户消息/COMMAND等许可域，不能伪造CREATE、KEYUP、FOCUS或关闭另一个进程。内核标记事件来源，不能信任用户给出的source flags。

SCUI拥有控件树、control ID、Tab焦点、键盘导航、菜单、命中、捕获状态、布局与COMMAND传播。内核只协调顶层窗口、真实输入、可见性、owner/modal阻断和跨窗口目标。普通绝对鼠标捕获需要新入口：拖动仍提供客户坐标，失焦/切会话/退出撤销；不能借现81相对捕获冻结光标来实现编辑器选择或桌面拖放。

CREATE失败时SCUI撤销候选控件树和其引用；应用过程自己负责在拒绝之前释放额外创建的应用资源，SC32API不能猜测任意应用分配。main返回后的正常runtime清理须先停止分发并撤销窗口/定时回调，不能在相关SCL的fini/free之后继续调用旧过程地址。

模态对话框的父子/owner关系由新OS窗口对象约束输入不穿透，同进程嵌套消息泵允许无关模型/服务继续；SCUI不能仅画灰色背景却让父窗口仍收硬件点击。隐藏会话保留后台网络、音频、CLI、IPC、作业和声明为后台的定时器；不暂停整个进程以偷省绘制。恢复/主题/geometry改变使布局、字体、图标和像素缓存按所属epoch失效。

## 5. 绘图与资源寿命

正式接口需要 `ScPaintBegin/End`、`ScWindowInvalidate`、`ScSurfaceCreate/Destroy/Query`、`ScSurfaceFill/Text`、`ScWindowSubmit`。绘图上下文包含本进程私有surface、客户几何/geometry epoch、裁剪和损伤，不能暴露内核canvas地址。完整FRAME32路径可先复用，但它只是完整提交能力，不代表按需重绘已完成。

第一条有效优化路径是新版本矩形提交：描述明确source width/height/stride、总缓冲bytes、geometry epoch和矩形列表。内核先检查所有整数乘加、行范围、完整用户输入、owner/格式与epoch，再复制所有矩形并一次发布损伤；失败不写半幅、不清PAINT。geometry已变返回STALE/RETRY并触发新尺寸绘制，不能拿旧stride写新canvas。源缓冲支持只读SCL数据，不要求RW。

alpha仍为直通0xAARRGGBB，不能丢透明效果；局部修改后若原画布opaque未知，采用安全alpha路径或维护分块alpha统计，不能只看最后一块就宣布整窗opaque。VGA设备边界回退与旧FRAME保持。共享画布映射以后若实现，必须通过独立共享内存能力/提交协议；本期不能给出可写内核物理地址。

buffer按真实客户尺寸惰性申请，同一窗口重用；resize先申请候选再换旧资源，OOM保留原窗和旧帧。关闭/退出按owner链释放surface、输入订阅和库元数据。绘图timer隐藏时不执行布局/栅格/提交；背景模型更新只累积损伤，显示恢复一次完整重画。失焦但仍可见、被另窗遮挡与最小化必须区分，不能误暂停可见应用。

## 6. 文件对象与SCCLIB标准I/O底座

### 6.1 已有流实际缺项

`streams_open`只接受READ、WRITE、NULL、TERMINAL四个独立mode。READ创建END_FILE；WRITE创建END_REPLACE和预留固定capacity的顺序事务，不能组合READ|WRITE。`streams_seek`只接受END_FILE/END_MEMORY，位置必须不超过现有长度；没有tell；END_REPLACE的write只走 `fs_stream_write` 的 `used+offset` 顺序尾部。

`streams_close(fd,1)`提交后关引用；提交失败保留描述符，最后一次普通close取消未提交事务。0x238 `ata_flush`只刷新设备，不提交事务，也不把库缓冲送入文件。它不能直接实现 `fflush(FILE*)`；把fflush做成close/reopen会改变FILE状态、失败重试和更新流读写交替。

已有流可用于标准输入/输出、管道和有限的只读底座，但不能宣称满足fopen、fseek、ftell、fflush的完整约定。不能用整文件应用RAM缓存规避文件系统权限/并发/刷新；也不能公开 `fopen("r+")` 却只读或只追加。

### 6.2 必须新增的普通文件合同

新增动态普通文件对象，与旧16 fd及替换事务分别处理。建议 `ScFileOpen/Close/Read/Write/Seek/Tell/Flush/Query/Truncate/Duplicate/GetStandard`，`ScDirectoryOpen/Next/Close`，以及 `ScPathResolve/Stat/Create/Remove/Rename/Permissions`。标准流适配仍只借本人已有端点，不能按路径打开任意UART。

| fopen模式需要的能力 | 新底座要求 |
|---|---|
| r / rb | 存在普通文件、只读、游标、EOF；每次按真实主体查r |
| w / wb | 创建或在成功open时截断；不能拖到close才让截断生效；初始化mode在首次发布前确定 |
| a / ab | 创建或打开，所有写入在受控append位置；seek不能使后续写覆盖中部 |
| r+ / w+ / a+及b变体 | 同一对象真正读写、可定位、读写交替和flush后继续；a+读位置与强制append写位置明确分开 |
| seek/tell | SET/CUR/END、负偏移和溢出检查、独立位置查询；普通二进制文件支持合法EOF外位置，读取返回EOF、写入缺口清零 |
| flush/close | flush发布已接受写入并保持句柄和当前位置；close按合同清理；flush失败有可查询错误与重试/放弃路径 |

普通文件数据是字节流，允许NUL与任意字节，不拿GUI的UTF-8校验限制二进制FILE。文本模式的换行与定位支持范围由SCCLIB明示；其`fread/fwrite`元素计数、EOF/error、ungetc、缓冲、更新流定位规则和`errno`属于Phase 3，不能因底座尚缺而悄悄缩减声明语义。

低层 `ScFileWrite` 通过out给出已接受长度，dirty写入何时发布必须明确；`ScFileFlush`把这些写入完成目录发布而保持打开，SCCLIB的fflush先送其用户缓冲，再调用该能力。未接受部分不能被计数为成功。参数/映射失败没有写入；实际IO失败允许有明确前缀计数，SCCLIB按真实已传字节处理部分元素和ferror。

采用内核控制的磁盘COW工作区/分页extent元数据实现随机写和动态增长，不要求应用预知整个文件最终上限，也不为大文件预分配整份RAM。首次w截断、首次创建、append定位和每次发布均经过权限/保护gate。候选增长/写盘/提交前保留旧正式数据；超级块提交结果不明继续使卷进入故障只读，不声称磁盘回滚完成。

新打开对象需要自己的文件寿命身份与当前位置；磁盘现有generation因修改/rename/chmod变化，不能直接拿它当永远不变的对象ID。新句柄在本人成功写入/flush后刷新版本并保持有效，权限变化每次复核；同名删除重建绝不能接管旧对象。新接口的rename/delete及与旧接口替换发生冲突时明示STALE/BUSY，并更新其关联链。旧END_FILE仍按原generation失效，不改变旧read/seek返回。

append写和发布需在同一协调链中决定真实EOF，不能由多个应用各读一次size后覆盖。新对象允许动态句柄数量，旧fd3..15及旧SPAWN2继承合同保持；标准0/1/2适配不继承另一个身份的私密文件。新关闭路径不能把未提交事务空间留到重启。

目录使用显式分页/对象游标和返回结构，区分结束、输出不足和目录版本改变；旧FSDIR容量满时截断成功，不能据此保证Files 2已枚举完整目录。v5父目录显式存在可利用，避免每页重复旧列表的全表回看去重；不以固定192/512/8192行数组截断应用显示。

## 7. 剪贴板、IPC与共享内存

### 7.1 系统剪贴板

需要按登录会话所有的真实broker对象。推荐首版用小型内核协调层管理不可变数据正文和代数；以后可迁移受保护Ring3服务，但必须先有可靠IPC、服务寿命和权限委托。SC32API每进程一份静态字符串或SCL BSS不共享，不能满足跨应用复制粘贴。

正式接口建议 `ScClipboardBegin/Offer/Commit/Abort/Open/Formats/Read/Close/Clear/Query`。Begin/Offer只构造候选；验证全部format、长度/偏移和资源后一次Commit替换。读取句柄固定旧快照，容量不足不消费；旧正文到最后快照引用关闭才释放。发布后正文归会话broker，源应用退出不使文本内容悬空；候选归源PID/代数，退出自动abort，注销释放该会话全部内容与引用。

首批格式为UTF-8文本与文件列表，具有明确编号、版本、字节长度。文本严格验证标量和终止约定；文件列表是显式条数/路径长度/操作COPY或CUT/源对象generation等记录，禁止把用户指针放进正文。复制/读取限制为同一实际登录会话；同UID不同登录也隔离，普通root不自动读取别人的桌面剪贴板。系统管理权不从clipboard token产生。

建议文本v1正文为合法UTF-8加一个末尾NUL，bytes包含NUL，拒绝中间NUL；Read按bytes完整复制，输出不足保持快照。文件列表v1头为size/version/count/operation四个u32；每项为record_bytes/path_bytes/source_generation/flags及紧随的NUL路径，记录尾对齐4B。path_bytes包含NUL，各项整体长度/计数/偏移先核验再提交；格式编号与名称由正式头统一分配，不复用文本内容来暗示CUT。

文件列表只是意图，不授予正文读取或删除权。SCSHELL粘贴逐项检查当前权限、源generation、目标冲突和实际复制结果。CUT只有目标成功后才删除对应源；失败保留未移动条目并给逐项结果。清理CUT条目核对旧clipboard代数，不能把用户新复制的内容清掉。多文件失败恢复可逐项呈现，不谎称整个批次具有未实现的磁盘原子事务。

### 7.2 IPC与拖放基础

建议 `ScIpcCreate/Listen/Connect/Accept/Send/Receive/Query/Close`。内核识别端点owner PID/代数、会话和ACL，接收记录由内核附真实sender/UID/session，发送者不能自报SYSTEM。默认同会话，跨身份或后台服务必须有明确授予的能力；不借同UID数字作为任意跨进程读写许可。命名端点按会话注册，重名明确失败；正式系统服务名由受控注册保留，第三方SCL不能仅凭自报名称接管broker。

正文复制进独立队列或通过明确授权的共享对象传递。非阻塞AGAIN是背压，Read容量不足不丢消息，close/退出撤订阅并通知对端EOF/取消；没有通用“读任意进程地址”API。队列字节预算可控制单连接内存，不成为全局端点或任务数量门槛。等待和消息/流就绪统一协调，但保留旧EVENTWAIT mask。

拖放由SCSHELL/SCUI在Ring3完成会话内目标发现、Offer→Accept→Transfer→Complete/Cancel协议、预览与COPY/MOVE反馈；OS补真实窗口命中目标、绝对capture和带能力IPC。跨窗口不代表跨会话，目标接收时再验证文件权限；失败撤临时资源，不删除未传完的源文件。

`ScMemoryCreateShared/OpenShared/Map/Unmap/Close`属于完整系统对象目标的后续切片：页引用由内核持有，授予的读/写权限不得提升，用户只得到本人VA；退出先撤映射再减引用，最后归还物理页。现paging_free_task_dir把每个私有US页直接free，加入共享映射前必须改为区分私有/共享引用；不能把现USERALLOC地址数值传给别的进程当共享内存。现非PAE无NX边界保持。

## 8. 定时器、主题与系统查询

`ScTimerCreate/Change/Cancel/Query`可以先在SC32API的Ring3动态最小堆实现，使用消息泵等待最近deadline；到期在普通Ring3栈投递TIMER，内核不从IRQ调用用户函数。周期timer按原deadline推进，迟到合并并报告missed次数，不补发无限旧回调；Cancel增加timer代数，撤销已经排队的旧事件。精度为现100Hz PIT的可观察粒度，不能承诺1ms回调或真实高精度时钟。

关联窗口销毁自动取消其GUItimer，process级后台timer可继续；隐藏绘图timer停止生成画面，后台工作timer不因桌面切换停运。`ScProcessSleep/ScSystemWait`复用既有EVENTWAIT超时和事件检查，不用busy-loop或三环HLT。新的宽单调计数应由内核在一次快照中给出，不能只在低频应用查询时推断跨多次u32回绕；RTC用于UTC日历，不能作单调计时源。

`ScThemeQuery/Apply/GetPath`使用现24个命名角色与会话快照，Classic/Aurora共享行为和布局；新主题字段先登记GFX/THEME，不在SC32里散布裸色。`ScDisplayQuery/Preview/Confirm/Cancel`保留全机设备、15秒回退和真实支持位图；主题消息与显示消息分别失效颜色/字体/几何缓存。

`ScSystemQuery/QueryCapabilities/QueryMemory/QueryCpu/QueryStorage/QueryClock`返回无内核指针的版本化副本。能力位只对真正存在的接口置位；探针可区分库ABI、内核接口版本和未支持的功能。CPU采样仍为客体PIT状态，graphics是kernel子集；PF物理页、SC32元数据逻辑字节和宿主QEMU CPU分别记录。不得以公共Error字符串或系统信息泄露别的用户私密数据。

完整目标还包括对现有异常、调试、图片、音频、网络、认证与模块查询的正式cdecl API映射：沿用已批准硬件、权限与能力范围，按实际支持逐项登记。它们不要求本阶段扩大硬件、HTTPS、字体、输入法或重启M8任务，也不能因为此处优先窗口/stdio就从最终SC32API清单删除。

## 9. 必要的新OS接口与实施顺序

下列为能力清单，**尚未分配正式功能号**。主代理在Phase 1接口稳定后统一写入`task.h/SYSCALL.md`并实现；不能复用旧号或旧忽略寄存器。窗口完整控件系统不进入这张内核清单。

| 优先级 | 新OS能力 | 直接解决的问题 |
|---|---|---|
| P0 | API/系统能力版本查询、typed-query/本人buffer范围检查、带代数的进程/资源查询 | 不用旧STATUS猜复用身份；旧/新结构转换不扩写或盲写out；库判断内核实际支持 |
| P0 | MESSAGEWINDOW_CREATE/DESTROY/QUERY/ATTRIBUTES | 独立关闭模式、真实handle焦点、owner/modal关系、标题与寿命 |
| P0 | MESSAGE_PEEK/GET/POST/WAIT | 多窗口有序消息、来源与payload验证、原子队列就绪/睡眠、错误无消费 |
| P0 | 原始键通断/修饰/重复路由、窗口输入快照、绝对capture | 真实KEYUP/快捷键/多次点击、菜单和拖动正确性 |
| P0 | WINDOW_INVALIDATE/PAINT_ACK、带geometry的矩形提交 | resize竞态、PAINT不丢、隐藏恢复、避免完整1080p比较 |
| P1，Phase 3前 | FILE_OPEN/READ/WRITE/SEEK/TELL/FLUSH/CLOSE/QUERY/控制及目录分页 | fopen六种读写模式、定位/刷新后继续、完整目录与动态句柄 |
| P1 | 统一的新对象ready/wait、宽单调时钟 | 文件/IPC/消息背压无轮询，timer长期寿命正确 |
| P1，Shell前 | CLIPBOARD候选/提交/快照/读取/释放 | 真正跨进程文本和文件剪贴板、源退出与替换生命周期 |
| P1，Shell前 | IPC端点/授权/发送/接收/关闭、真实窗口目标查询 | 跨窗口拖放和必要系统服务协调，不伪造输入 |
| P2，系统对象完整出口前 | SHARED_MEMORY对象/授予/映射/引用回收 | 明确VA与物理页引用，支持共享映射；不可复用私有free规则 |

实施顺序：

1. Phase 1完整出口、固定引导根/同runtime与LIB保护后，落正式`SC32API.H`、导出清单、私有桥、统一状态/版本、进程/页/现只读查询。此为首切片，不称Phase 2完整。
2. 同批完成P0新窗口/输入/队列/paint协议，用一进程多窗口真实事件程序证明新应用只含SC32API.H即可完成创建、键盘释放、移动、缩放、拒绝关闭和保留后台服务。完整窗口过程在Ring3。
3. 补P1文件对象和新等待，形成SCCLIB可直接实现标准FILE的底座；按清单继续Phase 3，不能在缺失更新流时给`r+`装成功空壳。
4. 完成系统剪贴板/IPC及共享内存，SCUI/Shell阶段再接入其真实控件与拖放工作流。公共API、权限和资源出口仍计入完整SC32API，不因优先级而取消。
5. 现音频/网络/图片/认证/调试/模块查询逐项映射与SDK编译支持；受影响行为有定向证据，其它有效基线复用。

## 10. 资源预算与必要验证出口

预算用于实施和测量，不是未经运行的性能结论：

- 无GUI进程只装必要库/实例元数据；不预分配窗口、paint surface或GUI全屏像素。小对象在页内复用；队列记录建议64B、一次批量最多32/64条为服务预算，记录实际PF/逻辑字节。
- 静止窗口、纯光标移动、隐藏绘图timer不得持续layout/raster/FRAME32。典型表格只绘可见区。改变一块128×32面板应提交实际16KiB像素及必要损伤，不能仍扫描1920×1080的8294400B作为默认路径。
- surface随真实客户尺寸/数量计空间，分配失败保留原对象，不能通过降分辨率或删alpha提速。按页占用区分应用surface、内核canvas、队列、SCL映像/元数据和会话缓存。
- 文件I/O按4KiB或更大缓冲批量，单次底座≤64KiB；不每字符陷入或整文件RAM复制。大目录分页不反复生成整列表；记录COW发布和目录bank/ATA flush成本，必要时采用分步服务，不能让大PIO在IF=0阻塞其它输入而未测量。
- 输入入队/唤醒、资源撤销沿owner/订阅链；计时堆O(log N)，纯状态操作不遍历全任务。真正输入长尾受调度/磁盘/复制共同影响，记录客体tick及宿主观察p50/p95/最大值，不凭算法宣称优秀。

后续每轮验证只解决具体问题并有结束条件，不增加无价值排列组合：

| 必要出口 | 结束条件 |
|---|---|
| 私有桥/动态导入 | 新SCX无SCAPI.H/应用sc_call，实际调用独立SC32API.SCL；cdecl保留寄存器、只读库输入、非法输出整区拒绝；旧头SHA保持 |
| 多窗口/输入 | 同进程A/B焦点准确；真实通/断码含一次短按、Shift/Ctrl/Alt与按钮顺序；SIZE/MOVE/FOCUS不可由轮询猜；新Close可取消且只毁一窗，旧Close行为保持 |
| PAINT/隐藏/服务 | invalidation和EndPaint成功确认；geometry变更拒绝旧提交；隐藏零layout/raster/submit而后台任务推进，恢复/主题后重画；原生截图和真实键鼠证据 |
| 文件底座 | r/w/a及三种+模式真正读写；seek/tell、EOF外、append和两次flush后继续；标准流/权限/旧代数失效与坏缓冲；IO失败和候选资源账明确 |
| 剪贴板/IPC | 两独立进程互读文本/文件意图；源退出仍有已提交快照；替换旧引用安全；同UID不同会话/伪造发送者/越权拒绝；CUT失败保留源 |
| 寿命与成本 | 显式关闭、正常EXIT、真实异常终止的暖态PF/对象/队列/磁盘保留账收敛；相同画质/尺寸/输入对照实际提交字节和响应；失败回滚不泄漏 |

宿主静态检查、宿主构建、SCCC客体编译与Windows双盘QEMU执行分别登记。正式库/接口的实际范围与待办同批同步`M10A2-IMPLEMENTATION.md`、`SYSCALL.md`及相关WM/FS/SESSION规范。没有新的运行证据，不把本文的设计或某个旧探针PASS写成整个Phase 2、四库或M10a2完成。

## 修订

2026-10-14：完成SC32API只读源码审计与完整合同建议，记录freestanding引导/同runtime、旧ABI冻结、消息窗口/KEYUP/多窗口焦点/关闭、标准FILE底座、真实剪贴板/IPC/共享映射、资源与定向验证出口；本次未修改实现或运行验证。
