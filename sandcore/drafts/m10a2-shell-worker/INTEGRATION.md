# SCFILEWORK 隔离接线清单

2026-10-15，SOURCE ONLY。本文是Root后续统一整合清单，不修改现Makefile/user/kernel/tools，也不请求额外用户批准。

1. 将本目录8个C/PRIVATE职责文件及SCFILEWORK.json放 `user/shellworker/`；实际清单以freeze.json为准。应用聚合include自己的.c片段，不把它们分别链接成重复TU。`SCFILEWORKPRIVATE.H`只属应用，不进SDK。
2. 唯一共用SCSHELLWIRE.H来自R3 owner，正式 `user/scshell/SCSHELLWIRE.H`，构建仅加 `-Iuser/scshell`。隔离审阅路径 `-Idrafts/m10a2-shell-r3`；公开SDK只有user/SCSHELL.H，没有WIRE/PRIVATE别名。
3. 先整合 `drafts/m10a2-shell-fs/` freeze2（12个真实lease出口、SC32 ABI1.3/version4/真实v5可用cap hi bit0），再与Root host9出口合并。FS freeze2 SHA为e43a2999dc877c21251a0f7baa9827bf016104e6690339e4c7e0ded7da3bb836，本worker不改该底座。Query/Advance/stream通知/owner停止由Root已审挂钩接线，不能只加公开声明。
4. GCC和原生SCCC均按 `--dynamic SCFILEWORK.json` 生成独立APP；使用当前SCCC与标准SDK，不放宽旧SCX容器/BSS/stack预算。真正链接imports、库格式、客体执行、收束页账由统一主控记录，宿主字节生成不能冒充客体执行。
5. 安装 `/SYS/SHELL/SCFILEWORK.SCX` 前必须把该发行路径纳入实际SYS保护/可信安装链，普通/root主体不能通过旧path或新lease替换worker。caller只Spawn固定绝对路径，不能把任意可执行体当可信持久ACK提供者。Root未来构建/包采集需纳入此应用和新公开SC32LEASE.H，SRC含职责片段、SCSHELL唯一私有头及leaseapi；private头不进公开INC。
6. caller的真实SCSHELL R3实现负责创建SESSION ACL listener、受管Spawn、ScJobQuery childPIDgen、Accept/每Packet身份检查、动态结果容器、bounded Step与Close。服务不以SYSTEM代办普通文件IO。Drop短票和long job独立，COPY/MOVE日志只认当前真实worker来源。

当前源码提供COPY/MOVE/delete/rename/mkdir、真实ReadStable/candidate/Flush/conditionalPublish、后序目录SetMode、foreign pin Busy、显式冲突/Retry/Cancel、版本化恢复ledger与动态cleanup；没有假注册完整能力、全局固定作业数、整文件R3缓存、close/reopen伪flush或NOT_SUPPORTED占位动作。

静态文字收集的literal include闭包23文件、178590B、宏名保守上界251；69个调用imports均有公开声明，无缺少的私有函数正文。`static-source.json`只记录读源码/文本匹配与依赖SHA，不是CPP展开、C语法检查、链接或运行结果，不能据此宣称产物通过。后续source格式/共同头变化时以新freeze和实际编译测量为准。

尚未进行：正式接线、GCC/SCCC构建、生成SCX字节/映像大小、原生客体编译和实际执行、普通登录SID/PIDgen/ACL路径、Readonly directory最终mode、prefix/uncertain/资源回收与性能测量。Root应在完整Phase5收齐后按实质风险安排少量定向检查；既有Phase4有效证据保留，不为本隔离目录重跑旧大矩阵，也不把它们当本workerPASS。

修订：2026-10-15，明确正式路径/唯一WIRE/12出口/可信安装与统一验证边界。
