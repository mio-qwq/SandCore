# SCLIB.SCL 通用库接口与生命周期

所属版本：**SandCore M10a2 — The Application Platform Era**。公共头为 `user/sclib/SCLIB.H`，正式产物 `/SYS/LIB/SCLIB.SCL`，SDK头保持完全大写。完整设计见 [SCLIB-DESIGN.md](SCLIB-DESIGN.md)，范围保持根M10a2RULE.MD §4.2全部项目。

**完整功能范围的源码/清单已收齐，并已统一生成宿主和客体库、实际执行UTILPROBE。** [真实证据](../evidence/m10a2-stdlib/README.md)包含七组253检查、22B实际FILE输出、事件/模型/lease重入与失败保持、正常退出精确PF回收。163个函数计数不代替所有规模性能证明，也不代表SCUI、桌面或整个M10a2完成。

## 1. ABI、标准依赖和错误

本库ABI1.0/library_version1。163项公开函数全部cdecl，最终manifest只导出它们；`ScLibFinalize`只为库fini，不向应用公开。对象opaque pointer、32位ScU32/ScI32/ScStatus与ScWideCounter读取共同SCTYPES；标准FILE/va_list读取真正小写stdio/stdarg头。普通应用不包含聚合或实现片段，不复制库正文。

两项真实依赖：SCCLIB ABI1/minor0/version1，以及SC32API ABI1/minor1/version2。后者提供正式ScCallbackPin与宽monotonic；其Root最低版本由SC32依赖链约束。旧85入口/P0证据不覆盖新增pin、C2变参或本库；不借旧产物称新源通过。

30项实际导入为：SCCLIB的malloc/calloc/realloc/free，memcpy/memmove/memset/memcmp/memchr，strlen/strcmp，strtol/strtoul，qsort，snprintf/vsnprintf，isxdigit/isdigit/toupper，fclose/fflush/fread/fwrite/fseek/ftell/ferror/feof以及errno访问实体`__scc_errno_location`；SC32的ScCallbackPin、ScSystemMonotonic。基本分配/复制/字符串/标准格式及stdio算法均调用这些实体，没有SCAPI、asm或私有syscall桥。

本库状态：OK0、END1、STOP2。稳定错误-201..-213分别为INVALID/RANGE/MEMORY/UTF8/STALE/NOT_FOUND/EXISTS/BUSY/IO/CANCELLED/SYNTAX/AGAIN/NOT_SUPPORTED。代理系统调用/用户loader的真实负ScStatus也可原样传回，不把它们瞎猜为本域；ErrorObject明确持domain/raw/op。未声明完整ISO C、FP/long long、TLS或多线程安全。

公共输入描述精确size/version1、保留字段零；输出先填size>=规范尺寸、version1（至65536），只写规范前缀/规范size，尾部保持。纯Ring3对象指针、span、out均须由调用者提供合法地址；库的逻辑/溢出检查不冒充同进程沙箱。NULL span仅在bytes0允许。Release(NULL)无作用，最后Release后指针不可复用；不能靠kind检查把已free指针变成有效handle。

常用记录尺寸：View20、ContainerInfo32、Iterator24、Array/List/HashDesc32、ListEntry24、HashEntry32、PathParts32、StreamDesc48、StreamInfo32、BinaryReader32、Value32、ParseIssue32、UtilErrorInfo48、ErrorDesc32、LogDesc32、LogEntry52、Event48、SubscriptionDesc40、EventResult32、ModelNodeInfo/Change32、CacheStamp/Desc32、CacheInfo64。源码已有基础ABI断言，全部尺寸仍须真实编译确认。

## 2. Array、List、Hash及借用

Array是连续定宽字节元素；Create/Retain/Release/Query/Reserve/Insert/Erase/Resize/View/Get/Set/Clear都有实际正文。容量按需几何增长，reserve可显式请求；溢出在realloc前拒绝，失败保留旧地址/容量/内容。Insert允许源位于原array，先保存alias再可能移动；Erase调用drop并压缩，Resize新增全零元素。无drop的Set直接memmove，不为常见标量写入多次malloc。

List是双链动态节点及动态token桶索引，Insert的before0尾插、非0为本人活token。Remove/Get使用稳定不复用token，跨容器/删除后STALE；数据指针属于节点，不是内核地址。元素起点16B对齐，width需按实际元素sizeof选择。Hash拥有二进制key副本和定宽value，key允许内嵌NUL，empty key合法；默认FNV-1a仅为本进程非密码索引。INSERT_ONLY/REPLACE_ONLY拒绝相反状态，cached hash令扩容不重扫key；load≤1/2，真实碰撞最坏仍可O(n)。

元素复制是浅字节复制，drop不是自动深拷贝。调用者把每个payload的实际拥有权转入，共享payload先自行保留引用；drop需支持Resize产生的零状态。正常Release/Erase/Remove/Clear回收一次，晚fini只安全回收库自有内存，不能在context已失效后补调用户drop。

View.data只读借用，bytes与generation均来自当前owner。任一成功修改/扩容/COW后重取View，旧data即使generation不再相同也不能再次解引用。Iterator由Iterate初始化，owner须被调用者保留，position是库生成的不透明本人游标，不能伪造/序列化或当token。Next先验generation，修改后STALE；正常Next O(1)，不逐项从链头扫描。

List/Hash Visit先pin实际procedure，取入口token水位。回调可插入/删除当前或未来项；新项排除，嵌套visit取自己的水位。删除退役到最外层visit后才drop/free，保存的next仍活；Hash当前回调的value被持有，替换它返回BUSY，删除则允许退役。最后Release发生在回调内时标closing、返回后收束，Visit给CANCELLED。析构或IO事务中的同对象操作返回BUSY/STALE，不允许读到半回收状态。

## 3. 字符串、UTF-8、格式和路径

ScString持counted bytes。Create拥有副本与额外NUL；Borrow只借已声明范围，绝不读取bytes后面来猜终止符。CStr在borrowed对象上申请完整NUL副本、成功才materialize并推进generation。拥有/借用对象都支持Assign/Insert/Erase/Append，alias来自旧data也保留；修改失败不改变原对象。嵌入NUL可保存，CStr仍按标准C字符串在第一个NUL结束，不能把strlen说成counted长度。

UTF-8Decode/Previous错误不推进offset、不改scalar；END也保持输出，offset/scalar须为不同out。Validate诊断首坏byte，Count/Offset按Unicode scalar计，不按字素或屏幕列；Encode只接受scalar，最多4B。严格拒绝过长、断续、代理项、超10FFFF与截断。非字符仍是可编码scalar，辅助predicate供应用选策略；Control为C0/C1，Whitespace固定Unicode15.1的25码点。依据 [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629.html) 与 [Unicode15.1 PropList事实](https://raw.githubusercontent.com/unicode-org/unicodetools/main/unicodetools/data/ucd/15.1.0/PropList.txt)，不假称完整locale casefold/UAX分词/规范化/字体/输入法。

StringFormat/AppendFormat及v组真实依赖vsnprintf和va_copy，两遍计数/输出后再提交候选。%n等标准格式自身的参数副作用不被承诺回滚；目标对象的内容/容量在最终成功前保持，且调用者不能经只读View越权改对象内部。标准库没有的FP/64位格式不能从这里冒充支持。FormatHex精确字节编码，FormatEscape输出quoted内容用的反斜杠转义（不自行加引号），FormatBytes为32位整数IEC单位与精确一位小数，不用浮点或可能溢出的remainder*10。

PathNormalize支持相对base、重复分隔、点段、上级段、可选BACKSLASH、REQUIRE_ABSOLUTE和REJECT_ABOVE_ROOT。absolute leaf覆盖base，空相对输出`.`，根保持`/`；相对前导`..`保留，不猜未知cwd。Join复用同一规则；Split给原UTF8字节偏移（directory含末slash、extension含末dot；`.name`不当扩展）；Relative需两边absolute，以完整组件的精确字节比较，不把`/a`误当`/ab`祖先。它是词法工具，未替代SC32 Resolve、大小写匹配、文件权限或SandFS63B实际名字边界，不解析URI/符号链接。

## 4. Buffer、Stream和bounded binary

Buffer拥有/借用COW，Reserve/Resize/Write/Append/Consume/Clear都维护generation；Write越EOF零填洞，长度乘加先验。候选失败不留下半记录，source alias先保留；Borrow不释放外部指针。Reader输入是调用期间由调用者保持的借用，不持久保存用户out。

Stream可来自受控callback描述、保留Buffer引用或借用/转移FILE。flags规定允许read/write/seek及OWN_CONTEXT/OWN_FILE；callback须实际提供相应函数，注册候选先pin后提交。借用FILE不自动fclose，转移FILE由正常Release收尾；调用者不能在adapter活着时另行seek/close同一个FILE。FILE请求SEEK时先真实ftell确认，pipe不伪报位置；读写后同步seekable FILE当前逻辑位置，append不把初始读游标误报为写EOF。无定位的callback/FILE位置为本adapter的已接受字节游标。

Read/Write都有实际transferred，错误或END可有已接受前缀，不能盲重放；callback正bytes却0进展/OK变AGAIN防空转。FILE用fread/fwrite/ferror/feof，真实EOF为END，errno EAGAIN另报；标准FILE的sticky error仍由它的标准clearerr管理，不偷偷擦掉错误。Seek失败不改库cursor/out；Flush是独立实际操作，Buffer无外部持久化且无需IO。Release的用户close/drop返回void，需durability证据时先检查Flush；不能凭Release返回不了错误就说磁盘成功。

BinaryReader所有范围先检查bytes-position，读失败不改cursor/out。LE U8/U16/U32、双字Counter完全按固定宽度，不冒充C uint64_t。varU32最多5B，拒绝溢出、持续continuation和非最短编码。UTF8记录为canonical varU32长度加严格UTF8正文；Reader验证整个正文后发布借用View。Writer先组完整候选再一次Append，OOM不能留下长度成功/正文缺失的半条记录。外部byte View的generation0表示caller负责其源寿命。

## 5. 配置语法与事务

ScConfig拥有(section,key)和值，section可空global，key非空；名字为严格UTF8，不许C0/DEL、`[]=#;"\\`或首尾ASCII空白。纯正文UTF8与原始NUL先验证，定位首坏byte；其它语法/重复错误定位失败项的首个非空byte，line/byte column从1起，不谎称一定指向转义内部的精确字符。整份Parse先创建独立候选，成功交换items并增generation；任何坏UTF、语法、重复、溢出或OOM释放候选、原配置不变。问题record的未知尾保持。

v1为行式INI，支持LF/CRLF、`#`/`;`评论、`[section]`，`[]`恢复global，key/value两侧ASCII空白去除。Quoted值可含Unicode和`\n\r\t\b\f\\\"\uXXXX`；合法UTF16 surrogate pair转scalar，孤立代理项拒绝。Quoted `\u0000`是计长值，不是原始文件NUL。未引用`#;`开始评论；数字样式字符串请明确引用。

```text
title="Notes \u4e2d"
enabled=true
count=42u
delta=-12
empty=null
mask=hex"00ff10"
sequence=counter(00000001,00000002)
[window]
path="/HOME/FILE.TXT"
```

NULL/BOOL/I32/U32/Counter/UTF8/Bytes全部真实typed值。裸带符号十进制为I32，u后缀为U32；转换依赖真实strtol/strtoul和ERANGE，不能截断成32位。counter两项按十六进制读lo/hi；bytes使用偶数合法hex。默认同(section,key)重复EXISTS；Parse REPLACE_DUPLICATES=1选择后值。Serialize SORT_KEYS=2按section/key的字节顺序，默认插入顺序，必要时重复section标记；字符串始终引用/转义，bytes用hex，counter固定两组8hex。输出完整候选拥有String，序列化再解析保类型/内容，不借printf `%s`截断嵌入NUL。

Get的Value.data是配置借用，只到下一次相关修改/Release；Set先clone拥有数据，不能把传入堆块当无声明的归属转移。Value.flags必须0，BOOL为0/1，scalar的多余hi/data/bytes拒绝。

## 6. 错误、日志、事件和模型

ErrorObject保存真实code/domain/raw/op及拥有UTF8 message，Retain/Release/Query/Message/Format都有实体。它不推断旧raw=-1究竟是磁盘、权限还是地址错，调用方明确填写领域。ParseIssue独立定位，ErrorInfo未给的位置为0，不能冒称已找到行列。

Logger minimum level过滤不分配/不IO；max_records0动态增长，正值为调用者预算。ring按需扩容，已flush或无sink的旧项可FIFO淘汰；未flush项满时BUSY，不悄悄丢记录。Text为严格UTF8副本，记录有64位sequence、可选真正宽monotonic；不把PITHz称FPS。LogFlush延迟建立escaped line，记实际sent偏移，prefix错误/AGAIN重试从未接受处继续，不重复前缀。Clear明确丢弃尚未送出的内容，已经接受的sink字节不回滚；sink由保留Stream引用维持。时间/级别/errno误差不被改称IO成功。

EventHub是本进程通用回调，type0订阅全，其它type精确匹配；priority高先、同级登记顺序。ONCE在回调前退役，嵌套emit不重复。新connect排除旧入口token水位，disconnect当前/未来项延迟free/drop；context_drop正常以`drop(context,NULL)`调用一次。Emit复制Event头但借data，系统不会解这个pointer；它不是ScMessage、硬件队列、IPC或系统剪贴板。STOP为正常停止，错误首值和called/stopped在Result里；STOP_ON_ERROR可截断传播。

Model的0为嵌入虚拟root，可拥有properties；Insert parent0在根下、before0尾插，非0 before必须同parent；Remove/Move0拒绝，cycle为INVALID。稳定真实node ID不复用、动态hash索引；NodeQuery.revision为全model保守代数，任一结构/值变更令快照失效。Visit先快照ID集（root0也包含），回调中删除的节点跳过、移出仍活项保持本次集合、新项不加入。

ModelConnect订阅SC_MODEL_INSERT/REMOVE/MOVE/VALUE 1..4，Event.data是32B ScModelChange，含node/parent/revision/property。状态先提交再观察，观察失败不能当候选回滚；ScModelNotificationStatus给最后通知首错误，Query.flags bit0提示。观察中Release model由外层depth延后回收；临时property名字保留到整个通知返回，不能释放旧值后还借它作回调名。

## 7. Cache：stamp、LRU与lease

Create必须实际loader/drop、正budget，并pin两个地址。key为拥有的二进制span；完整CacheStamp32含namespace/object ID/object generation/theme/display/user generation，全字段相同才hit。调用方以真实文件revision、主题/显示/几何/世界等事件设置stamp/Invalidate；库不轮询全系统猜失效。

每entry计费 **sizeof(SlCacheEntry)=72B + key_bytes + 1 + loader报告cost**。cache对象、桶和lease-index另外消耗真实SCCLIB内存，不计这个逻辑资源预算；不能称budget是实测PF数。单字key、resource cost4时entry78B，budget156B容两项；budget8B不能装它。Query.used_bytes包含仍有lease的旧stamp资源，不能假报已经回收。

Acquire相同stamp增加独立稳定lease并LRU置前；miss先留候选、实际load、检查cost/预算及可淘汰资源、留lease，然后才提交。失败保原entry/有效stamp/lease/out，不驱逐旧内容。loader失败且给非NULL候选由drop撤；成功NULL可为合法sentinel，后续候选失败也执行drop。真实malloc OOM可由loader返回MEMORY，不注入虚假系统allocator。

新stamp替换后，旧有lease项失效但保留resource与计费，新的独立候选可同时存在；无lease旧项立即drop。预算只淘汰LRU无lease项，全部leased或不够可释放空间则BUSY，SetBudget失败保持原预算。Clear/Invalidate只在最后lease后drop旧资源。用户最后Release(cache)停止Acquire，但每lease的内部pin保护header/resource，ReleaseLease仍允许closing对象；最后返回才实际收束，不能给调用者悬空resource。loader/drop中改同cache为BUSY，context必须到全部lease和drop完成仍活。

## 8. 收束、规模与验证出口

全对象记录在库实例的动态链。正常Release调用拥有回收、深度归零才收header；late fini静默，先全部清内容再全部清header，让跨对象保留引用在内容阶段仍可解除。内部typed值的安全标准内存drop与用户callbacks分明；不把异常跳过用户析构说成它们已执行。各进程独立可写状态，无系统级全局缓存、隐藏权限或共享model。

当前静态实数：163公共声明/163对应定义/163manifest exports，private fini另1，30实际导入；include图30唯一文件/176936原始字节/284不同定义宏，加当前11预定义共295。FILE32/MACRO384/SOURCE262144未超；实际token/代码/ABI/Werror/效率仍需主代理编译和客体证据。职责源排版仅改空白，并核对去空白内容逐一相同；不以此代替生成或运行。

主代理UTILPROBE按相关风险验证实际SCLIB依赖/标准库调用、own/borrow/alias/代数、UTF8边界、路径与配置typed roundtrip/失败保持、事件/模型重入、cache lease/失效/LRU/真实OOM。只测实际未解决问题，已有动态链接/P0部分证据按依赖变化复用；没有自行扩大矩阵。性能须测实际allocator页/复制、典型和最坏容器规模、回调遍历、cache复用与UI输入成本，不据O(1)期望或小程序PASS宣传整机流畅。

## 修订

2026-10-15：收齐163项完整§4.2通用功能正文、SCCLIB真实算法依赖、SC32代码寿命pin、事务/借用/回调/模型/cache与二阶段静默fini；静态核对，宿主/客体生成与执行由主代理统一完成。


修订：2026-10-15，统一stdlib-build-06及guest02/04/05实际SCCLIB/SCLIB/R1/C1..C5、九模式/当前G1/253项通用工作流与精确回收通过；保留三项验证失败、未变输入复用与证据边界，见evidence/m10a2-stdlib，完整目标继续。
