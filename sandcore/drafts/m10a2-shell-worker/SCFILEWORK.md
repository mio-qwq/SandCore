# SCFILEWORK：受管 Ring3 文件作业

2026-10-15，**隔离候选源码，未接入、未构建、未运行**。本目录不是可安装产物，也不是Phase5或M10a2完成证据。现kernel/user/Make/tools保持原样；FS租约另有已交Root的freeze2，本文不代替其实际验证。

## 发行位置与依赖

正式应用拟安装在受保护的 `/SYS/SHELL/SCFILEWORK.SCX`。聚合为 `SCFILEWORK.C`，动态清单为 `SCFILEWORK.json`；main严格为 `int main(void)`，由已有自动SCX CRT进入。没有SCAPI.H、asm、私有寄存器桥、静态复制系统库或SCSHELL服务权限。SCX1旧容器/262144B装载限制、64B动态CRT、0x3d0000映像预算保持，实际产物大小待统一构建。

显式依赖SC32API ABI1.3/library4、SCCLIB ABI1.0/version1、SCLIB ABI1.0/version1；共69函数imports（SC32 34、SCCLIB 12、SCLIB 23），没有data imports。SCCLIB的真实atexit/ROOT R1依赖沿其正式清单解析。worker不依赖SCSHELL.SCL或SCUI.SCL，公共UI库不被复制到worker。

唯一共用值协议是另一owner的 `drafts/m10a2-shell-r3/SCSHELLWIRE.H`。正式放 `user/scshell/SCSHELLWIRE.H`，worker构建加 `-Iuser/scshell`；不复制第二份，不进公开SDK INC。SC32LEASE.H来自FS freeze2的拟正式公共头，不把private WIRE暴露给应用。

## 身份与握手

caller创建SESSION listener并配置真实SID/UID/GID/NORMAL ACL，然后 `ScProcessSpawn` 固定绝对路径，flags=`SC32_SPAWN_NO_STREAM_INHERIT`。绝对根斜杠必需：含斜杠的进程命令仍按CWD解析，相对SYS路径会在Files目录下找错。argv只有：

```text
/SYS/SHELL/SCFILEWORK.SCX SCSJ.<callerPID>.<generation>.<SID>.<jobLo>.<jobHi>
```

名称是十进制canonical值，无前导零，最大59B。它不是秘密或授权。caller从真实ScJobQuery取得child PID/gen后才Accept；worker从ScProcessSelf/ScIdentityQuery取得本人事实，再ScIpcPeer核真实parent PID/gen、SID、UID/GID、subject UID/GID、NORMAL realm与非硬件flags。普通UID0仍是NORMAL，不推成SYSTEM。worker不接受UID/GID/realm的payload自报值，不创建登录或SYSTEM连接。

HELLO0、PLAN1各fragment、COMMIT2及后续递增双字command sequence完全按唯一WIRE。每个Packet再核真实sender全部身份。PLAN只准备拥有内存，COMMIT后才取得FS租约。路径必须已由caller解析一次为绝对规范UTF8，最多65B含唯一末NUL，4B零padding、所有offset整区/非重叠/乘加先验；worker只做词法规范检查，不再叠自己的CWD。无用flags/reserved拒绝。源/目标相等、相互包含或跨选择重叠修改的危险范围在副作用前拒绝；RENAME同一名字是无新提交的已满足意图，不伪造持久提交。

## 拥有与调度

worker是caller拥有的真实受管job，不是同SID后台SYSTEM服务。每个root、发现的item、排序vector、路径hash、消息候选都动态分配；数量受真实内存、ID空间和v5卷容量约束，没有固定任务/作业数组。源是本人READ_STABLE或NAMESPACE强pin，目标是本人不可见候选或独占目录范围。ScFile/ScDirectory/lease只留在worker内存，不走IPC。

Source namespace一次捕获META可见成员，每页64行；无自身R目录本身可见，隐藏深层不会泄露。FILE真正读取仍逐次fileR，命名操作逐次真实parentRW，mode修改逐次owner/manage；借用范围token不能替代权限。目录MOVE/RENAME可用METADATA_ONLY pin实现无自身R但parentRW的旧合法整体改名；epoch0/无SNAPSHOT明确未枚举，不把members0当空目录。

Root选择去重用排序/前缀，成员索引用SCLIB hash，工作顺序用本人vector排序。稳定item节点不移动，View不被当可写排序内存。文件前序处理、目录后序收束，目录之间只持真实需要的owner链。每次主循环处理一个frame、一个文件步骤或64行快照，然后让出；空闲用真正BrokerWait的同gate ready检查与安装等待，EOF单列，不把空队列或STREAM旧事实当EOF。

一个64KiB数据buffer、64行row buffer、64KiB日志buffer。FILE的公开同步实现可能在一次extent迁移中内部多次128-sector续进并Yield：其它任务与WM可运行，但R3软Cancel在这次公开调用返回边界才被消费，不能宣称恒定64KiB延迟。强制ScJobCancel由kernel真实owner清理处理。元数据每次Advance沿FS的16-sector预算，未接受与已接受operation严格区分。

## 动作与冲突

| 动作 | 实际路径 |
| --- | --- |
| COPY FILE | exact源READ_STABLE → 私有CreatePinned → 64KiB Read/Write → 真Flush → exact/absent Publish → 恢复记录 → ITEM |
| COPY DIR | 固定成员计划，空目录mode3先确认发布，子项逐项复制，后序SetMode最终权限；不是整树原子复制 |
| MOVE | 同卷优先条件Rename，整个目录用一次v5命名提交；仅后端明确未受理的能力缺失才走真正copy+remove路径，不把IO/超时换路径重放 |
| DELETE | 文件exact Remove；递归时固定成员后序逐项Remove，目录必须真正空，失败不删新出现或未确认的成员 |
| RENAME | real META身份pin + source/target exact条件，保旧readonly自身权限下的parentRW合法路径；不先remove目标 |
| MKDIR | 私有空目录 → absent/exact Publish mode3 → 真SetMode最终mode；父目录须实际存在 |

覆盖已有文件时，候选不用既有target名。它在明确独立recovery_path的真实可写父目录中以动态唯一短basename独占创建，始终不可见，Flush后条件Publish到最终target；目标原正文在确定提交前保留。名字仍受v5真实63B rootless长度限制，短名空间用尽明确NO_SPACE，不能截断成另一对象、借SYSTEM临时目录或删除碰撞项。

默认不自动覆盖；逐项CONFLICT给真实source/target观察、非0 conflict_epoch、cause/domain/raw。Replace、Skip、Rename target、Merge、Retry、Cancel只接受当前job/item/epoch和显式revision。决定后再变化会生成新询问；前置拒绝不构成新operation，受理PENDING后只用原operation_sequence Advance。CRC/bank/superblock不明为UNKNOWN且无durable ACK。

文件Retry按明确新观察重新pin；完整已Flush但尚未受理Publish的候选可复用，避免重复读取。目录权限恢复或初始快照失败的显式Retry是新的本人规划轮次：正常仍活原快照可重读，未枚举子树取得新的授权快照；只添加未提交成员，已完成事实不回退、不重放。新成员ID从最大root ID之后单调分配。已发布目录只重试剩余mode或空源目录Remove，不从头复制；重开META pin仍按用户确认的revision及实际空目录条件验证。

不可读项可以独立Skip。目录Skip传播到其未执行后代；失败、取消或Skip后的已发布目标保留。部分成功逐项显示，不回滚前面成功文件。MOVE源删除失败单列SOURCE_RETAINED（已复制/目标已确认，源未删除），保留目标pin直到本项删除确定完成或明确放弃。

PRESERVE_MODE是真实现：FILE在首次Publish带最终mode，DIR先mode3完成children后用ScFileLeaseSetMode（70b）后序改rw；不保源UID/GID，不暗称保mtime/birth-ID。合并已有目录仅在明确PRESERVE_MODE时改其mode，仍需真owner或auth_can_manage；默认不改外来目录权限。

## 进度、确认与取消

Progress96的bytes是已确认逻辑bytes加当前私有候选的已接受Write prefix；PRIVATE_PREFIX=1明确“正在复制，未提交”，丢候选重试可以回退。Ack232.bytes只含已确认目标/删除的knownbytes。total_known为0时不能画假100%。实时progress可合并，已确定ITEM不丢；每连接待发有明确256KiB背压预算，主循环停止新的FS副作用而等待真正WRITE ready，洪泛query不扩成全局门槛。

完整目录Rename只发根的真实持久改名事实，覆盖同一受锁子树；不为未执行子计划伪造新revision或逐字节复制。未枚举时total_known0。OBSERVATION_LIVE只在当前真实pin仍活的询问出现，完成消息清object ID与此位，保revision历史事实，绝不把open-pin观察当持久ID。

COPY fallback的MOVE只有目标COMMITTED/DURABLE、恢复ledger真Flush之后才发起同源身份/revision的条件Remove。日志失败不删源。源删失败不删除已发布目标。DropComplete只是短票handoff消费，worker不导入或调用它；IPC Send成功也不等于持久确认。源端Cut reconciliation只能认本次真实worker身份、job/item/paths与stage/DURABLE结果。

软Cancel停止未接受操作，已接受metadata先在安全点取消/推进至确定状态。已确认rename/remove仍按真实成功结果报告；取消不恢复旧名字、不撤已发布目标。正常返回及atexit只运行一次cleanup；FILE Close先保租约提交判断，后续ID Release/GC只以本人值续进。退出/logout/异常由kernel owner链兜底，不能把强制停止或EOF伪装成durable ACK。

## 源码职责

| 文件 | 职责 |
| --- | --- |
| SCFILEWORK.C | 自动main、真实身份握手、受管主循环、ready等待、最终结果/退出 |
| SCFILEWORKPRIVATE.H | worker独有拥有模型、稳定节点与跨职责前向合同 |
| worker_lease.c | 公开lease/FILE生命周期、exact条件构造、规范绝对路径 |
| worker_plan.c | 动态root/member去重、固定snapshot分页、前/后序索引 |
| worker_protocol.c | 纯值frame校验、真实sender、command sequence、背压/结果 |
| worker_recovery.c | v1 header/CRC frame、不可见首建、append/Flush、持久事实标记 |
| worker_transfer.c | COPY/MOVE/delete/rename/mkdir、prefix、冲突、后序mode/remove |
| worker_retry.c | 显式新snapshot规划与稳定索引重建，保已提交项不重放 |

恢复格式的精确字节见 `RECOVERY.md`；正式接线与Source范围见 `INTEGRATION.md`。尚无GCC/SCCC产物、运行时间、资源回收或性能PASS；v5每次namespace提交仍需全bank序列化，仅分16sector推进，不能据短步就宣称IO总量优秀或整个目录原子。Root将按完整Phase5统一构建与少量实际风险验证，不为本worker单独扩大矩阵。

修订：2026-10-15，收齐隔离真实受管worker、动态计划/IO/冲突/日志/后序权限/显式Retry，未接线未验证。
