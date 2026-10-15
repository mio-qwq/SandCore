# SC32 Broker：剪贴板、IPC与真实共享页

所属版本：**SandCore M10a2 — The Application Platform Era**。本合同落实 [SC32API-DESIGN.md](SC32API-DESIGN.md) §7，普通应用只调用正式 cdecl SC32 API，不处理软中断、内核指针或跨任务用户地址。

**本批真实源码已接入SC32API/task/main/session/paging/Make，尚未统一构建、未运行。** 新源码按成功初始化登记Clip/IPC/Shared能力位；当前已验证P3产物不含本批服务，旧Foundation/Stdlib/SCLIB证据不覆盖新行为。下文初稿必要接线沿革已完成的项，以末尾当前接线记录及源码为准。

## 1. 文件、ABI和接线

公共头 `user/SC32BROKER.H`，接口均为SC32API.SCL正式新增函数，片段 `user/brokerapi/sc32_broker.c`只共用现SC32PRIVATE/bridge，freestanding无SCCLIB依赖。应用显式包含独立BROKER公共头；SC32API.C已组合片段，正式json已合并31个导出并统一ABI1.2/version3，避免BROKER/API/DROP类型循环。小整数常量为同名enum，FFFFFFFF游标保留unsigned宏，避免新SDK轻易挤满384项CPP表。


`user/brokerapi/SC32BROKER.exports.json`为31项真实新增export的合并片段，只有exports一键，供Root追加正式九字段库清单；它不是可传给--scl的完整manifest。本片段无R3持久分配/context/callback，资源由kernel owner链及Close/EXIT收束，不新增SCCLIB/ROOT依赖。

内核 `kernel/sc32_broker.c/h`，私有字布局 `user/sc32/SC32BROKERWIRE.H`。统一SYS0x300保持 `EBX=selector, ECX=request, EDX=out`，Clipboard为300..30e，IPC400..40b，共享500..509。私有Error.operation分别b00/c00/d00起；不占FONT的600/601或a00/a01，不改变旧SCAPI原字节/忽略寄存器。错误沿SC32 -101..-116，文本/协议非法为INVALID，版本不合为VERSION，权限ACCESS、代数或已消费对象STALE、预算/背压AGAIN、对端关闭BROKEN_PIPE、溢出RANGE、OOM MEMORY。

公共记录均32位、LE、4B对齐：Identity48，ClipboardDesc32/Info96/FormatInfo16/Page32/IoResult32，IPC EndpointDesc64/ConnectDesc32/Info64/Packet80，ReadyInfo32/WaitDesc32，SharedDesc32/Info64/GrantInfo32/MappingInfo48。固定输入必须精确size/version1、保留字段零；query输出size至少规范宽度、至多65536/version1，整个caller声明范围先验RW，成功只写规范前缀/规范size，未知尾部保持。所有输入span先验完整RO，合法SCL只读页可以传入；零span允许NULL。

Root的必要新接线：

1. 任务/物理分配器存在后调用 `sc32_broker_init()`；SYS300三个selector范围转到 `sc32_broker_call`，WAIT40b走下面单独的原子检查/睡眠路径。
2. task0 IF=0主循环调用 `sc32_broker_poll()`，pending使主循环继续服务；调用间可调度和处理输入/WM。没有用户回调从IRQ或Ring0执行。
3. task退出阶段在释放页目录、auth/session成员及task槽之前反复调用 `sc32_broker_stop_step(pid)`；返回1才进入后续清理。它只走本人PID/gen链，无新task_record字段和全任务/全句柄扫描。
4. session真实release/注销调用 `sc32_broker_session_discard(id)`，不能只清可见桌面或按UID混清另一个登录会话。
5. 将 `user/brokerapi/PAGINGSHARED-DRAFT.inc` 的真实增量放入paging.c对应位置，并完成该文件开头列出的范围/标记/回收/调试/设备条件。当前旧paging.c/h未修改。

Clipboard/IPC/Shared capability只在相应gate、退出hook与正式库export真正接通后置，Shared还须新PTE/range/ref出口真实存在。复制消息与剪贴板可以先接通，共享能力不能仅因接口声明而置1。

## 2. 拥有者、身份与对象池

公共opaque ID全局单调31位不回绕，同时核对kind、真实owner PID和task_generation。编号耗尽明确失败；objpool的32项为分配粒度，不是句柄/任务数量上限。拥有者池按PID索引，资源沿双向本人链链接；失败候选先收齐资源才公开，关闭不需要再次分配一个清理节点。

Identity由内核真实凭据生成：PID/gen、UID/GID、realm、登录session、subject UID/GID及硬件SYSTEM资格事实。消息发送者不能自行填写这些字段。UID=-1不是管理权；`HARDWARE_SYSTEM`只来自有效外部串口的 `auth_can_mod`。受委托服务的subject仍单独记录，默认IPC不会把同为SYSTEM服务UID的不同用户请求混成一个权限域。普通/root/网络子进程不会由broker token或消息正文取得SYSTEM。

GC节点嵌入对象，关闭/失败不依赖malloc。PF负载、共享对象与退役帧沿真正待回收链；消息/候选只保存已复制内核值，不跨gate保存用户request/out/data。延迟GC的待回收PF是真实仍占用资源，统计不能假称已经归还；最后垃圾链为空才收束。

## 3. Clipboard候选、提交和快照

Clipboard严格按真实登录session隔离，与UID、当前可见桌面不同。同UID两次登录不能互读；普通会话使用本会话，锁屏拒绝，SYSTEM剪贴板只允许有效硬件管理链操作，服务UID相等不能借此读管理桌面内容。

`Begin(desc,&candidate)`留本人可变候选，budget0选1MiB默认，可显式选至64MiB逻辑预算。`Offer(candidate,format,data,bytes)`支持budget内完整正文；单gate传输≤64KiB。较大正文由私有Begin/Write/Finish事务分段复制、跨段保持UTF8或FILES解析状态，每段让出，完整验证后才替旧项；源span在同步调用期间保持有效且不并发修改。失败保原format，Commit遇仍未Finish的writer为BUSY。总budget包含format元数据及正文，不是全系统PF配额；format索引为AVL，不每次从完整候选链查找。

`Commit(candidate,expected_epoch,&info)`把全部已Offer格式一次发布，候选必须非空；expected_epoch0为明确无条件，非0为CAS，失配STALE保候选与旧current。epoch从1开始、最大FFFFFFFF，耗尽拒绝不回绕。成功消耗candidate；返回info.handle0，若需持有快照再Open。`Abort`只释放未提交候选，源EXIT撤候选但不撤已经提交的current。

`Open(&info)`留本人immutable snapshot，info.handle供Formats/Read/Close。已提交正文独立于源进程；源EXIT后仍能读。替换/Clear只撤旧current引用，旧snapshot最后Close才回收。Formats每批0..64行，rows整个容量区RW预验，cursor为库产生的本人snapshot游标，0开始、FFFFFFFF结束；capacity0只查当前状态。Read按format/offset读取，offset越末尾RANGE，输出IoResult给真实transferred/total/END，未知format NOT_FOUND；它不保存用户游标。logout使current及旧snapshot不可继续读，进程退出关闭所有本人refs，随后GC最后释放正文。

`Query`不留snapshot，报告current或EMPTY；`Clear(expected_epoch,&info)`沿同CAS规则。`Watch(1,&epoch)`登记本人会话观察，commit/clear后后台按真实PID/gen发STREAM4；Query/Open成功ack观察epoch。Ready(handle0)报告epoch差。Watch0/EXIT撤本人订阅，不扫描其它任务，快速连续commit可以合并通知但不能使尾部观察者永远漏掉最后代数。

### TEXT与FILES真实类型

TEXT1为严格UTF8加唯一末尾NUL，bytes包含它；中间NUL、过长、断续、代理项、超10FFFF或截断拒绝。内核4KiB栈只留流式UTF8状态，不再在栈上留64KiB/整页副本。CUSTOM格式号10000及以上为应用明确typed opaque span，仍按会话/immutable寿命；不是SYSTEM能力。

FILES2正文为16B `{size=16,version=1,count,operation}`，operation COPY1/CUT2。每项20B `{record_bytes,path_bytes,object_id,revision,flags}`，后跟含唯一末尾NUL的绝对规范路径，record精确补零到4B；path_bytes2..65，revision非0。核对乘加/计数/所有record边界/所有路径/零padding后才能Offer，不能把文本内容暗示为CUT。

flags PIN_OBSERVATION1时object_id非0，它只是当前modern open-pin的观察身份；flags0时object_id必须0。现SandFS v5没有公开持久birthID，旧pin关闭后不能拿其ID要求文件仍同寿命，尤其不能把源EXIT后的正常复制全误报STALE。SCSHELL粘贴重新比真实磁盘revision/路径/当前权限；观察pin若仍活可作附加确认。文件列表仅是意图，不授正文读取、覆盖或删除权。

CUT的文档剪切在Ring3先准备可回滚的编辑候选，clipboard提交后再执行无失败点的本地commit。文件MOVE则必须逐项在目标真正持久提交成功后才由SCSHELL删除对应源；Send成功、ACK收到或clipboard Commit都不等于磁盘搬迁完成。清理旧CUT条目要CAS旧epoch，不能清掉用户后来复制的新内容。多文件IO不是未实现的磁盘原子事务。

## 4. 命名IPC、ACL、帧与关闭

名称为1..63B精确区分大小写的ASCII字母/数字/下划线/横线/点；`.`和`..`拒绝。SESSION namespace0按真实session注册，Create.session0=self或必须精确等于self，重名EXISTS。SYSTEM namespace1为全系统保留，只有真实 `auth_can_mod` 可通过公开Create注册/Listen；第三方SCL、普通root、受委托服务不能凭自报SYSTEM/service名接管它。后续可信服务的保留名若需由内核launch预置，须另接明确受保护注册链，不能降低公开gate规则。

默认ACL要求真实session、realm、UID/GID和subject UID/GID一致。ACL_SESSION/UID/GID/REALM显式替换相应条件：UID/GID比较真实subject，省略的条件沿owner身份；跨session必须明确SESSION条件。PUBLIC_NORMAL只允许SYSTEM注册者明确授权普通登录会话，可在SESSION条件allowed_session0时接受任意普通session，其它显式条件仍生效；锁屏/服务realm/负主体不混入普通客户端。权限字段仅表示owner对本人endpoint的连接意图，接收者不会因此变成SYSTEM或取得对方私有页。

Create留endpoint、Listen允许连接。Connect核对名字、真实server PID/gen及ACL，先留两真实owned channel及connection，再原子入pending；成功返回CONNECTING channel，Send在Accept前AGAIN。pending_budget控制本人listener的逻辑排队内存，不是全局endpoint数。Accept out与peer完整预验后才消费连接，peer是内核捕获的真实身份；已接受connection脱离listener，后来Close listener不破坏它。

每个direction为独立FIFO原子帧。type0保留，Send只有type/body，不允许自报sender/flags。正文≤64KiB，最小帧以内联128B保存，不为每条短消息单独申请一页；较大正文用独立PF页链，不整文件/Ring3缓存。budget计message元数据+正文，队满AGAIN且不部分入队。Peek以Packet.bytes给所需正文尺寸而不消费；Receive容量不足RANGE且out/队列保持，调用者先Peek再准备合适容量。

Packet80包含type、bytes、DATA/EOF、双字sequence和真实Identity48。普通零字节帧仍为DATA且sequence有效，不能误当EOF；对端close/EXIT后先读完已接受帧，再返回EOF flag/bytes0，Send为BROKEN_PIPE。关闭立即撤本人公共handle并通知peer，余下discard正文分批GC，最后connection引用归零才清元数据；没有旧16fd配额或不可见的保活writer。IPCepoch仅是状态提示，实际authority为handle/PIDgen；sequence完整双字增长、耗尽拒绝不回绕。

## 5. Ready与GUI/broker组合等待

Ready给READ/WRITE/ACCEPT/CLOSED、epoch及实际可读/可写字节。WriteReady的字节为扣message元数据后能原子接收的payload，并限制max_frame；Waiting最小write bytes可避免“只剩1B却重复重发64KiB”的空转。ReadReady含真实EOF/closed状态，接收者仍按Packet flags判断。

`ScBrokerWait(desc,&events)`的desc32给handle、wanted flags、minimum_write_bytes、event_mask、timeout。handle0/wanted0可只等INPUT/STREAM等事实事件；Clipboard Watch配handle0/READ。SCUI泵使用INPUT|STREAM，不再用只等INPUT的ScMessageGet来承诺跨API唤醒。库返回真实事件位，timeout为SC32_TIMEOUT，唤醒不是消息预留，之后重新Peek/Receive/Accept/Query。

Root的WAIT40b专门路径必须在同一IF=0陷入内完成：

```c
u32 timeout,mask,events;
int result=sc32_broker_wait_check(current,request,output,&timeout,&mask,&events);
if(result!=SC32_PENDING)return result==0?(int)events:result;
task_record(current)->wait_result_mode=0;
int waited=task_wait_install(frame,mask,timeout);
if(task_record(current)->waiting)return 0;
return waited<0?(waited==-4?SC32_E_MEMORY:SC32_E_INVALID):waited;
```

check先验整个request；out必须NULL。包含INPUT时使用现GUI真实队列peek (`sc32_gui_wait_result(pid,0)`)，不能只依赖已经消费过的INPUT提示。检查和安装之间不开IRQ、不回用户；只存可信陷入frame，既有mode0唤醒时返回事件位，没有user out/request指针跨睡眠。此精确task接线当前尚未实施；裸声明不是已经可睡的API。旧mask255及已发布EVENTWAIT语义不变。

## 6. 真PF共享、grant和映射

CreateShared留独立内核对象，按页取整容量、每页在任何用户PTE可见前全零。每gate只初始化16页（64KiB正文），内部PENDING token由私有wrapper保存并让出后继续；public out只在所有页READY发布。OOM标候选FAILED、撤本人临时ref后以同一GC链归还已取PF和页索引。没有把已有malloc地址传给其它进程，也不复制字符串冒称共享。

GrantShared绑定真实目标PID/gen及完整身份快照、显式session条件、scope offset/length和READ1或READ|WRITE3。offset/bytes4096粒度，只在本人已有scope内且access为子集；目标不同session必须显式CROSS_SESSION。OpenShared只允许精确目标身份和仍活issuer grant，不能从猜中ID取得能力。打开后reference独立；Revoke/issuer EXIT只停止后续Open，不撤已经合法打开的refs/maps，不能虚称这个v1有强制撤销所有已映射页。

MapShared为本人 `60000000..63FFFFFF` 虚拟窗口，物理帧是同一个object的真实PF。这个窗在现PF管理上限256MiB之上，与内核物理恒等别名不冲突，也不占旧heap/SCL窗。PTE_SHARED800标记PF归broker；只读grant的PTE无RW，输出校验也不能借Ring0物理别名绕写。非PAE无NX，不能宣传禁止执行数据/完整W^X。

reserve只留PTE占位，最多16张页表/16384个元数据项，不能一次分配/清零64MiB正文。bind/unbind每次≤16页；Mapping成功前不公开候选VA，失败撤全reservation/PTE/ref。CloseShared不撤仍活Mapping；Unmap同步wrapper内部让出逐步撤PTE，完成后清空页表，最后object ref才由broker归还PF。grant/reference/mapping都沿各自真实owner链，Source EXIT后其它合法refs/maps继续，最后recipient EXIT/Unmap/Close才释放物理正文。

paging草稿尚需Root同批在现实现接线：新任务目录该窗16PDE清零；user_range接受此窗并照查P/US/RW；free_task_dir区分Shared PF，不按private规则doublefree；已有private free/protect不接该窗；debug_patch对Shared拒绝，不能借旧受控INT3特例跨写readonly grant；map_device拒绝窗内BAR别名冲突。broker mapping清理必须在任务pd释放前完成。这些条件实际缺失时Shared cap保持0，不能用旧USERALLOC/现free_task_dir猜测已经支持。

## 7. 分批清理、容量与验收出口

每次poll最多32个GC/通知逻辑步骤，共享/blob正文及共享页索引回收全轮总预算16页；objpool空块元数据另在至多32个逻辑步骤内归还，仍列入实测PF账。剩余工作唤醒task0下一轮。Clip format一项一项退役；endpoint关闭分批取消pending server channel；channel关闭分批丢帧，已发到peer的帧不丢；共享lastref分批清PF/索引页。stop_step沿本人链至多32句柄，遇Mapping只做16页并保留清理阶段，下轮继续；不能提前释放pd再查旧PTE。

本批不依据O(logN)或有界代码声称延迟/内存效率已达标。逻辑budget不等PF数，记录真实池页、候选/GC pending、复制字节及最大gate/poll tick；同一实际负载的输入/WM长尾由主代理统一测。物理OOM、候选完整回滚、异常EXIT与session logout需匹配具体新源，旧Foundation结果只能复用未影响部分。

现task调度的`cleanup_head<0`全局条件仍会在一项大清理期间阻止所有R3派发；16页短步自身不能消除此门槛。Root整合时须审这个条件与kernel_pending，在保留state2/owner撤销及pd最后释放顺序的同时，允许无关ready在有限GC批次间推进。本子任务没有改现task源或据此声称已实现全机不卡顿。

静态核对：31项公共声明都有对应桥正文，92个内核内部helper引用能解析，全部新C/H/草稿的定界符匹配，私有片段无SCAPI/asm或SCCLIB引导依赖。2026-10-15当前Root SC32聚合（含Font/Palette/Popup）加本broker片段为27个唯一include、150801原始字节/150828B含终止字节，371个不同定义宏加11预定义为382；FILE32/SOURCE262144/MACRO384尚未超，但只有2个宏余量，后续Root正在写的Cursor/Wheel/Drop等需重新计算。此为文件/文本静态计数，不是运行预处理、机器码/ABI/Werror或应用执行。

必要的定向出口是两独立进程：immutable TEXT/FILES源EXIT仍可读、替换旧snapshot安全、CAS失败保持；真实sender与错误身份/session拒绝、短队列AGAIN/容量不足不消费/EOF明确；真实shared PF复用、只读拒写、owner/recipient分步退出收束；GUI泵在IPC/clip和键鼠之间真实唤醒。拖放window资格/短期ticket与最终Shell MOVE/权限重验属WM/SCSHELL后续接线，不能据本broker帧交付称完整跨窗口拖放已完成。不扩理论全矩阵、不复制大量数据盘。

## 修订记录

2026-10-15：新增正式broker头/私有selector、同session immutable clipboard、复制型命名IPC真实身份/背压/关闭、真PF共享对象/grant/短步映射和paging增量草稿；现冻结输入未改，未接线/构建/客体验收。


## 2026-10-15 同批真实接线状态

Root已将31个桥合入SC32API.SCL，库ABI1.2/version3，新增Drop7项加FONT/Palette/Popup后总130导出，原86保留。`SC32BROKER.H`与`SC32DROP.H`为独立公共头，应用显式包含；不在SC32API.H中形成BROKER→API→DROP→BROKER尚未定义类型的循环。

task_init已初始化broker；SYS300三个范围与WAIT40b同IF=0检查/安装睡眠已接，main调用真实poll；session注销/释放撤clip与Drop。`paging_shared.inc`正文已进入paging，16个新PDE窗口/逐页范围/RW/Shared PF归属、private_trim保留未present reservation、debug_patch和设备别名拒绝均已改。退出先撤原FILE/GUI/Streams/调试/窗口/网络关联，之后逐16页拆broker映射，最后auth/session/pd与PID释放。清理队尾水印保证每轮同拥有者只做一次资源阶段；旧授权全部撤销后，READY在有限共享回收批次之间可调度，未把清理页数变成整个三环全局停机。

Drop可信helper共用named Connect的同一事务正文，ACL/真实双方peer、预算、两能力候选回滚、pending/Accept均相同；没有据foreign endpoint数值给调用者拥有权。`SC32_MSG_BROKER_READY`未实现枚举已去掉；实际组合泵依赖BrokerWait的INPUT|STREAM，不声明没有生产者的GUI消息。

上述是实际源码接线，**本批尚未统一构建或运行**。初稿“未接线/草稿”的叙述留作沿革；新源码不能复用旧P3行为PASS。新PLATFORMPROBE针对普通会话真实双进程/大TEXT/旧snapshot/CAS、原子IPC/身份/背压/EOF、真共享授权/RO与回收；统一证据由Root执行后登记。

修订：2026-10-15，记录真实聚合/gate/paging/owner/session清理与调度条件、Drop共用Connect事务，待本批构建/客体验证。

2026-10-15静态修复：分步Shared Unbind仅撤映射，保留整块SHARED/FIRST/LAST reservation；最终Release完整预验无P/精确块边界后一次清占位并trim，防止同PID交错新Map复用旧Unmap尚在拥有的前缀。实际未运行。

2026-10-15静态退出通知修复：拥有者分IPC/其它能力/Mapping三条动态链；退出先关闭endpoint/channel并唤醒真实peer，再撤其它能力，最后逐16页拆映射。旧mixed-head会让后来创建的大mapping挡住已经EOF的独立IPC关闭通知，不能只靠state2使查询看到EOF而不唤醒等待者。句柄/public布局不变，数量无固定门槛；修复尚未运行。


2026-10-15真实统一构建/首轮接续：ui-build-04退出0、45.7486秒、source_modified=[]；CORE full507120/payload506992/BSS374600，SC32API138410B/130导出，SCUI328844B/157导出/137导入，DEMO36388B/119导入，PLATFORM26148B，TEXT2724B，当前SCCC0 load128937。build01新Drop排版Werror、02内部几何前置声明、03文档NULL span/时钟失败及实际未使用errno导入均保原日志，据具体诊断修复，不降警告门槛。

Windows双盘ui-platform-guest-01已真实普通su mio：父PID6/gen1、childPID5/gen2、SID4、UID1000/realmNORMAL/flags0；全部87平台检查通过，131079B文本/旧snapshot/CAS、实际IPC发送者/队列/EOF/等待、18页Shared RO/RW与issuer退出、FONT/Palette符合探针。parent实际PTE89→125→89、PT4→4，运行0.781秒/一核CPU0.671875秒。该轮整体仍FAIL：退出全br/drop/window/file/job/pipe对象/页归原，Font owned952/Desktop3818/records5页不变，PF9007→9008，唯一变化wait_pages0→1，主控尚未计旧有限等待heap首次缓存。VM已结束，未记录wait_count/capacity不能追溯把该FAIL改PASS；保持报告/原stdout/截图，下一轮核这份旧缓存真实归属与空闲状态再继续官方边界/G1/原生库与GUI。原盘及构建source/output摘要保持。

上述只是实际平台首组子结果，不是SCUI/整个M10a2通过。当前GUI主控/source独立工具仍收束，完整目标active。
