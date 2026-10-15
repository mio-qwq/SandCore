# SC32API v1 基础库

所属版本为 **SandCore M10a2 — The Application Platform Era**。公开入口是完全大写的 `user/SC32API.H`，纯类型来自 `user/SCTYPES.H`，独立库源为 `user/sc32/SC32API.C`，清单为同目录 `SC32API.json`。

**当前状态：主代理已统一构建，并实际执行公开文件/typed-query应用、客体生成的根库和85导出SC32库，以及客体编译的三窗口应用。** 证据与范围见 [m10a2-sc32](../evidence/m10a2-sc32/README.md)。原65个基础函数保留，同库新增20个消息窗口入口与过程生命周期见 [SC32-GUI-USER.md](SC32-GUI-USER.md)。SCUI、剪贴板、通用IPC、共享映射与完整平台仍按 `SC32API-DESIGN.md` 继续；本次实际出口不代表整个Phase 2或M10a2完成。

## 1. 动态链接与依赖

2026-10-15增量已随R1/SCCLIB统一构建、客体生成并实际执行，见[m10a2-stdlib](../evidence/m10a2-stdlib/README.md)。新增真实ScCallbackPin，原85入口保留，ABI1/minor1/library_version2、86 exports/6 ROOT imports，要求ROOT1.1/version2。回调只在自动应用运行/prepare期间固定实际主正文或READY/X模块，库和依赖闭包直到进程终止；不是对任意可读指针宣称CFI/NX。R3代码固定能力、Guard私有static回调与事件/model/cache回调已运行；不新增kernel gate。此前三窗口证据仍对应9bf6d8e的1.0/85入口版本。

SC32API是Ring3代码，依赖只有 `SCLDR.SCL`，不导入SCCLIB/libc。六个真实导入为 `ScApplicationRuntime`、`ScRuntimeOpen`、`ScRuntimeClose`、`ScRuntimeResolve`、`ScRuntimeQuery`、`ScApplicationPinCallback`。当前清单ABI主版本1、次版本1、library version2，init为空，fini为不公开的 `Sc32Finalize`；公共函数一律cdecl。1.0历史五项导入保留其原含义。

主编译输入是 `SC32API.C`，其源片段按桥、错误/验证、系统、进程、路径、身份/会话/主题/库、文件职责分开。宿主编译须明确32位C（大写`.C`不能被驱动程序误当C++），客体由SCCC `--scl`加同一清单生成。这里只记录接线输入；具体命令、产物摘要和真实编译/执行另由主代理记录。

私有桥在 `sc32_bridge.c` 保存EBX/ESI/EDI/EBP，所有软中断和寄存器处理留在SCL正文。普通应用包含SC32API.H并导入函数即可，不包含SCAPI.H、不复制桥、不静态包含本库正文。SCLDR及SC32API仍无Ring0/SYSTEM权限，文件和会话能力依调用者真实凭据执行。

库装卸使用 `ScApplicationRuntime` 返回的同一个启动上下文。手动装入SC32API而没有自动应用上下文时，库操作返回NOT_SUPPORTED，不偷偷创建另一棵runtime。正式系统名称固定从受保护 `/SYS/LIB/` 获取；第三方名称由同一runtime的显式包目录解析，不能CWD/PATH劫持系统库。

## 2. 类型、记录与输出

`ScU32/ScI32/ScStatus`固定32位；`ScWideCounter`为lo/hi两个u32。指针为i386本人虚址；公开结构按4字节对齐，公共头没有内联函数或内核私有结构。私有静态条件数组核对各记录尺寸，不要求用户复制断言或处理寄存器。

调用者先填写记录的`size=sizeof(record),version=1`。输入描述必须是精确v1尺寸且保留字段零；输出可声明规范尺寸到65536B，完整声明范围必须可写。成功只写规范v1前缀、size报告规范尺寸、未知尾部保持；参数/版本/映射错误不改输出。输出record不接受未初始化size/version。

| 记录 | v1字节数 | 核心内容 |
|---|---:|---|
| ScErrorInfo | 32 | 稳定code、domain、raw、operation |
| ScCapabilities | 64 | 库ABI/内核合同、实际cap位、路径/参数/页/时钟边界 |
| ScProcessRef / ScProcessInfo | 16 / 96 | PID+代数；状态/身份/会话/统计/名字副本 |
| ScProcessPage / ScJobInfo | 64 / 48 | 分页游标/同次PIT分类；受管作业状态/退出结果 |
| ScMemoryInfo / ScMonotonicClock | 64 / 32 | 真实全局PF及本人私有页；原子lo/hi/PIT频率 |
| ScUtcClock / ScCpuInfo / ScStorageInfo | 48 / 64 / 80 | UTC日历；客体状态采样；卷/位图空间 |
| ScPathStatInfo / ScPathInfo | 16 / 48 | 类型/大小；UID/GID/rw/对象目录代数 |
| ScFileOpenDesc / ScFileInfo / ScIoResult | 32 / 48 / 32 | 打开模式；object_id和revision；实际前缀计数 |
| ScDirectoryEntry / ScDirectoryPage | 96 / 32 | 直接子项元数据/64B名字；cursor/epoch/FINISHED |
| ScIdentityInfo / ScSessionInfo / ScSessionPage | 32 / 80 / 32 | 真实凭据；独立登录会话及分页 |
| ScThemeInfo / ScDisplayInfo | 128 / 64 | 24个已登记角色；全机显示模式/预览回退 |
| ScLibraryDesc / ScLibraryInfo | 32 / 64 | 版本约束；同一runtime的逻辑字节/引用统计 |

旧快照先放足量私有栈临时区，再转换公开输出。16B ProcessRef不能直接传给旧32B SELF，64B CpuInfo不能直接接旧256B CPUINFO。转换前使用私有CORE范围Probe验证最终out，直接typed查询由内核预验完整输出。字符串逐页探查并在NUL结束，避免每字节陷入，也不会因为NUL后的下一页缺失而拒绝字符串。

没有状态消费的字符串查询，容量不足可以写`length`（正文长度，不含NUL）后返回RANGE，正文保持不变；ErrorName/Format的`required`则包含末尾NUL。可用NULL/0先查询长度。`length/required`与正文输出区重叠会拒绝；分页头/rows、IO result/buffer重叠亦拒绝，避免不同输出互相覆盖。

## 3. 错误域

0为成功，1为PENDING，2为TIMEOUT，3为RESIDENT。SC32负错误固定从-101开始：

| 值 | 名称 | 含义 |
|---:|---|---|
| -101 / -102 | INVALID / VERSION | 参数/尺寸/映射拒绝；记录或库版本拒绝 |
| -103 / -104 / -105 | ACCESS / NOT_FOUND / EXISTS | 权限；明确缺失；排他创建碰撞 |
| -106 / -107 | MEMORY / NO_SPACE | 内存；磁盘/相关空间不足 |
| -108 / -109 / -110 | STALE / AGAIN / BROKEN_PIPE | 寿命不匹配；非阻塞重试；无读者 |
| -111 / -112 / -113 | IO / NOT_SUPPORTED / BUSY | IO；未支持/无应用runtime；对象状态忙 |
| -114 / -115 / -116 | RANGE / CANCELLED / FAILED | 范围；取消；后端不能可靠细分的失败 |

`ScErrorQuery/Name/Format`不覆盖之前的失败诊断；普通成功也不清除last-error。没有用户线程/TLS，last-error归本进程SC32实例，所有调用本身仍返回状态。文本不会输出密码、令牌、文件正文或内核地址。

domain1为公共参数/范围预检，domain2为新内核服务，domain3为历史syscall或STD端点适配，domain4为SCLDR。raw保留其来源；例如通用旧FS的-1保持FAILED+raw=-1，不猜ENOENT/EIO。CREATEEX的-8已明确为碰撞才映射EXISTS；SPAWN2明确的-2/-3/-4映射缺失/容器非法/内存；旧环境查询-2明确是缺少变量。SCLDR的NOT_FOUND仍包含文件不可读取的粗边界，不改称精细权限诊断。

新FILE结果有有效typed写回时，错误诊断的raw来自FILE记录的实际驱动/端点结果；前置拒绝不写记录。库临时reply使用raw=INT_MIN哨兵区分两类失败，不能把初始化的零值冒充已传输。`operation`为稳定函数族诊断编号，具体定义在私有SC32PRIVATE.H；它不要求应用调用内部selector。

## 4. 已收齐函数与语义

### 系统与内存

`ScSystemCapabilities/Memory/Monotonic/Clock/Cpu/Storage/Wait`与`ScMemoryAllocatePages/FreePages`均有真实实现。Wait继续只接受原八类事件及100Hz粒度超时，TIMEOUT bit与状态分开；它不是有序窗口消息队列。公开枚举INPUT/VISIBILITY/STREAM/JOB/NETWORK/AUDIO/IMAGE/AUTH，不借旧bit扩展新消息。

页分配是图形堆0x40000000..0x43FFFFFF中的清零整页，不是malloc；1..64MiB，失败输出不变，只允许精确块首释放。模块映像0x50000000..0x53FFFFFF仍由SCLDR拥有，不能用此FreePages卸库。

Memory记录的process_total_pages为本人全部US映射正文（SCX、BSS、普通/异常栈、图形堆、模块）；heap/module字段分别计这两个窗口。page_table_pages另计本人PD和已存在私有PT，不含内核栈/旁表/历史池，不与全局used_bytes重复相加。实现按本人现存私有PT计数，不扫其它任务。

Monotonic来自PIT ISR真实低字回绕进位并原子读取，不由应用偶尔查询推算；UTC来自有界RTC稳定快照，更新中AGAIN，当前RTC范围2000..2099。CpuInfo仍是客体PIT分类，graphics是kernel子集，不能当FPS、硬件周期或宿主CPU。

### 进程与受管作业

`ScProcessSelf/Query/Enumerate/Terminate/Spawn/Exit/Yield/Sleep/Arguments/Environment/SetEnvironment/WorkingDirectory/SetWorkingDirectory`以及`ScJobQuery/Wait/Close/Cancel`均有函数正文。

Self输出PID/代数；Query(NULL,out)查询本人，显式Ref须16B/版本1。新Query只承诺活对象，退出或无可验凭据为STALE；不拼旧STATUS和新代数。本人允许；普通同UID还须同会话；root只管理普通目标；SYSTEM目标要求外部串口资格。Terminate先按新Query边界检查，再以旧KILLGEN2同次核对代数结束，不能把一个PID当永久引用。

Enumerate一页1..64行，页cursor用FFFFFFFF开始/结束；本页状态与CPU来自同一次旧PROCESSPAGE，realm不可由该快照取得时标UNKNOWN，不制造NORMAL值。旧ABI允许的跨会话同UID明细在新副本中进一步隐藏；PID/UID等公开基本信息不当作访问能力。跨页不冻结任务；检查cursor，不按count=0猜结束。

Spawn返回作业票据而非PID，command路径最多63B、参数1023B；streams=NULL继承当前存在的0/1/2，显式三项是本人现有历史流描述符适配，-1表示无连接。它们不是现代ScFile ID。flags bit0仅不继承终端/标准流，**仍是父任务拥有的可等待作业**，不能改称完全脱离父寿命。

JobQuery不领票；JobWait只在完成时用WAIT2领结果，先检查最终out，未完成timeout=0返回PENDING，有限期限到时返回TIMEOUT且保留票据。等待用JOB事件及动态超时，不空转轮询。JobClose只释放已完成票，运行/暂停时BUSY；Cancel按真实孩子代数结束但不擅自消费票据。父退出仍按原受管链撤孩子。

Arguments完整1024B文本视图，环境/用户名不是授权。USER/HOME/PWD只读派生；cwd和新路径统一根语义。ProcessExit直接结束进程，由内核收资源，不承诺用户fini；main正常返回才由启动runtime按真实顺序清理SCL。

### 路径与元数据

`ScPathResolve/Stat/Metadata/Create/Remove/Rename/Permissions/Read/Replace`都有实际后端。所有相对输入只结合cwd一次，再把绝对路径送旧根接口；根为`/`，最大63B名字+斜杠+NUL需65B。旧RESOLVE仍根相对、64B，两者分别调用，不改变旧ABI。

Create用首次发布即指定六位rw的CREATEEX；rw是u/g/o各两位r/w，不是Unix rwx。Permissions的uid/gid=-2表示保持。CORE/AUTH/RECOVERY/LIB和MOD资格继续由内核检查，库不能用root绕过保护。

PathRead/Replace是明确的路径调用子集，每次最多64KiB；更大文件按offset分块读或使用现代File对象，不把此子集当stdio。旧READAT发生IO失败可能已写部分且没有前缀数，所以PathRead使用库私有64KiB复用缓存，完整成功才发表调用者正文与transferred；失败不猜计数。该缓存惰性申请16页，归SC32实例，正常fini释放；异常由内核收私有页，首次使用后的暖态差额必须计缓存，不能记成无泄漏已测结论。

PathReplace的旧FSWRITE失败仍有原FS持久化边界，不承诺所有物理掉电/扇区撕裂回滚。不能以后台快照/路径操作替代现代更新流。

### 普通文件与目录

独立FILE内核切片的正式协议见 `SC32-FILE.md`、私有 `SC32FILEWIRE.H`。本库已按该协议写完 `ScFileOpen/Read/Write/Seek/Tell/Flush/Close/Query/Truncate/Duplicate/GetStandard`和`ScDirectoryOpen/Next/Close`，接入与实际执行由主代理验收；未接入时返回NOT_SUPPORTED，不伪造行为或置能力位。

OpenDesc flags R1/W2/CREATE4/TRUNCATE8/APPEND16：r=1、w=14、a=22、r+=3、w+=15、a+=23。create_mode只在首次创建用六位rw。a+从读游标0开始，写始终受控真实EOF；w成功open即截断。Seek支持SET/CUR/END和有符号偏移，合法EOF外定位及零洞由真实内核FILE完成，旧END_REPLACE不因此获得seek/read。

每次Read/Write最多65536B，zero bytes允许NULL。ScIoResult包含实际transferred、位置、长度、独立object_id、最新revision和状态flags；IO失败有有效typed结果才发表前缀。object_id是打开对象身份，revision是磁盘目录版本，不混作同一个generation。普通File内部COW迁移/零洞/metadata分步的AGAIN/raw=-10且transferred=0由私有桥让出并同请求续调；不重放非零前缀。STD管道raw=-6继续向调用者返回非阻塞AGAIN。

Flush完成发布并保持打开和游标。Close在内部续进中保持句柄，最终成功或真实flush失败均释放句柄；希望重试durability应先Flush，错误时保留句柄，再决定重试或Close。Dup共享打开对象/游标；GetStandard借本人已有端点能力、没有UART设备名入口，也不占旧fd新槽。

FileInfo在STANDARD标记下另给endpoint_kind和endpoint_capabilities，区分旧FILE、父Shell替换流、管道、串口、TTY、空流与内存。只有真实SEEK/TELL/FLUSH能力才设对应位，不能因为Query.position=0就把管道当可ftell；父替换流的PARENT_PUBLISH说明目录发布仍归父Shell，孩子不抢其commit权。普通现代文件这两字段为0，定位/刷新按普通文件ABI解释。

DirectoryNext批量1..64条，整个rows声明范围先验RW，成功只写实际count条；rows无需逐条预置头。ScDirectoryPage携cursor/epoch/FINISHED，按FINISHED判断结束，不能只看空页。内容或权限版本变化STALE，不消费游标；重开获得新视图。新对象的所有者、引用、回收与失败原子性仍需真实内核证据。

### 身份、会话、主题、显示和动态库

`ScIdentityQuery`返回真实UID/GID/realm/管理位/任务代数/会话；普通SYSTEM服务不因UID=-1获得模块管理。`ScSessionEnumerate/Control/Path`沿用登录会话隔离，page cursor明确，SYSTEM显示必须外部授权；会话和UID不是同一编号。

`ScThemeQuery/Apply/Path`转换原32字主题快照，24个命名角色以SC32_COLOR_*提供，Aurora/Classic共享已登记布局。临时/保存/重读分别0/1/2，重读忽略path；名称保持文本，真实非空资源路径改为绝对路径。`ScDisplayQuery/Apply`保留全机显示设备、缩放/支持位图、15秒预览/确认/取消，不能把主题切换当硬件模式变化。

`ScLoadLibrary/GetSymbol/FreeLibrary/LibraryQuery`只用同一个启动runtime。GetSymbol指定函数/数据及最小尺寸，首次取得地址固定库闭包；FreeLibrary释放显式引用，保留映像返回RESIDENT。LibraryQuery是整个当前runtime的逻辑映像/元数据和复用统计，不把逻辑字节当物理PF页。Load的init回调返回后重新Probe最终out；失效则撤此次外部引用，不盲写释放过的应用地址。

## 5. 私有内核接口与能力位

`SC32WIRE.H`定义私有统一gate0x300：EBX=operation、ECX=request、EDX=out；普通应用不传这些参数。

| CORE operation | 输入 | 输出 |
|---:|---|---|
| 01 | 严格20B Probe：size/version/address/bytes/access；out必须NULL | 无；bytes0不访问，access仅R1/W2组合 |
| 02 | NULL | ScCapabilities64 |
| 03 | ScProcessRef16，pid/gen全0为本人 | ScProcessInfo96 |
| 04 | NULL | ScMemoryInfo64 |
| 05 | NULL | ScMonotonicClock32 |

GUI100..1ff、FILE200..2ff、clipboard300、IPC400、shared500分别为私有operation域。旧调用功能号与向量不混用。CORE位只报告真实服务；现代DIR/UPDATE_FILES须FILE接入才置，WINDOW_MESSAGES/CLIPBOARD/IPC/SHARED_MEMORY保持0直到对应真实功能完成。保留未来能力常量不等于导出占位函数。

## 6. 交接与必要定向验证

基础静态核对：65个公共声明都有实际函数定义；同库新增20个真实GUI入口后manifest恰导出85项。5个导入都有真实调用；私有桥、助手、fini没有成为公共导出；已与CORE/FILE/GUI代理协调记录尺寸、错误域和协作文件例外。编译期尺寸断言仍待实际编译，静态一致不能写成客体通过。

主代理下一轮只做相关目标：

1. GCC引导与客体SCCC生成SC32API.SCL，检查独立SCL/五个真实导入；动态新SCX仅SC32API.H调用Self/Capabilities/Memory/Path及同runtime库操作，cdecl、只读输入、非法out/尾部护栏正确。
2. 普通身份/同UID不同会话/普通root与外部SYSTEM的Query/Terminate边界；分页与Job退出票、PID复用/超时消费；不额外重跑无改动网络、字体或M8。
3. FILE实际r/w/a及+更新、seek/tell/两次flush继续、内部续进同步、STD背压、目录cursor/epoch、只读out/输入与部分IO/Close语义；资源失败仅做具体必要回归。
4. 正常main返回、ProcessExit、异常退出的暖态PF/句柄/事务账，区分16页路径缓存、SCL逻辑字节和内核池。截图/真实输入、日志和实际来源由主代理登记。

同库消息窗口/输入/PAINT的源码已收齐，统一定向验证后继续SCUI与后续四库；不因基础函数多就申请完整M10a2验收。

## 修订

2026-10-14：收齐SC32API基础切片65个实际函数、公共/私有版本记录、独立cdecl桥、稳定错误/安全输出、同runtime库操作及现代FILE桥；静态核对完成，构建/客体执行/资源验收交主代理，未宣称Phase 2完成。

2026-10-14：保留原基础入口，在同库接入20个真实GUI过程/消息桥、公共SC32GUI.H、manifest与静默GUI fini；详细生命周期和未验证出口见SC32-GUI-USER.md。

2026-10-15：主代理统一构建、实际客体生成的库/公开应用与三窗口P0运行通过；首Shell背景表一页单列，暖PF及GUI池精确恢复，证据见evidence/m10a2-sc32；完整平台继续。


修订：2026-10-15，统一stdlib-build-06及guest02/04/05实际SCCLIB/SCLIB/R1/C1..C5、九模式/当前G1/253项通用工作流与精确回收通过；保留三项验证失败、未变输入复用与证据边界，见evidence/m10a2-stdlib，完整目标继续。


## M10a2 SCUI平台增量（本批未验证）

库保留已发布86入口，ABI1.2/version3共130实际导出：Font2、Palette1、Popup/PaintCancel/Cursor3、Broker31、Drop7。公共FONT/POPUP/PALETTE头可由SC32API包含，BROKER/DROP显式包含以避免类型循环；详细参数/错误/记录见对应规范。旧SCAPI.H SHA256仍81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1。

新增功能均有真实R3桥、内核服务、owner/session回收与构建规则，非声明占位。Font、弹出窗和鼠标状态来自已有真正系统资源；Clip/IPC/Shared/Drop是独立动态对象及权限合同，不把原流/malloc/窗口列表改名。新WHEEL21来自PS/2真协商包，INPUT|STREAM组合泵靠原子BrokerWait；没有预留而未生产的Broker GUI消息。

本批尚未构建/运行。后续只测改动实际影响的新平台和SCUI、必要原ABI兼容/退出；P3未受影响的原source与证据保留，不重跑无关旧网络/游戏矩阵。

修订：2026-10-15，保留86/增44正式函数和真实平台接线、版本及未验证边界。
