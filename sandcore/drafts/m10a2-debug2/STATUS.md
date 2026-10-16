# Debug2 交接状态

2026-10-16。实现草稿已收齐，当前为 **HOST_BUILD_PASS / GUEST_UNVERIFIED**。没有运行QEMU，没有Git操作，没有改写正式主树；主代理继续整合、定向客体验证与总目标。

已实写：公开typed调试对象/错误/代数，真实owned paused launch，真实TF/INT3控制，新动态断点链、未映射失效与增量回收，内核可信SCX/ROOT登记，默认SCL来源/映射/重定位逐步核验，真实模块/段/导出快照与源SHA256，SCDB1MIO v1消费者，SCUI七面板原生应用、地址与分支/栈导航、菜单/工具栏/上下文菜单、Splitter、Dock、PropertySheet、文件对话框、布局保存/恢复、隐藏停帧、模块epoch缓存、退出/重启作业回收。

真实宿主产物（均只在 `build/m10a2-debug2-agent-check/`）：

| 产物 | 实际结果 |
|---|---|
| DEBUG2.SCX | 62,765B，4个真实库依赖，102个实际导入，545个动态重定位 |
| SC32API.SCL | ABI1.4/v6，137,510B，164 exports，6 imports，2648 relocations |
| SCLDR.SCL | ABI1.2/v3，20,361B，12 exports，0 imports，169 relocations |
| decoder-test | 18组明确指令/目标、截断和不可读检查，152断言，ASan+UBSan通过 |

完整宿主报告是 `build/m10a2-debug2-agent-check/report.json` 与逐项log。新应用、新kernel gate、新SC32 aggregate、新ROOT aggregate、CORE/BOOT adapter均通过实际GCC编译；新源使用-Werror。旧process/task adapter沿其原构建规则保留既有warning，实际编译成功。后续仅内核计数/分页/核验缓存和未提交模块拒绝的静态修正做了-Werror定向重编，绑定信息见 `completion.json`，不把未改应用/库结果归零。

`integration.patch`、`integration.json`、`INTEGRATION.md` 是正式接线入口。SC32API1.4/v6需与Studio的SpawnCapture增量合并；ROOT1.2/v3的登记是无SC32依赖的私有桥。SCAPI.H、debugger.c、SCDBG.H、SCDBG.inc、m9/scdbg.c的原字节SHA保存在integration.json。

尚未证实：真实客体新gate的owner/epoch/TF/INT3/映射来源矩阵、新GUI像素与键鼠、Classic/Aurora及隐藏行为、实际SCCC生成本批程序/库、双盘回收与性能。它们是主代理整合后的收尾，不是本目录的PASS。解码器明确是常用i386子集，SSE/FPU等未知编码按真实首字节db，不借此假称全x86反汇编。自定义非默认ScRuntimePlatform没有来源凭证时不登记；不会伪装成运行期模块。

本轮自审已修正：ROOT运行后BSS不可重新当零；私有模块候选不可被猜ID读取；MODULEPROTECT/卸载改变epoch；源文件变化撤候选；旧/新断点地址冲突及旧pending状态；当前IP新断点先执行原指令；待重插删除/卸载及共享页修补拒绝；运行时撤旧现场；模块cache失效；用户路径65B身份；动态分页线性推进；源符号同名TU身份；对话框真实拥有期；重启/关闭先结束和领取真实作业。

修订：2026-10-16，首次交付独立Debug2完整实现草稿、真实宿主产物和明确的待客体边界。
