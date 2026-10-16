# SC32 typed debugging v1

2026-10-16。公开入口 `SC32DEBUG.H`，实现位于 SC32API 1.4 / release 6；引导 ROOT 1.2 / release 3 使用自身 freestanding 私有登记桥，依赖图不增加 SC32API→ROOT→SC32API 环。此文描述已写实现，宿主检查与客体证据分别记录。

## 对象、权限、生命周期

`ScDebugLaunch` 是唯一新公开目标建立入口。描述符含完整旧 streams 命令与三个现有描述符，-1 表示空设备。内核在同一个 IF=0 陷入中实际创建 streams 作业、建立真实 owner 并在首指令前暂停；失败终止并领取本次作业。返回的 `ScDebugRef` 含 PID、进程 generation、binding；不是可猜 PID 附加接口。

每次操作核对真实 caller、owner/owner generation、目标 generation/binding、同 UID 与同 session。没有普通 root 接管其它目标、SYSTEM 身份转换或 Ring0 调试入口。最高 SYSTEM/COM1 的外部硬件边界沿原合同。

`ScDebugQuery` 只在真实暂停时置 `REGISTERS_VALID`；运行期间寄存器记录明确不可用。退出/槽回收可使引用返回 STALE，最终退出码由原生 `ScJobQuery` / `ScJobWait` 领取。调试器退出时，既有 owner 清理链终止专属目标；新增断点/模块旁表按一个对象一步回收，没有任务总量或断点数量宏。

控制的 `stop_generation` 必须与当前暂停完全相同。Continue/Step 只接受暂停；Pause/Terminate 允许活目标。暂停使用真实 saved_esp，Step 打开真实 TF，#DB 后停下。INT3 命中恢复原字节、回退 EIP、下一次执行原指令后重插；在当前 IP 新建断点先进入待重插状态。删除待重插断点取消该状态。模块释放使相应断点失效，PID/模块复用不能沿用原字节。

## 记录与错误

所有公开记录以 32 位小端、4B 对齐，先填 `size/version=1`。输入大小精确；输出可比规范大，但整个声明范围必须可写，成功只写已定义前缀、报告规范 size、未知尾部保留。错误不改普通单记录输出。批量读/枚举在同次陷入内先验全部调用者范围和目标代数，正常成功属于一个暂停代数。

主要尺寸：Ref 24B，LaunchDesc 32B，LaunchInfo 48B，Registers 96B，Snapshot 160B，ReadInfo 32B，Breakpoint 32B，Module 204B，Segment 32B，Symbol 168B，Page 32B。Module.source 是 65B 绝对完整路径；name、表格省略文字只作显示。

返回域沿 SC32：INVALID=-101，VERSION=-102，ACCESS=-103，NOT_FOUND=-104，EXISTS=-105，MEMORY=-106，STALE=-108，IO=-111，BUSY=-113，RANGE=-114。能力位是 `ScCapabilities.capabilities_hi & SC32_DEBUG_CAP_HI`。私有 0x900..0x91F selector 不进入公共 SDK；旧 SCAPI.H / 0x50 / 0x51 / 0x228 的寄存器合同不变。

`ScDebugRead` 每次 1..4096B，分别输出 data 与逐字节 readable。readable=0 时 data=0，GUI显示 `??` / `????????`，不把它当作真实零。输出两数组及结果记录不能重叠。`ORIGINAL_CODE` 还原已登记的新/旧 INT3 原字节用于解码，实际执行仍由 CPU 与内核断点状态控制。

Breakpoints/Modules/Segments/Symbols 按最多64条一页枚举，批大小不是总量上限。Breakpoint/Module 的 `after=0` 从头开始，随后使用上一页 cursor；expected_epoch 非零必须匹配。Segment/Symbol 的 cursor 是下一条索引。FINISHED 是唯一结束依据。所有页都核暂停代数；完整 GUI 候选发布前再核 pause/module/breakpoint 三个代数。

## 真实模块登记

主 SCX 由可信加载器在实际复制及校验同一事务中登记，记录完整容器（含图标）的 SHA256、实际载荷/BSS 范围、源 revision 与分配大小。SCX 没有独立段权限，公开主段如实是原用户可读写、可执行映像；不会虚构 `.text`/`.data` 页隔离。

固定 ROOT 由 `scl_boot` 在真实 root_copy 完成、首条三环指令之前直接登记同一已验证文件及真实映像；随后 Adopt 仅借用该精确内核登记。ROOT 此时已经执行并修改 loader 全局状态，不能在 Adopt 时把它的 BSS 再错误要求为零。

其余默认 SCLDR 库在重定位与保护完成、init 之前登记。内核另行打开来源文件并按64KiB步骤读取、hash、验证 SCL 文件与 loader 提交的元数据；核对真实 MODULE 分配、段范围与当前 RW 页。接着按64KiB步骤逐段比较原始正文/BSS及所有ABS32/PC32重定位，导入反查已经登记的真实同进程依赖。合法 INT3 用其原始字节参与核验。登记期间 MODULEPROTECT 改变 mapping_epoch 会使候选失败。来源文件变化、坏映射、坏 relocation、OOM 均撤销候选；PENDING 期间 ROOT 真实让出 CPU。

来源只读打开也遵守native文件层非零调用身份合同，使用当前任务代数，不放宽文件层校验；无CREATE/TRUNCATE/APPEND，不进入写事务。释放只读引用仍传writer=0，避免取消其它句柄的真实pending。权限、lease冲突、真实映射和来源核验均保持。首次客体执行发现的旧writer=0会先返回FS_N_INVALID，再被总括为ROOT的SC_SCL_IO=-11；其修复不涉及缺失导入或ABI放宽。

内核独立保存已验证元数据和来源摘要；模块视图来自该快照，不扫描 SCLDR 用户堆。init 前显示 INITIALIZING，成功后 READY。源 SHA 是装载时文件身份，不代表可写 data/BSS 永远不变。每次 Segment 查询从真实页表取 R/RW；MIXED 明示混合权限，X 仅是 SCL 的逻辑段声明。当前非 PAE 没有 NX，不宣称完整 W^X。

ROOT 不为自定义 ScRuntimePlatform 伪造来源登记；没有经过默认来源/映射核验的模块不会出现在受验证清单。普通应用即使直接调用私有 gate，也只能登记自己映像，必须通过相同文件/映射/重定位核验，不获得跨进程或内核权限。正常释放前撤登记和对应断点，MODULEFREE、任务异常/退出与槽回收亦撤销。登记不是授予符号地址的读写权限。

## Debug2 界面与符号

默认启动：`/APPS/DEBUG2.SCX --launch /absolute/TARGET.SCX [arguments]`。Studio 启动 Debug2，由 Debug2 创建被调试目标；不先启动目标再猜PID。SCL 使用真实 SCX 宿主加载，不能直接执行 SCL。

反汇编显示真实 i386 指令/操作数/ModRM/SIB/分支目标；未实现的 SSE/FPU、地址宽度前缀或坏/截断编码只消费首个字节显示 db。TF 单步不依赖解码器猜指令长度。寄存器、代码、内存、栈、断点、模块和符号属于同一个暂停副本，运行后统一撤现场可用性。

SCL exports 来自内核保存的正式元数据。补充 `.sdbg` 使用原生工具链唯一 `SCDBGFILE.H` 的 SCDB1MIO v1，路径是完整映像名追加 `.sdbg`。只有装载时完整文件 SHA256/大小、SCX VA/SCL RVA 地址域、所有表/字符串/对象/源文件及每条 symbol/line 的实际模块段范围都通过才发布。TU本地同名符号保留 object_index；源路径和FNV只是来源提示，不授权打开文件，也不声称DWARF级信息。没有 sidecar 时仍显示真实内核导出符号；错误明确说明。

菜单/工具栏/表格/PropertySheet/文件对话框/右键菜单/Splitter/Dock 均为真正 SCUI 对象；Classic/Aurora 同一树，颜色和字体取框架角色。F5继续、F4暂停、F8单步、F9加断点、F2重启、Ctrl+G代码地址、Ctrl+M内存地址、F7跟随分支。地址支持 eip/esp、最多8位十六进制、精确 symbol 或 module!symbol；同名不同地址明确拒绝歧义。

隐藏时绘图 timer 暂停，无目标时不保留采样 timer。运行目标5 ticks查询状态，暂停25 ticks查询，候选按1 tick有限步骤推进；关闭回收使用后台1 tick。目标后台工作不因 GUI 隐藏暂停。模块 epoch 不变时复用 immutable 模块/符号/sidecar，单步不反复读盘。以上结构不是整机性能结论，实测仍待客体。

修订：2026-10-16，新增typed调试、真实模块核验登记、SCDB1MIO消费及SCUI Debug2合同；旧调试入口及SYSTEM边界保留，当前仅有宿主定向证据。

修订：2026-10-16，修正来源只读打开的非零调用身份；真实原生SCLD已执行并生成ROOT/SC32API/SCCLIB/SCLIB/SCUI，Debug2交互仍待统一GUI验收。
