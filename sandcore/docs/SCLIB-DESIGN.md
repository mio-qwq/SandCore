# SCLIB 通用库设计

所属版本：**SandCore M10a2 — The Application Platform Era**，合同为根M10a2RULE.MD §4.2全部范围。2026-10-15，公共头、16个职责源、聚合和163项真实导出已收齐；**仅静态核对，未构建、未客体验证**。SCCLIB标准语义与SC32系统协调均不被这个通用库替代；现阶段不是完整M10a2已完成。

## 1. 分层与完整对象范围

SCLIB.SCL是Ring3自有通用库，正式公开头 `user/sclib/SCLIB.H`；公共标准头仍小写。依赖真正SCCLIB的malloc/calloc/realloc/free、memcpy/memmove/memset/memcmp、strlen/strcmp、strto、qsort和snprintf/vsnprintf/stdio。基础算法不复制、不增加asm/syscall，不静态把SCCLIB正文复制进SCLIB或应用。系统时钟和代码固定仅通过正式SC32API；库不得到额外UID/session、SYSTEM或Ring0资格。

| §4.2范围 | 本批完整公开方向 | 实现责任 |
|---|---|---|
| 动态数组 | ScArray连续字节元素、reserve/insert/erase/resize/get/set、drop | realloc失败保留旧对象；输入alias先保存；版本化借用View |
| 链表 | ScList稳定token、前插/尾插、remove、检查迭代及可重入visit | 节点动态分配；回调删除延迟回收、入口水位屏蔽新项 |
| 哈希 | ScHash拥有二进制key/定宽value、load≤1/2、缓存hash、迭代/visit | 高效默认字节哈希；候选桶齐后提交，不在扩容重扫key |
| 字符串 | ScString拥有/借用、materialize CStr、alias编辑、格式化 | counted bytes与NUL副本分明，嵌入NUL不伪称strlen相同 |
| UTF-8 / Unicode | 严格decode/previous/encode/validate/count/index、scalar/非字符/空白/control、trim | 不接受过长、代理项、F5..FF或截断；不把字节ctype当Unicode |
| 路径 | normalize/join/split/relative | 词法层处理点段/根/可选反斜杠；不假替文件权限或OS Resolve |
| 配置 | ScConfig拥有typed值，完整INI parse/serialize、重复策略 | 全候选解析后交换，错误定位；稳定序列化与再解析 |
| 错误 / 日志 | ScErrorObject、ScLogger级别/时间/有界记录/stream sink | 错误domain/raw不猜；写入失败显式，不用空日志说IO成功 |
| 格式辅助 | string vformat、hex、escape、整数IEC单位 | 标准格式器实际依赖；32位精确整数，不假支持FP/long long |
| buffer / stream | ScBuffer拥有/借用COW；callback/FILE/buffer流 | 前缀传输、EOF/AGAIN、定位和flush明确；借用FILE无隐式close |
| 序列化 / binary | bounded reader、LE8/16/32/双字、canonical varU32、length UTF8 | 所有读长度/加法预验；失败不推进cursor或半追加 |
| 事件 / callback | ScEventHub priority/once/disconnect/嵌套emit | 原事件为借用；入口水位、retired、depth；不是OS消息/IPC |
| 数据模型 | ScModel动态树、稳定ID、typed properties、移动/移除/观察 | 禁循环owner；节点/键和值有实际归属，变更提交后通知 |
| 资源缓存 | ScResourceCache拥有key、stamp、LRU、budget、lease、失效 | 资源不得在lease仍活时drop；老stamp与新资源分明 |

公共输出记录先填size/version1，成功写规范前缀、未知尾保持。输入描述固定v1且保留位零。与标准C函数一样，纯Ring3库的指针/范围须由调用者提供合法本人地址；版本和逻辑边界检查不被夸大成同进程内存沙箱。系统API的整区权限预验仍由SC32内核gate负责。

## 2. 失败事务与代数

所有尺寸乘加先预验32位范围，再申请候选；OOM或解析/版本失败不更改原内容、容量、输出句柄或有效代数。候选和临时alias副本都有唯一owner，失败路径释放。原位写入只放在已无失败点的位置；已提交的用户析构/观察副作用不能装成可回滚。

对象是不透明指针，借用View包含当前generation；修改后必须重取，外部持有者不能缓存旧数据地址或长期把它当可写内存。对象最后Release后指针失效，调用方持引用；迭代器借用owner，不能超出owner寿命。generation和token域耗尽明确RANGE，不回绕复用成同身份；它们是库对象域而非PID/内核句柄。

Array/List/Hash复制元素字节。设置drop表示容器拥有相应元素资源，但不能自动推断深拷贝或给共享指针加引用；调用者在插入前转移/保留实际payload所有权，drop接受Resize产生的全零元素。数组重排/借用变动由generation失效；链表/Hash受保护visit允许删除当前/未来项，新项不加入本次旧水位，嵌套visit单独取水位。

## 3. Callback、正常回收与退出

每对象用引用/调用depth/closing控制。最后Release标closing、拒绝新操作，仍执行的callback/外层API保留header到depth归零；用户析构先标/撤实际节点再调用，重入不能双释放。析构期间改同一容器返回BUSY，访问其它对象可继续。Hash回调持有当前value，替换该value返回BUSY；删除当前可退役并延后drop。

持久callback必须固定它所属模块代码。主代理已将R1 pin-by-code-address接入正式 `ScStatus ScCallbackPin(const void *function_address)`；SCLIB直接读公开SC32API.H，不重复声明或提供桥/桩。注册先申请候选，再pin全部实际callback/drop地址，再提交。最低依赖SC32API ABI1/minor1/library_version2；它经公开库调用Root识别实际代码范围，系统不执行该地址。当前SC32API85出口的旧证据不覆盖这个尚未统一构建/运行的新pin增量。

Callback context通常由调用者借用，代码pin不延长它的寿命；context须存活到注销/全部活动callback返回/最后lease结束。Event订阅提供context_drop时转移该context的清理责任，正常注销在最后活动调用后以`drop(context,NULL)`回收，失败注册仍属调用者。公共地址从SCX或正确固定的模块来，不能以只读映射存在就声称它是可执行函数。单进程单线程是现有执行条件，库不假称TLS、C11原子或多线程无锁安全。

显式Release执行拥有的payload回收；应用应在context/model销毁前释放其容器与订阅。晚SCLIB fini静默停派发，不再进用户drop/loader/contextclose：其它库或atexit可能已清model。先清全部对象内容，再清全部header，防止Stream保留更早Buffer、Model保留Hash时强制逐对象free出现UAF；内部typed值/字符串仅用安全自有析构。异常EXIT由标准allocator/内核owner兜底，不把跳过用户析构说成它们已运行。R1 prepare/exit接线由主代理保持独立合同。

## 4. 事件、模型与缓存失效

EventHub是通用本进程回调容器。priority稳定排序，once在调用前退役以防嵌套emit重复；disconnect当前/下一项只标retired，最外层emit后drop context一次。emit使用入口token水位，新connect只会在后续或明确嵌套emit中见到；事件data是本次调用借用，没有伪系统队列、剪贴板、跨进程IPC或硬件沿。

Model拥有树和property字节。稳定node ID不复用，移到自己/后代拒绝；大子树迭代关闭，不递归耗尽应用栈。typed字符串/bytes先验证/clone后提交，旧值随后释放；观察在状态提交后发生，首通知错误单独通过ScModelNotificationStatus查询。NodeInfo.revision采用保守全model generation，结构或值变化均使快照失效；按变化node精确失效可由观察层实现。Visit先捕获ID集，之后删除项跳过、移动后仍活项继续、新插项不加入，不读取已回收node。

Cache stamp由调用方填namespace/object ID/object generation/theme/display/user generation，完整匹配才命中。它不擅自探测所有OS对象或谎称自动知道编辑世界、相机、主题/尺寸失效；应用按真实事件推进stamp或Invalidate。key与metadata计入budget，resource cost由loader报告，不能称预算就是实测物理页。LRU只淘汰无lease项；Invalidate后老资源仍为已有lease保留，新Acquire用新stamp独立候选。无可淘汰空间明确BUSY，候选失败不删原资源。cache最后Release停止加载，但活lease维持资源/header到ReleaseLease，不返回悬空指针。

## 5. 编码与性能来源

严格UTF-8按 [RFC 3629](https://www.rfc-editor.org/rfc/rfc3629.html) 的1..4字节及scalar边界实现；White_Space固定为 [Unicode 15.1 PropList](https://raw.githubusercontent.com/unicode-org/unicodetools/main/unicodetools/data/ucd/15.1.0/PropList.txt) 的25个码点集合。仅核对标准事实，没有复制第三方实现/数据文件，不把这些scalar helper称完整UAX文本分词、locale casefold或字体/输入法。

Array容量几何增长、Hash cached hash和<=1/2 load、List/事件节点延迟回收、buffer COW、cache LRU/预算分别承担可说明的成本。List token由动态桶索引查找，正常为期望O(1)，真实碰撞最坏O(n)；checked iterator保存仅由库生成的本人节点游标，先验owner/generation再前进，Next为O(1)，调用者不能伪造/改内部游标。受保护visit沿链一次走。长列表绘制/虚拟化仍由SCUI模型索引配合，不在每个鼠标move全扫模型。

本阶段不自行编译/QEMU，不编造性能达标数字。主代理在SCCLIB/C2/R1/pin与全部SCLIB源收齐后，按真实事务/alias/编码/配置roundtrip、回调重入、lease失效和动态库依赖组织必要定向证据；现有P0 GUI与Foundation未受改动部分复用，不重复全理论矩阵。

## 修订

2026-10-15：收齐SCLIB完整§4.2公共对象/事务/借用/回调/缓存合同与163个真实函数、SCCLIB28/SC32两个实际导入；include图30文件/176786B/295含预定义宏，静态阶段，构建和客体证据交主代理。
