# SCSHELL v1 file job wire

2026-10-15，SOURCE CONTRACT；未构建、未运行。唯一私有合同为 `SCSHELLWIRE.H`，
worker、客户端与可信桌面只共享值协议，不传指针、FILE、lease或回调。Header为48B LE，
Hello32 / Begin64 / Item48 / Control32 / Decision40 / Progress96 / Ack232，头内size/version精确。

客户端先创建SESSION listener，名称为 `SCSJ.<pid>.<gen>.<sid>.<jobLo>.<jobHi>`。
十进制无前导零（数值0只写一个0），最多59B；job nonce本进程双字单调不复用，不是安全凭据。
endpoint max_frame=65536，queue/pending budget明确且动态，不设作业数上限。ACL要求本SID、
本UID/GID、NORMAL realm。然后通过真正受管 `ScProcessSpawn` 启动固定受保护路径：

```text
/SYS/SHELL/SCFILEWORK.SCX SCSJ.<pid>.<gen>.<sid>.<jobLo>.<jobHi>
```

flags为 `SC32_SPAWN_NO_STREAM_INHERIT`，不继承三条标准流；不会创建GUI session-lifetime
launch或调用命令解释器。客户端从真实ScJobQuery取得child PID/gen，再Accept；假连接只关闭，
不改作业状态。worker只用这个唯一argv，通过正常SESSION Connect连接，ScIpcPeer核名称中
的caller PID/gen/SID以及本进程真实UID/GID/subject/NORMAL。客户端Peer/每Packet.sender必须
是该真实child PID/gen、同SID、与caller相同的普通UID/GID/subject；双方均不信body自报权。

所有帧 type=SC32_IPC_FRAME_DATA。序号是Header.request_lo/hi的本连接command sequence；
HELLO唯一用0，其后客户端从1递增，不复用，所有同一PLAN fragment共享seq1。
job_lo/hi是Begin/Hello/结果中的作业身份。REPLY=2、EVENT=4，MORE=1仅PLAN fragment可用。
非fragment offset=0且total_bytes=body_bytes；fragment按offset==已接长度前进，拒重叠/缺口，
frame含Header不超过65536。各body offset相对完整body起点，范围不重叠，padding必须零。

1. 客户端发送HELLO0，worker HELLO_ACK0回自己的真实features与相同job nonce；不是FS提交。
2. 客户端按最多65488B body/帧发送PLAN(seq1)。Begin是64B，紧后count×48B Item表；
   字符串区4B对齐。source/destination都是解析一次后的绝对路径，含唯一NUL最多65B。
   每item_id非0且唯一。MKDIR可source_bytes=0，其它有source；目标空时COPY/MOVE由全局
   destination目录与source basename生成，RENAME/MKDIR要明确逐项目标。
3. Begin.recovery必须明确普通可写、在所有被锁source/dest子树之外；无可用恢复路径就明确失败。
   Begin.conflict_policy=0（逐项询问），final_mode<=63，默认3。没有默认覆盖或隐式保源UID/GID。
   source/target revision只有对应EXPECT flag时非0；跨进程不传object_id当birth ID。
4. 收全PLAN仅准备拥有内存。收到COMMIT(seq2,Control32)后才规划/获得lease/执行；worker回
   ACCEPTED(REPLY,seq2,Progress96)，随后PROGRESS/CONFLICT/ITEM/RESULT(EVENT，相关当前seq)。
   Header.state_epoch是单调进度revision，不回绕；实时progress可合并，已确定ITEM不得丢失。
5. CONFLICT用Ack232给真实source/target观察和非0 conflict_epoch。RESOLVE使用下个command seq、
   exact job/item/conflict epoch及explicit source/target revision；Replace/Skip/Rename/Merge/Retry/Cancel
   仅影响尚未提交的当前项。Rename随后附absolute新目标。确认后对象变化返回新冲突，不复发旧提交。
6. CANCEL是Control32，用下个command seq；worker只在有界安全点停止，CANCELLED_ACK与最终
   RESULT确认实际终态。取消不回滚已发布项目；Close/进程退出的强制停止不构成durable ACK。

COPY/MOVE/DELETE/RENAME/MKDIR与RECURSIVE都是真正worker动作。PENDING lease只以同
operation_sequence Advance，不能重发Publish/Remove begin。IO部分prefix按确知字节推进；
复制顺序为读取稳定源→私有候选→真实Flush→条件Publish→durable ledger；MOVE保留目标保护到
删源结束，允许条件rename快路径。SOURCE_RETAINED独立per-item状态，显示“已复制，源未删除”。
只有本次真实worker的Packet及匹配job/item/观察可成为ledger；别的进程发“完成”没有删除权。

Ack.stage明确区分NONE/CANDIDATE/FLUSHED/DESTINATION/RENAME/SOURCE_REMOVED/DELETED/
DIRECTORY。DURABLE仅后端确认提交且必要恢复记录真实Flush之后置位；UNCERTAIN不给durable
成功ACK。OBSERVATION_LIVE不是持久身份，最后释放后清flag/object值。最终RESULT的Progress96
包含已知64位bytes/items与明确status/cause/domain/raw，不用100%暗示全部成功。

Progress.flags独立解释PRIVATE_PREFIX=1：bytes包括当前私有候选已接受的Write prefix，
它仍未Publish，取消/重试丢候选时可以回退。不能据此设置Ack.DURABLE或删源。
Ack232.bytes仍只包含真实已确认提交/删除事实，不混合未提交prefix。

源remove前先Flush记录目标已提交的确知事实；恢复文件可用首私有CreatePinned+Publish(mode3)，
随后带长度/version/CRC的append并真实Flush，非破坏性COPY可批量64items/64KiB。
记录可能滞后，重启只核当前对象、权限和候选，不自动重放source删除/覆盖或认同名新对象。
协议不要求每字节或整计划反复写日志。worker owns plan/lease/64KiB buffer/ledger，caller owns
listener/channel/managed job/结果快照；EOF无最终RESULT的剩余项目为UNKNOWN，已知durable项保留。

Send返回AGAIN表示未入队，可保同一frame重试；Send成功之后EOF/超时不得自动重发有副作用frame。
Peek先知长度再申请拥有候选，Receive不足容量不消费。Query/Step的等待使用真实BrokerWait。
跨窗口Drag只冻结合法target意图；ScDropComplete可以在handoff轮次建立独立job后消费短票，
它和durable MOVE ACK分别核验，任何借用方签名无效。目标若不能提供本caller可信worker ledger，
保源并显示未完成；文件库绝不按第三方UI的COMPLETE删除源。
