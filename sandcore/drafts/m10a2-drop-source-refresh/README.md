# 文件拖放的真实终态

本目录只交隔离源码和精确补丁；没有修改主树、构建、启动 VM 或改写数据盘。状态是 `SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED`。GUI02 的自动来源刷新失败继续保留，不能以原文件操作成功替代本次协议验收。

根因有两层：Files 在确认动作、创建文件作业之前已返回 TRANSFER，旧 SCUI 随即发送传输 COMPLETE；稍后用户选择 MOVE 只改了应用副本，源端仍收到原协商 COPY。Desktop 收到 COMPLETE 又只清拖动状态。因此在旧 ACK 上加一次刷新仍会过早。

实现把短票交接与业务完成分开：显式 v2 的 HOLD/READY 在模态框之前消费原传输票，随后只通过同一认证 channel、owner、窗口代数、nonce、原票轮次、format 和动作交集传业务结果。接收方从 receipt 的原 FILES 正文创建绑定作业；逐项 `DONE + DURABLE` 且真实 `RENAME(4)` / `SOURCE_REMOVED(5)` 才累计来源改变。源端只置一次普通目录失效，仍由真实分页查询决定哪些图标存在，不再删源、不加目录轮询。

新增公共头只有 `SCUIDROP.H` 和 `SCSHELLDROP.H`。SCUI 为 ABI1.4/library5，原170入口原顺序保留，追加4个；SCSHELL 为 ABI1.2/library3，原96入口保留，追加3个。旧 `SCUI.H`、`SCSHELL.H`、`ScUiDragData`、`SC32DROP.H` 和内核 Drop 实现保持原字节。旧 `ScShellDragBegin` 仍 flags0，即时 ACK 语义不变。只有 Files/Desktop 改用 BeginEx 的显式选项。完整合同见 [SCUI-DROP-DEFERRED.md](SCUI-DROP-DEFERRED.md)。

`drop-source-refresh.patch` 是29个路径的局部差异：22个现有文件、7个新增文件。`PINS.json` 固定每个前像、结果和总补丁；`overlay/` 仅供审阅，不能整份覆盖主树。manifest 是源合同，未手改 `.NATIVE.JSON`；真实 SCCC/SCO/SCDB 和动态导入检查仍由 Root 统一生成。

**与 TEXT1 修复的合并边界：** `shell_clipboard.c` 的顶部文本函数完全未改。本组只改原 `ScShellDragBegin` 起的 Drag/Drop 尾段。优先按 `shell-clipboard-lower.patch` 和 `HUNK-PINS.json` 合并；若 Root 已合顶部修复，整文件 before/result SHA 必然变化，应核对尾段 SHA 与上下文并记录组合后的真实摘要，不能为了匹配本组 SHA 覆盖顶部。其它文件也只应用对应精确 hunk，保留不相关的 Session/ARGB/布局等现有正文。

SDK stage 已追加两头和合同文档；make 显式登记 SCUI/SCSHELL/Files/Desktop 的新依赖。公共 app 头集合只排除这两个新头，避免未使用它们的其它应用仅因 wildcard 重编；没有降低来源或 unused-import 校验。

取消、EOF、节点销毁与退出都释放 receipt/channel；交接后不因失焦或隐藏取消正在确认或执行的事务。已知源退出且 COMMIT 尚未完整发送时，绑定作业直接停止本人 managed child，不能先发 COMMIT 再补 CANCEL。已提交部分按原 ledger 保留，负终态仍可携真实来源改变；不明结果只触发一次复核，不能记成功或自动重发 MOVE。晚到结果携本次 Begin token，不能清掉下一次物理拖动。

`python -B drafts/m10a2-drop-source-refresh/make_patch.py` 只重建草稿；随后 `python -B drafts/m10a2-drop-source-refresh/seal_review.py` 检查前像/补丁可重建性、导出前缀、头布局、尾段隔离和词法边界并封签。运行目录为 sandcore。静态 PASS 不代表 C 编译或客体通过。

Root 整合后的必要出口是原实际场景：Desktop → Files 默认 COPY、确认选择“移动到目标”，核真实目标和源、DURABLE stage4/5、最终实际 MOVE 与来源改变，无 F5 自动消除来源图标；再确认取消保源和原即时分支。异常/部分提交沿既有 owner/ledger 账核对，不新增逐组件 VM 或矩阵。现有复制/移动和取消证据路径及 SHA 在 `SOURCE-PINS.json`。

修订记录：2026-10-16，文件拖放短票交接、认证终态和来源视图失效源码交付；未构建、未客体验证。
