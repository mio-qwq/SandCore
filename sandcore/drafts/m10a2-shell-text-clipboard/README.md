# Shell TEXT1 剪贴板窄修复

仅修改 `user/scshell/shell_clipboard.c` 的 `ScShellClipboardText` 与 `ScShellClipboardReadText` 两个函数。没有改 include、任何 helper、下半部 FILES/Drag/Drop、公开头、ABI 或调用号。原像/后像及两函数独立摘要见 `PINS.json`；本目录为源码补丁交付，未构建、未调用 VM、未验证新字节的 Calc→Notes 实际流程。

根因与已有合同一致：`docs/SC32-BROKER.md` 的 TEXT1 必须有唯一末尾 NUL，`bytes` 包含它；Broker 的 `br_clip_validate` 和分段 Offer 都执行该条件。旧 writer 把不含 NUL 的 ScUiText span 直接 Offer，正确 span 无法发布；旧 reader 又拒绝任何 NUL，所以无法读取合法 TEXT1。SCUI 的 Copy 已传正文+1，Paste 也明确验证末尾 NUL、无内嵌 NUL、正文 UTF8 后只粘贴 bytes-1。

writer 先核记录和输出、公开 64MiB Offer 上限，再扫描正文，避免 `bytes+1` 回绕或扫描已经不可能合法的极大 span。分配独立 owned `bytes+1`，仅复制 span 的正文并手动追加 0，不读取源 span 之外的终止字节。public Offer 同步完成内核复制（大内容内部按64KiB分段并让出），返回后立即释放 owned。Begin/Offer/Commit 任一失败均撤候选，成功 Commit 消耗候选；epoch CAS 语义不变，失败不发布新内容。先用 operation80 保存实际错误，再 Abort，避免清理调用覆盖失败来源。

预算不能继续借旧 `sh_clip_offer`：它对大正文只申请等于正文的 budget，而 `br_clip_begin/br_clip_offer/br_offer_begin` 要求 budget 同时容纳格式元数据。此补丁不改该 helper，在文本函数内使用公开事务，给序列化正文额外4096B余量（与现 SCUI Copy 的做法一致），接近上限则饱和到64MiB。所有加法在64MiB界内，实际元数据 charge 仍由 Broker 严格核验；上限附近可能 RANGE，物理不足可能 MEMORY，不保证64MiB正文一定可发布，不硬编码内核私有结构宽度。

reader 先要求 n≥1、最后一字节为0、正文内无0；framing 错误为 `SC_SHELL_E_PROTOCOL`，与 Shell 现有 counted-string 解码约定一致。再验证 n-1 字节 UTF8，编码坏返回 `SC_SHELL_E_UTF8`。ScString 只保存 n-1 正文字节，其内部终止字节由 SCLIB 自己管理。合法空文本是1B的 `00`，返回长度0；`out/epoch` 仍只在全部成功时赋值。

静态核验：补丁只含两个函数的差异；前缀、两函数间的 `sh_clip_read` 及从 `sh_clip_files_encode` 开始的11658B下半部逐字节不变。malloc/free/string 声明已由 SCSHELLPRIVATE.H 提供，无 include 增量；全部调用符号已在当前 SCSHELL.NATIVE.JSON 导入中。只检查源码和摘要，没有新增运行夹具或构建。

固定整文件原像 SHA256：`fb5acb791a2be1ca7fca4eab2c3b27860984dc7b48c0538a3222ab30a57e8f2a`。
固定整文件后像 SHA256：`a9999323fc798061a04dc5842174422ff11b97f33e2bbd338519a3745b03101b`。
`clipboard-text.patch` SHA256：`c6db96f5c1d98595b2b5f40a6c4896bd156776d494178da1809eb425bd9f656e`。

Root 与 files2 agent 的下半部补丁合并时：不要用完整后像覆盖已经合入的 Drag/Drop。若整文件原像已因下半部变化而不同，先核 `PINS.json` 中两个函数的独立原像，再只合这两个 hunk；前后像是审阅和精确来源载体。主树当前保持冻结，后续统一构建、原生 SCCC/SCLD 和实际 Calc Copy→Notes Paste 的最小回归由 Root 执行。
