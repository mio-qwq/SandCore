# Drop 冻结交付的窄补充

仅审阅 `drafts/m10a2-drop-source-refresh` 的冻结来源；原交付和 main 均未修改，没有构建、VM 调用或新增夹具。本目录的 `review-supplement.patch` 基于原 inc SHA256 `9b37cdeefd5249c44b3200d0d13d09775efddbb058ea777e650ce3b891f94851`，仅两类修正。

1. 原 inc 第22行以 `packet->type==SC32_IPC_FRAME_EOF` 判断 EOF，但真实 Broker EOF 是 type0、flags 含 EOF。应改为 `packet->flags&SC32_IPC_FRAME_EOF`。否则正常关闭落入 `ui_drop_receive` 的格式拒绝，成为 INVALID，绕过 peer pump 已有 BROKEN_PIPE 的免 notification_error 分支。已排队的 FINAL 是 DATA，仍会先领取；这一点不是正常 FINAL 可读路径的阻断。
2. receiver 的 `ui_d2_peer_step` 已核目标节点存活/代数，但 `ui_d2_peer_work` 在 READY 后只看 channel。若目标节点销毁而同 root 另一个 Drop 节点仍存活，owner_live 保持真、channel 为空，step 会被一直跳过，receipt 保持 READY。把既有退役条件提成私有 `ui_d2_peer_retired`，供 step 和 work 共用；退役本身触发清理，不加轮询/计时器。补丁不改 Root 已另合的 source gate。

正常单文件 MOVE 的 HOLD→READY→绑定 job→FINAL→SOURCE_CHANGED→目录复核路径已逐段静态追踪，没有发现确定的必然阻断：短票在 HOLD 后消费；READY/FINAL 仍核原 round/nonce/peer；Shell 从 receipt 原 FILES 创建受管 job，真实 worker 身份与 job PID/gen 绑定；ledger 的 DONE+DURABLE 且 RENAME4/SOURCE_REMOVED5 才计来源改变；FINAL 已入队后 Release 关闭 channel 不吞 DATA；源端只失效，真实分页查询决定图标存活。此结论不等于构建、ABI 运行或新 Drop 客体 PASS。

原 inc 前后像、精确 anchors 和摘要见 PINS。三处文本替换可逆回完全相同原件；没有改变公开 ABI、票据布局、job、manifest、原 source gate 或 Root 正处理的 misleading-indentation 行。Root 应按 anchors 组合到当前候选，不能用本目录完整 after 覆盖已经合入的 source gate/排版修复。

补丁 SHA256 `941ac88f4e07cbf4fb93c051895b753b1fddd9ec3edaf61993d2eba7e315f534`；本组独立后像 SHA256 `b6a782d2232543ff4da694b86a0441a5341facecf6faa0f11fd41c7bbb86d499`。候选组合后的实际摘要需由 Root 重新记录。本目录交付即冻结，不扩其它理论审计或矩阵。
