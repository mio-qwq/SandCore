# P6 最小真实目标 refresh

只接现有Root VM/Guest/Harness，不创建QEMU、不修改主源或既有run03/GUI
控制器。未执行时不产生成功报告。变化标签从当前真实source_closures逐项
比较得出，不能硬编码FILES2。当前预期是SCSHELL私有prompt，若实际include
闭包使Desktop也变化，则Desktop也必须重生成；不按文件名猜影响范围。

完整继承8库、21个artifact和27个SCO。未变文件只做必要的小型产物副本：
正文/map/SCDB/object/log按原SHA复制到新bundle，避免严格SDK的当前out约束
拒绝引用外目录；不复制数据盘。每项明确generated_this_vm=false，
generation_origin.REUSED回到真正生成它的报告SHA、case、源闭包和工具。
旧stdout/命令/QEMU PID仍指原证据，不把REUSED case改写为新VM PASS。

新顶层source snapshot仅代表当前准备；provenance使用现有
CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN和
artifact_generation合同。SDK actual_native/compiler/native_linker已有此支持。
Candidate保持同一统一PASS schema、集合和源码alias规则；预计无需改它。
若未变object仍引用与当前不同的源原文，则拒绝复用并要求刷新该标签，不能
把旧SCDB FNV和新文件拼在一起。不可编译的历史草稿仍是HOST/archive来源。

工具仅用run03真实SCCC1和最终native SCLD（不是SCLD0种子），先从当前VM
SYS/BIN实际读回核SHA，并确认其源闭包没有变化。只上传变化的实际SRC和
所选正式NATIVE.JSON，Source路径继续/SYS/SRC；只编变化标签的SCO并由同一
最终SCLD链接。逐项保命令/完整stdout/返回码/工具SHA/文件代数，复用既有
SCO/SCDB/SCL/SCD校验；进程PID仍NOT_OBSERVED，不再造观察屏障。

新工作只有一次前后quiet/owner账和原盘保持检查，不重跑27项或功能矩阵。
运行产物仅取回，不替换当前GUI VM映射中的旧库；新的SDK/Candidate、发行
CORE是否重绑由Root按实际变化决定。五库不含SCSHELL，Desktop不变时无需
因SCSHELL prompt刷新而重绑CORE。
