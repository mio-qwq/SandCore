# SC32 普通文件、目录与标准流底座

2026-10-15，**SandCore M10a2 — The Application Platform Era**。本批已统一构建；公开应用通过HOST与实际客体生成的SC32库运行更新/定位/零洞/flush/目录页/标准流继承，GUI日志实际反复写入关闭，证据见 [m10a2-sc32](../evidence/m10a2-sc32/README.md)。这是列明行为的实际证据，不把它称为完整stdio或Phase 2/3完成；六模式缓冲stdio的标准语义仍由下一批SCCLIB实现与验证。

实现入口为 `kernel/sc32_file.c/.h`、`fs.c/.h` 和 `streams.c/.h`。私有 wire 为 `user/sc32/SC32FILEWIRE.H`，公开 cdecl 入口与转换由 `SC32API.H`、`user/sc32/sc32_file.c` 承担。内核不使用 libc/浮点；SandFS v5 磁盘记录、CRC、双 bank 和旧 SCAPI.H 字节没有新增格式字段。

## 1. 门、范围与输出

统一门 SYS0x300：EBX=selector、ECX=本人请求、EDX=本人输出；实际 PID/代数来自可信陷入，不从用户记录读取。使用共同 `sc32_core_input/output`：输入为精确32B、size/version=32/1；输出先检查版本和完整声明 RW 区间，最小 FILE48B、DIRECTORY32B，最大65536B。成功仅写规范前缀，未知尾部保持。所有直接数据和路径也在分配、位置推进、端点消费、截断或写盘前完整预验；RO 数据可作为写入源，读取目标必须 RW，零字节 IO 允许 NULL。

| selector | 能力 | 请求 |
|---:|---|---|
| 0x200 | OPEN | size/version/path/flags/create_mode/reserved3 |
| 0x201 / 202 | READ / WRITE | size/version/handle/buffer/bytes/reserved3 |
| 0x203 | SEEK | size/version/handle/whence/offset_s32/reserved3 |
| 0x204..207 | TELL / FLUSH / CLOSE / QUERY | size/version/handle/value0/reserved4 |
| 0x208 | TRUNCATE | 同上，value为新长度 |
| 0x209 | DUPLICATE | 同上，value0 |
| 0x20a | GET_STANDARD | handle0、value0/1/2 |
| 0x220 | DIRECTORY_OPEN | OPEN布局，但flags/mode/reserved全0 |
| 0x221 | DIRECTORY_NEXT | size/version/handle/rows/capacity/reserved3 |
| 0x222 | DIRECTORY_CLOSE | HANDLE布局、value0 |
| 0x223 | PATH_QUERY | size/version/path/flags0/reserved4；独立48B ScPathInfo输出 |

FILE回复48B依次为 `size,version,handle,transferred,position,length,object,revision,flags,raw,endpoint_kind,endpoint_capabilities`。DIRECTORY回复32B依次为 `size,version,handle,count,cursor,epoch,flags,reserved`。所有字段32位、小端、4B对齐；只含本人地址和不透明编号，不返回内核指针。

前置参数/版本/映射/句柄/打开方向拒绝不改输出、不消费状态。进入实际后端后写规范回复并保留真实 raw；IO失败的 transferred 只表示确定完成的前缀。库临时回复使用 raw=INT_MIN 的未写哨兵区分两类结果；STD raw/domain来自旧端点，modern raw/domain来自新内核文件底座。READ/WRITE的数据范围不能与48B回复重叠，目录rows不能与32B页回复重叠。

路径原始输入最大127B加NUL，规范名字最多63B；每个请求只调用一次 `userspace_resolve` 得到rootless名，再直接交根内fs接口。续进仍发送原始请求，不将rootless结果再次套cwd。ASCII大小写按SandFS比较，显示拼写保留；不为二进制正文强加UTF-8/NUL限制。

## 2. 模式、位置与身份

flags：READ=1、WRITE=2、CREATE=4、TRUNCATE=8、APPEND=16；至少READ/WRITE之一，后三位须有WRITE。create_mode为首次发布的六位rw，0..63，无先公开再chmod窗口。

| fopen族 | flags | 正式行为 |
|---|---:|---|
| r / rb | 1 | 存在普通文件，初始位置0 |
| w / wb | 14 | 新建或在OPEN成功之前真正发布截断，初始位置0 |
| a / ab | 22 | 新建或打开，初始位置0，每次WRITE强制取真实EOF |
| r+ | 3 | 同一个对象读写、定位、反复flush后继续 |
| w+ | 15 | 真正读写并在OPEN时截断 |
| a+ | 23 | 初始读取位置0，读取按当前游标，写入强制EOF，写后游标为新EOF |

普通独立OPEN有各自描述和游标。DUPLICATE共享描述/游标，各别名各有单调句柄和关闭寿命；不占旧16fd槽。普通句柄、目录句柄和底层文件对象均动态分配，数量只受真实物理页、磁盘目录/空间和编号域约束。句柄和对象编号不回绕复用；耗尽明确失败。身份同时核对资源种类、owner PID和task_generation，旧PID或另一任务的编号不能接管对象。

文件 `object` 为活跃open-pin的独立寿命身份；`revision` 为现磁盘generation，内容/权限改变可变。chmod不撤modern身份，每次按真实subject和当前目录/文件权限重新检查。seek计算SET/CUR/END、有符号32位增量和u32溢出，允许EOF外位置；READ在或超过EOF返回0，不创建缺口；下一次WRITE才清零缺口。TRUNCATE不移动已有游标。PIPE/TTY/null的seek/tell依据真实能力拒绝，不能假报位置0成功。

## 3. 原位写、扩展和有界续进

当前v5只有单连续extent，保持原磁盘布局。现有范围内的小随机写仅处理实际涉及扇区，边界扇区RMW，最多64KiB caller数据；不复制整文件、不把整文件缓存到Ring3。成功前缀立即可见，内存revision推进，旧END_FILE因此按原规则STALE。已接受数据已经过实际ATA写/flush，目录revision的最终发布可由随后FLUSH完成。

真正增长时优先利用extent之后的相邻空闲扇区；相邻区不可用或增长写会覆盖旧正文时建立新extent。迁移、旧尾清理和EOF外零缺口每步最多128扇区；候选记录保留copy/zero进度和owner/writer键，不保留用户数据指针。仅因真实增长才迁移，不为每次小write复制旧正文。相邻空闲判断用512扇区摘要叶，仅边叶逐位检查。

长工作通过私有FS_N_AGAIN=-10/SC32_E_AGAIN续进，transferred=0；库在有效该回复时让出并以同请求继续，普通公开文件函数仍同步完成。STD pipe的旧raw=-6是实际非阻塞背压，不能当这个内部续进循环。其它writer与短期卷锁竞争也通过内部AGAIN让出，不能越过同文件未完成增长键；同句柄擅自换操作/参数仍BUSY。append位置在同一协调链中取真实EOF，不能让两个writer各读一次旧size后覆盖。

候选正文完成后，元数据使用现内核meta区构造完整快照。每步只写16扇区并对同一8KiB块续算CRC，超级块最后单独提交；只有短期metadata快照阶段冻结目录变更，读取/WM仍可调度。主循环 `fs_native_poll()` 推进已有metadata提交，不需要用户指针或借用户CR3；调用者停止续调也不能无限期锁住全卷。copy/zero阶段没有全卷metadata锁。

提交完成通过 `sc32_file_complete` 将最终48B整数结果及操作键留在原writer句柄，立即释放文件pending键。原调用下次通过完整输入/输出预验后领取缓存，不重放append正文；其它writer可在新EOF继续，不必等原用户ACK。缓存不含用户指针。已由先前有效请求启动的后台工作可继续完成，后续无效输出请求本身不启动新工作、不消费结果。关闭/退出撤结果缓存，只回收资源，不伪回滚已完成发布。

FLUSH发布所有已接受的当前目录revision并保持对象/游标；整个目录bank是卷级快照，可能顺带发布其它对象已接受的revision，不是单文件原子事务。未发布候选不进入dirents。权限在各续进和提交批次重新核对；普通/root/System三环对CORE/AUTH/RECOVERY/LIB、MOD和祖先操作仍沿现保护gate，poll不借kernel PID0权限替原owner写MOD。

## 4. 失败、close与退出

只计已成功完成IO的prefix。读取失败的当前扇区不复制到caller；原位写失败的当前扇区可能部分落盘，标UNCERTAIN并冻结卷，不能说未计数字节必然没变。独立未发布COW候选失败可撤销其空间、继续保留原正式extent；涉及原有扇区或超级块的结果不明仍按卷故障处理。CRC不是日志/签名，未承诺扇区撕裂或掉电原子性。

增长候选在metadata成功后才发布长度与引用，因此候选失败的已报告传输量为0；完整候选成功发布后一次报告真实长度。元数据结果不明时，内存旧目录只是最后已知视图，不能作为磁盘已回滚证明。续进权限失败撤该writer的未完成候选；FLUSH失败保持句柄，已知在超级块前失败可重试；结果不明的故障卷拒绝后续修改。QUERY可看到DIRTY/FAULTED/UNCERTAIN/GROWING。

CLOSE先取消该句柄未完成的未接受增长，再尝试FLUSH；内部AGAIN保持句柄继续，最终成功或真实flush失败后都销毁句柄。关闭失败不回滚已接受正文；调用者可以先显式FLUSH处理错误，再选择CLOSE释放。别名关闭只撤本别名，最后描述引用才撤open-pin。正常/异常进程资源清理沿本人链、每步至多32句柄，无用户回调或无界flush；未发布候选撤销，已发布文件不伪回滚，退出不代替显式durability保证。

活跃modern pin期间，旧整文件replace/remove/rename及同名旧顺序事务返回BUSY，祖先rename/remove也检查pin；不让旧同名替换接管modern对象。没有modern pin/metadata提交的旧路径保持原行为，旧16fd和事务本体保留。metadata快照短锁期间其它目录修改也BUSY，快照结束释放；只读旧访问继续。旧END_FILE仍检查磁盘generation，modern自己的写/flush/chmod不会把自己的新身份误当STALE。

## 5. 标准流与混合继承

首次GET_STANDARD把旧0/1/2的真实descriptor引用转移给动态native-ref；旧slot保留弱proxy，**不计endpoint refs/readers/writers**。proxy保留本任务控制订阅与默认继承路由；旧ready、读写和SPAWN仍可路由，child attach/显式old dup增加真实引用。没有使用新take的旧程序路径原样。

后续GET_STANDARD与DUPLICATE给同endpoint增加各自native引用，真实位置仍共享。每个native close真实减reader/writer；最后本PID标准别名关闭时先撤弱proxy/控制订阅，再撤最后真实ref。child或显式old dup仍持endpoint时它们可继续，但不能复活本PID已经关闭的标准slot；最后全局writer关闭才产生真实pipe EOF。没有新占旧16fd来实现modern duplicate，也没有隐形常驻writer。

STD回复flags含STANDARD；endpoint_kind为FILE1、PARENT_REPLACE2、PIPE3、SERIAL4、TTY5、NULL6、MEMORY7。capabilities为SEEK1/TELL2/FLUSH4/PARENT_PUBLISH8。ordinary modern文件的两个endpoint字段为0，普通文件能力由其正式合同决定。

FILE/MEMORY标准端点支持真实seek/tell，二进制EOF外读为0；PIPE/TTY/SERIAL/null无seek/tell并保留NOT_SUPPORTED/raw。TTY继续既有UTF-8文本协议和256B分块，不伪装成任意二进制设备。内存端点的新EOF外位置对旧别名read也有安全EOF守卫，避免原先不可达的无符号剩余长度下溢。

Shell父管理PARENT_REPLACE继续原顺序事务合同：真实tell取t.used；STDflush要求库buffer已交端点并刷新实际ATA，目录发布仍由父close/commit拥有。child不取得父事务管理权，不能偷偷close/reopen父描述符。其capabilities为TELL/FLUSH/PARENT_PUBLISH，无随机seek；这一继承边界不扩到modern ScFileOpen，后者拥有真实保持打开的目录FLUSH。

## 6. 目录与接线

DIRECTORY_OPEN建立本人动态对象和卷视图epoch；NEXT容量0..64，整个rows容量区先验RW，按实际count写规范96B `ScDirectoryEntry`，其它空间保持。每行有size/version/kind/length/generation/UID/GID/rw/name64；**name是直属leaf，如`DATA.BIN`，不含父目录或根斜杠**，内部rootless路径不作为行名称。EOF为FINISHED=1和cursor=FFFFFFFF，不以单独count0猜EOF，capacity0可查询当前页状态。

v5显式父目录直接扫描后续记录，只有直属条目进入结果，不为每页重新生成全列表或设192/512行截断。cursor跨页一次向前推进；内容/权限/删除等修改推进独立视图epoch，STALE不消费/输出，新OPEN取得新视图。旧隐式目录兼容读取仍按实际旧卷记录回看去重，仅保留其原规模成本，不作为v5的高频算法。

主代理整合：SYS0x300的0x200..2ff分发 `sc32_file_call`；退出清理接 `sc32_file_stop_step`；主循环接 `fs_native_poll`；构建增加sc32_file对象、SC32API真实File wrappers与对应库导出。FS内部调用同一对象的 `sc32_file_complete`，没有额外task字段或用户回调。新streams私有native链也接入现stop/stop_step，先撤真实端点引用再回收task_data。capabilities只在这些真实路径接通后置位。

后续只做有目的的定向构建/Windows双盘验证：六模式、EOF零洞、持续两次flush、append/别名/并发位置、权限/旧fd与pin冲突、目录分页/STALE/坏缓冲、stdio firsttake/default spawn/最后writer EOF，正常与真实异常退出页/候选空间账。大缺口/迁移需同时观察另一任务或WM推进和实际时延；记录典型规模、预算、发布成本与失败边界，不凭代码预算宣布性能达标。

静态冻结的结束条件：copy_sector到旧extent末且zero_offset到target才进入用户正文写入；metadata_sector到dir_sectors后下一步单独写超级块，成功或明确错误都释放volume snapshot锁。完成缓存只待原句柄领取，不再占file pending键。CLOSE的内部AGAIN保持对象，最终结果销毁本别名；stop_step在本人head为空时返回1。STD最后本PID native alias关闭撤weak proxy，最后全局真实writer退出使pipe EOF；父END_REPLACE仍由父commit拥有。以上为实现合同，不是未运行代码的PASS记录。

## 修订

2026-10-14：固定File/Directory wire、独立身份/版本、六模式和有界随机IO/元数据续进、失败prefix/UNCERTAIN、真实标准端点引用/弱proxy与旧父事务边界；本批源码/静态接线收齐后由主代理统一构建与客体验证。

2026-10-15：主代理统一构建、实际客体生成的库/公开应用与三窗口P0运行通过；首Shell背景表一页单列，暖PF及GUI池精确恢复，证据见evidence/m10a2-sc32；完整平台继续。

## 2026-10-16：精确路径元数据查询

新程序包含完全大写 `SC32PATH.H`，调用 `ScStatus ScPathQuery(const char *path, ScPathInfo *out)`；要求 SC32API ABI1.3、library_version5。库154项导出，原153项名称/参数/行为保持。`ScPathInfo` 为48B：size/version/kind/bytes、UID/GID/rw/flags/generation/reserved3，bytes是文件字节数，不是FD位置；generation为本次已提交目录对象代数。

调用前设置out.size/out.version；原始路径至多127B，按当前CWD仅解析一次，SandFS规范名仍至多63B，当前空格/冒号/反斜杠规则未改变。请求32B精确v1，flags/reserved全零；输出声明完整RW范围先验。成功只写48B前缀；任何失败完整保留输出和未知尾部。

查询只需要真实META/祖先遍历权限，不创建FILE/Directory对象、不读文件正文、不取得namespace或内容pin。先核可访问性再查名字，避免用NOT_FOUND泄露不可遍历目录的后代。可遍历目录内缺名返回NOT_FOUND；不可遍历为ACCESS；卷故障为IO；非v5为NOT_SUPPORTED。查询不提供跨yield稳定性，后续修改必须使用观察到的generation及lease条件提交，不能拿元数据快照充当权限票。

旧ScPathStat/ScPathMetadata依然走原始粗错误合同；没有把旧FAILED全域转换成NOT_FOUND。新Shell/worker迁移到精确入口，避免把正常新目标误判为IO，也避免把真实权限/故障当作可创建空位。统一HOST构建24、真实客体generation12的四个受影响组件已通过；普通用户实际COPY/MOVE正在定向验证，尚不记为事务PASS。

修订：2026-10-16，新增独立selector223/SC32PATH.H及错误、权限、输出和兼容合同，旧ABI/磁盘格式保持。


2026-10-16事务修复：私有候选允许Publish到本人预留的同一名字，仍要求目标实际不存在、当前权限、已flush revision和完整条件提交；已发布对象同名Rename继续INVALID。索引原地提交只撤提交期slot1，保留拥有者slot0与子树，不做无效搬迁。首次FILEJOB失败的WORK.TRACE实际定位为planning1/recovery2/active0，原FAIL保留；新CORE14进行最小同工作流回归，尚未写PASS。

worker诊断可由调用者本人设置SCSHELL_TRACE为普通可写路径，默认关闭；错误先冻结再写日志，日志IO失败不改变原始提交结果。不使用SYSTEM串口，不绕FS权限，不把日志当durable ACK。

2026-10-16实际结果：FILEJOB04通过普通UID1000的精确路径查询、131079B COPY/MOVE、持久ACK、客体逐字节和宿主取回内容对照；两份832B恢复日志的头/frame CRC与START/提交/FINISH独立审计通过。退出/注销13830→13830 PF，各池/历史/字体/未归因增量0，输入/原盘保持。证据入口evidence/m10a2-p5-file-jobs/README.md。完整P5、主要组件和SDK仍未完成，持续目标active。

## 稳定读共享打开

READ_STABLE保证来源正文/名称稳定，不要求稳定读者之间独占。内核私有STABLE_OPEN仅供FILE稳定读Acquire/OpenPinned，允许空闲且有效的slot0 READ_STABLE共存；write lease、namespace、slot1目标保留和PENDING/取消/清理/异常记录仍冲突。普通READ_OPEN原语义不变，不能用于取得稳定性，因为它也允许普通读取写租约对象。

各读者分别持本人FILE/游标、owner PID/gen、精确object/revision和lease强引用。原R或METADATA_ONLY的META权限、精确版本、native pending、别名与ref溢出检查保持；写Acquire/OpenPinned与typed rename/remove/set-mode的唯一绑定不放宽。已有普通WRITE描述符的后续Write/Truncate/Flush仍由lease_native_guard检查读租约，不能绕过。没有新增公共调用、header布局、磁盘格式或权限。

旧fs_permissions仍按真正owner/管理者和原保护gate改权限，后续IO重验当前权限；READ_STABLE不屏蔽旧权限撤回。typed lease SetMode在存在其它稳定读时仍因unique/TREE冲突失败，不能把两者混称“所有chmod都被锁住”。本组仅隔离源码方案，尚未构建或客体验证。

修订记录：2026-10-16，稳定读共享打开与原权限/alias/writer边界合同；旧ABI及普通读取行为保持，待统一整合验证。
