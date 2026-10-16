# Studio2 首次 F5 的 INVALID

本草稿只准备固定原像补丁；没有修改主树、运行编译器或启动 VM。现场为 GUI01 的普通 UID/GID 1000 会话，截图 `build/m10a2-p6-gui-run-01/p6-native-studio-plugin-build-result.png`。截图确实显示 `构建结束：未取得当前产物资格 / 状态：-101`，输出面板为空。

`studio_build.c::namespace_pin` 把 `ScLeaseAcquireDesc` 的 `READ_ONLY_NAMESPACE` 标志与 `METADATA_ONLY` 一起传给 `ScFileLeaseOpenPinned`。实际值为 3，而 `kernel/fs_lease_open.inc:122` 只允许后者的 bit 1，必定返回 `SC32_E_INVALID`。该调用在保存阶段、所有输入 pin 和第一次编译命令之前。命令在 spawn 前已经写入输出，因此此故障不指向环境设置、捕获端点或编译器。应用、SC32、内核相关源摘要都与 GUI01 的输入 pins 一致。

不能只删除只读标志。OpenPinned 的目录路径不设置 `r->read_only`；这样得到的 namespace 会在 `fs_lease_index.inc:120` 阻止编译器的 `FS_LEASE_READ_OPEN`。本补丁使用已有公开的只读 Acquire 合同，保留真实 namespace 稳定性和原权限检查。

补丁仅触及以下三个文件：

- `studio_build.c`：Query 目录 revision → Open 目录 → Next 一行取得真实目录 epoch → Acquire 只读 namespace；清理阶段未完成关闭时不丢引用或前移。
- `studio_io.c`：共享 pin_close 同时关闭文件/目录；BUSY、AGAIN、PENDING 保留原引用，STALE 可确认原引用已失效；没有取得 lease 的临时目录只关闭句柄，不发 lease 0 的 Release。
- `STUDIO2.JSON`：只增加四个现有 SC32API 导入；不改 ABI、selector 或库。

Acquire 前，临时目录句柄已经由 `StPin` 持有。Next 或 Acquire 失败返回原错误，现有 `build_fail` 转入 CLEAN，继续处理这个句柄。Acquire 输出使用独立局部记录，失败时不覆盖临时句柄。成功后原子移交完整 lease 记录。清理时先关闭句柄，Close 可启动内核 detach；随后 Release 的 STALE 也表示原 lease 已被回收，允许清空本地引用。其它负错误保留引用，CLEAN 不宣称完成。

`ScDirectoryPage.generation` 是全局目录 epoch，`ScPathInfo.generation` 是这个目录的 revision，两者分别传入 `expected_epoch` 和 `expected_revision`。Next 的容量必须是 1，不能照抄 `worker_lease.c` 的零容量调用；公开 wrapper 明确拒绝容量 0。

`make_patch.py` 校验三个固定原像，只向本目录生成 `overlay/`、`studio-namespace.patch`、`PINS.json`、`REVIEW.json`。可由 Root 在 SandCore 目录运行 `python3 drafts/m10a2-studio-build-review/make_patch.py` 复现文本产物；它不编译、不运行客体，也不修改主树。应用补丁时以 PINS 的原像为准。

已经阅读 SCW 组的 `RENAMES.json`、`PINS.json`。SCW 不修改 `studio_build.c`；其 `studio_io.c` 修改处与 pin_close 不重叠。脚本在内存中核验同一 pin_close 变换对 SCW 的精确 after-image 同样可应用，并记录合并后摘要。不要用整个 overlay 覆盖已经合入 SCW 的文件，应应用局部 hunk。SCW 工程文件更名由其原补丁负责。

Root 应保留其它变更，以真实 SCCC→SCO 重生 STUDIO2.NATIVE.JSON；本目录没有手写或改写 NATIVE manifest。新增的四个公开导入是 `ScDirectoryClose`、`ScDirectoryNext`、`ScDirectoryOpen`、`ScFileLeaseAcquire`，都已经存在于当前 SC32API 导出表。此次源变化的 native refresh 标签应由实际闭包选出 STUDIO2，不重生未变化组件。

只完成源码路径审阅、固定原像/SCW 合并检查、文本 patch 生成与回读。补丁尚未构建，也没有新的 GUI 编译成功证据。足够的客体结束条件是修后同一真实普通会话 F5 能进入实际编译、链接和当前产物发布，保留命令/stdout/产物身份及关闭后的资源账；不追加理论矩阵。
