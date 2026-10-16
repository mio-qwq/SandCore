# 稳定读租约的共享打开

READ_STABLE 固定本人 FILE/object/revision 与名称，排斥内容修改和条件改名、删除、替换；原公开合同没有声明稳定读之间独占。WRITE_EXCLUSIVE 才要求唯一实际 description/对象引用。多个稳定读分别拥有现代 FILE、游标、owner PID/gen 和 lease 强引用，共用活跃 native 对象，并不共享解码器或游标。

本次只增私有冲突操作 `FS_LEASE_STABLE_OPEN=4`，不是 SYS selector，也不是用户可传的新 flags。仅 `fs_lease_acquire` 与 `fs_lease_open` 的 FILE/READ_STABLE 请求使用；写、目录 namespace、候选创建仍沿原 CONTENT/TREE。旧 READ_OPEN 保持允许普通只读打开的原语义，不能把它用于稳定租约，因为该分支也放过 WRITE_EXCLUSIVE。

STABLE_OPEN 仅跳过覆盖本规范名的 slot0 READ_STABLE：必须为 FILE、READY 或 COMMITTED，且没有 cancel/release、queued/stage、CANDIDATE/REVOKED/UNCERTAIN。PENDING 的 rename/remove/set-mode 虽然仍是 READ_STABLE kind，也不能放行；slot1 目标保留永远不通过这条例外。WRITE_EXCLUSIVE、独立 namespace、取消/清理/异常中的记录均继续冲突。原显式 namespace grant 的 owner/gen/范围/权限检查与 `lease_allowed` 规则保持。

参数、权限与失败顺序不动：请求/输出先完整预验，路径只解析一次；Acquire 要求本人未绑定句柄、精确 object/revision、真正 READ 打开方向；OpenPinned 要求当前精确 revision、真实 R，METADATA_ONLY 仍只授 META 且禁正文 IO。卷故障、事务冲突、短期 metadata 锁、不存在/种类/版本错误及所有分配失败仍沿原路径返回和回滚。

同 PID 不授跨租约许可。读 Acquire 不要求 alias 唯一，与其原实现相同；本次不放宽写 Acquire 的 `aliases==1 && native refs==1`，不放宽写 OpenPinned 的 native_busy。新增读仍经过 `lease_open_object` 的 pending/unpublished/invalidated/ref 溢出检查，再建真实 description 与 lease 强引用。普通已打开 WRITE 描述符可以存在，但其后每次 Write/Truncate/Flush 都经过 `lease_native_guard`；对象链出现 READ_STABLE 即 BUSY，不能用旧 writer、同 PID alias 或只改打开 flags 越过稳定读。已在推进的 native pending 会在取得 stable 前被拒绝。

两个稳定读存在时，typed Rename/Remove/SetMode 的 unique binding（单 alias、native refs==2、单一 lease_head）与 TREE 检查仍拒绝修改；其它 reader 释放后才可重新尝试。新的写打开、普通正文写、旧覆盖/rename/remove 和祖先 TREE 冲突不变。READ_STABLE 也不等于 durable 快照，没有新增掉电或整文件原子性承诺。

权限边界必须区分：旧 `fs_permissions` 本来没有 lease conflict gate，合法 owner/管理者仍可改 UID/GID/rw，保护路径、MOD、SYSTEM 和原权限检查仍照旧；每次 IO 的 `native_access/fs_access` 重新检查真实当前权限。普通 modern pin 不屏蔽旧 chmod 的原合同保持。因此不能称“所有权限变更都被 stable 排斥”。typed lease SetMode 会被其它稳定读的 unique/TREE 检查排斥。本稿不改变旧 SCAPI 入口或给 lease 追加权限，外部身份撤销也不受阻。

临界步骤保持原单核中断门执行链：精确冲突/版本/权限验证至创建索引、绑定 FILE 没有新增 yield 或 PENDING 点。关闭/Release/owner 退出仍只撤各自引用与名字链，最后对象引用才回收；未增加固定读者表、全卷扫描或后台计时器。

本组只有隔离源码、精确前像 patch 与静态定位。未构建、未运行 VM；它是可审候选，并不代替 Root 对 worker 原位 seek 与候选交换路线的选择，也不能宣布同曲重播/seek 已经通过。

修订记录：2026-10-16，稳定读共享打开的私有冲突类型及权限/alias/writer边界；公开 ABI、调用号和旧普通读取行为保持。
