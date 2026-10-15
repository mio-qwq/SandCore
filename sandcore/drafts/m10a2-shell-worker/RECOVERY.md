# SCFILEWORK v1 恢复ledger

SOURCE ONLY，未运行。格式是本项目自写的i386 LE值记录，不依赖libc/FPU或第三方实现。读取只能用于展示确知事实、人工核对与新确认，不是自动执行脚本。

文件由caller明确给出绝对路径，不能位于任一source/destination锁子树。worker first CreatePinned不可见文件，写header并真Flush，再absent Publish(mode3)；不覆盖既有恢复文件。随后保持真实WRITE租约顺序append，部分Write只推进实际prefix，错误后不从0重写同记录。

## Header64

| offset | 宽度 | 字段 |
| --- | --- | --- |
| 0 | 8 | `SCSFJ1\0\0` |
| 8 | 4 | size=64 |
| 12 | 4 | version=1 |
| 16 | 8 | job_lo/job_hi |
| 24 | 8 | worker PID/generation |
| 32 | 8 | 真实uid/gid（s32） |
| 40 | 8 | 真实realm/session |
| 48 | 4 | flags0 |
| 52 | 8 | reserved0 |
| 60 | 4 | 前60B CRC32 |

## Frame

24B前缀后紧跟原232B ScShellWireJobAck；record_bytes固定256、version1、body_bytes232。前缀依次6u32：record_bytes/version/type/sequence/body_bytes/crc。sequence从1单调，无回绕；type为stage0..7，或START=0x100、FINISH=0x101、REMOVE_INTENT=0x102。CRC覆盖前20B及232B正文，跳过crc字段。算法为反射CRC32多项式0xedb88320，初/末xor0xffffffff，可连续更新。

Ack正文有job双字、真实worker PID/gen、item、job operation、stage、status/cause/domain/raw、known bytes、source/target revision及68B绝对path。所有reserved和路径NUL后padding0，完成记录无LIVE object值。object_id没有跨重启意义，即使日志保留它也不得当birth-ID授权。

磁盘record中的DURABLE表示该record写入前后端已确认的FS事实；RECOVERY_WRITTEN不预写来自证本记录已Flush。R3内存中的该位和发出的durable ACK，只在真实ScFileFlush成功后置位。一次批量至多64records/64KiB；COPY完成记录可批量，开始删除源前的目标事实/REMOVE_INTENT必须立即Flush。最终FINISH也真Flush。

读取端逐项先验header/版本/长度/CRC/sequence/job/worker事实、UTF8与padding，再发布解析结果。末尾不足24B、body不完整或CRC错都只表示尾部未确认，不把坏尾续成成功。不能跳过坏record再拼后面的字节，也不能把FINISH缺失当全部失败回滚；前面已确认事实仍保留。

日志可能滞后，崩溃时某个namespace operation可能已提交而其完成record未落盘。尤其REMOVE_INTENT不是“已经删除”，最后known stage不是给同名对象的删除权限。重启后PID/代数/open-pin不能重用，所有继续动作都必须重新查询当前路径/权限/revision、让用户明确确认新job；不自动覆盖、删源、重放append或认同名新对象。

UNCERTAIN超级块/flush故障不给durable ACK，不释放可能被bank引用的extent，不宣称物理扇区撕裂原子。已发布对象不因Cancel、日志失败或Close被回滚。元数据真实提交以FS freeze2合同为准；本文没有运行PASS或断电实验。

修订：2026-10-15，固定header64/CRC frame256及已确认事实/日志Flush分层，源码未验证。
