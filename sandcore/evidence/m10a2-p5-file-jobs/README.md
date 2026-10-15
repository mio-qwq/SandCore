# P5 精确元数据与真实文件事务

Windows QEMU 双盘、六个实际原生系统库、真实 G1 编译的 FILEJOBPROBE，普通 UID1000 / NORMAL 会话；范围仅本页列明，完整 P5 / M10a2 继续进行。

- runtime-04/shell-runtime.json：P5_TYPED_PATH_AND_REAL_FILE_JOBS_PASS，最终 owner / GC账13830→13830物理页，所有池对象/页、历史、字体和未归因增量为0；输入与原盘保持。
- 新 ScPathQuery 的存在/缺名/权限和失败不写输出通过；旧 PathStat/Metadata 与 SCAPI 字节不变。
- COPY 131079B 跨两次64KiB块，真实 ACK stage3/flags9；MOVE 实际Rename stage4/flags9。普通worker保持本人PID/gen/UID/SID，源在目的持久提交后才移除。
- 源与最终文件在客体逐字节校验并独立取回，SHA256同为 fa1923a8e1fbeb986a53fa4f5c40b84c02c01f44ebae10671821e28b915c521e。两个832B恢复日志的64B头和每个256B frame CRC、连续序号、实际提交记录、START/FINISH全部独立审计通过，见actual-binary-recovery-audit.json；该审计没有新启动VM。
- runtime-01..03保持真实FAIL，02的本人WORK.TRACE定位recovery2。修复候选同名Publish与两个NULL误判为同一租约；不是放宽已发布对象Rename/权限/条件提交。
- generation-12真实更新SC32/SCSHELL/worker/desktop；13只更新默认关闭的本人失败诊断worker。SCUI与ROOT/STD/UTIL均追溯原实际生成来源，不冒称本轮重编。CORE15绑定同一125405B原生桌面。

index.json保存本包每一字节摘要。CORE15为本切片实际运行内核；没有磁盘副本、私钥或新信任入口。跨进程FILE剪贴板/实际FILE拖放、布局冷启动、完整组件/SDK仍待后续工作。

修订：2026-10-16，保存真实COPY/MOVE、失败沿革、CRC和零增量回收证据。
