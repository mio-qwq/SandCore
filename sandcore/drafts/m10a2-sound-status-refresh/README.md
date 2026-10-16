# 候选回收后的 Sound2 状态刷新

只有 `sound_collect` 一处窄 hunk：候选 job 已 Query/Wait 完成、资源回收并清空 `sound_app.candidate` 后，若旧 current 仍在，调用已有 `sound_changed`。它此时会显示旧曲实际状态；旧曲已 EOF 而不再发 telemetry 时，不会永久残留“正在准备”。不改变音频、worker、IPC/ACK、job 或交接状态。

这是明确的有条件显示缺口修复，**不是 GUI02 重播根因已定位或修复的结论**。已有截图、单 worker ps 和实际正常关闭/owner0事实不足以证明当时 candidate 指针、phase、inflight 与 job 的值。保留真实重播未通过状态；没有新运行验证。

源码前像/后像、`sound-status.patch`、PINS 均在本目录。前像 SHA256 `b1cf434a244f5985e999ccefab326eff250c797220a5c9a8dcfee4817808ec7b`；后像 SHA256 `b0a34e848dd9d24a04660d494700d4a792eb5141e2576369dc13847af0d1b8c4`；补丁 SHA256 `f3ac053446afa90f03de5eb70fe6c7950b324f2315e35125dc516be9b6a20c05`。

静态逆替换精确回到原像。未改 main，未构建、未调用 VM；没有新增运行夹具。Root 后续合入时，真实受影响生成标签是 SOUND2，SOUNDWORK 没有本次变化；最终集合仍由 preparation 实际闭包决定。
