# SCUI单库原生刷新与平台检查点

这里只保存真实 Phase 4 开发恢复材料，**GUI尚未通过，M10a2未完成**。完整首次平台/G1/结构体和官方文本证据在相邻 `m10a2-ui-platform`；P3三库原生字节和执行证明在 `m10a2-stdlib`，这里不重复保存磁盘或它们的库。

`ui-build-12.json` 为同批源码成功构建，39.6878秒、构建期间源码未变；相比build11只有 `ui_data.c` 与HOST SCUI.SCL改变。端点采用原严格同SID/主体默认ACL，修复未填allowed_session导致INVALID及错误清理覆盖cause的问题，未扩大权限。

`refresh-report.json` 保留原始完整报告字节。单独Windows双盘guest运行旧已执行G1，只新编译SCUI：656796B，SHA256 `5de54249fea6cbdf65d4a083e2a254045585706193833e611b6cfe224e635950`，158导出/137导入及真实资源账通过。原平台/3013文本/ABI等按不可变源码闭包明确REUSED，未在这次VM重跑；报告顶层沿用第一轮状态，不能据其名称把复用项说成刷新本轮执行。逐产物来源在两份原始provenance，日志与map也是实际客体输出。

`native-five-report-GUI-FAIL.json` 是GUI02原始FAIL报告，不能改成PASS。该VM确实安装并get核对五份原生库，普通会话两组平台/官方3013文本与owner账PASS，但宿主READY正则失败，尚无完整GUI操作。后续GUI03/04纯解析失败及GUI05真实旧F12路由冲突留在各build原报告，具体进展见M10A2-IMPLEMENTATION/HANDOFF。

`CORE-build12.SKM`、`core-build12.elf/.sym` 保存路由修复前实际内核字节，供旧运行来源核对。新内核修复后不能把旧core摘要改成新值；G1/库/App未变化部分逐项复用，只有实际受影响行为新验。旧SCAPI.H原字节未变。

`archive.json`列原路径、复制长度及SHA，所有复制已重新核对。无原盘、完整数据盘、VFS资源树或管理传输正文副本。
