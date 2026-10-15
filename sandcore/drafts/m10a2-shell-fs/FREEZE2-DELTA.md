# 租约 freeze2 源码差异

2026-10-15，SOURCE ONLY。Root授权的两项权限修复和一个必要条件mode操作；没有应用到现kernel/user，没有构建、编译或VM。

freeze-v1.json保留原freeze.json原字节，SHA256为29ce62c76ad69c9817ef9b593471e1a02afaee4bd3098ed9a60515a4129daa04。新freeze.json逐文件给出旧/新SHA差异，不用当前源码重建旧来源。

1. 正常NS快照只收当时META可见条目：不可读文件可以单列并逐项Skip；不可读目录本身可见，其内部名字因祖先无R被滤掉。每页发布前复验根R和所有当前META，撤权不部分发布；条目不是永久读取授权。
2. OpenPinned.flags2 / Info.flags256为existing-only METADATA_ONLY。FILE READ_STABLE/open_flags0、DIR NAMESPACE/open_flags0；Query/Close可用，IO/seek/flush/DirNext/Snapshot拒绝。FILE私有句柄标记在Release后仍存到Close，不能重Acquire或借句柄恢复IO；不能作子项parent grant。元数据目录epoch0/无SNAPSHOT表示未枚举。条件rename/remove/EXACT覆盖仍核parentRW、META、exact对象/revision、foreign pin与保护后代，不要求对象自身R/W。
3. 新ScFileLeaseSetMode / selector70b / error-operationf0b / Info.operation6。ScLeaseModeDesc48B只改rw；原11入口/布局/selector不变。真正owner或auth_can_manage授权沿旧fs_permissions，SYSTEM所属仍核硬件MOD链，不额外要求fileW/parentW。真实16sector目录bank+CRC+superblock状态机只修改exact source.mode/revision，不改名字、UID/GID或extent；不确定提交不给durable ACK。供目录COPY完成children后后序恢复最终mode。

现文件挂钩仍只在integration.patch里，generator只写本隔离目录。Root正式整合时须采用freeze2头/12出口/新patch，不混用v1旧私有FILE字段。建议后续最小实际检查仅覆盖Root已指出的权限路径、最终只读目录与确认/不明提交生命周期；本文没有新增全卷矩阵。
