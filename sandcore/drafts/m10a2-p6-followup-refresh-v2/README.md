# 显式标签的 followup refresh v2

只在本新目录准备接线，旧 `m10a2-p6-followup-refresh` 保持冻结。没有构建、VM 调用/输入、磁盘复制、main/工具/旧报告修改；没有读取或猜测新 Drop API 内容。Root 收齐所有修复、HOST08、最终标签集合和报告完整 SHA 后才执行。

v2 复用已审旧 `Basis` 的固定报告、八库/CORE/SDK 引导、布局和 85 项副文件 witness；新增两项接线：

1. 调用者逐项重复 `--label`，没有默认标签。标签必须属于真实当前 `CASES`，空集合、重复、未知、compiler/SCLD 标签均拒绝。原 `preparation` 仍逐标签计算真实闭包变化，且 `changed_labels` 必须与调用者集合精确相等；不按当前预期自动放行。原 helper 对 compiler closure 和 SCLD 源变化的拒绝保留。
2. 新生成报告的 `preflight.public_header_sources` 由实际 `prepared.public_header_sources` 复制。每个 header source 先核本轮真实 `source_input_sha256` 与文件字节；`inherit` 后只为新报告设置这个映射，写回新目录。旧报告和旧 map 不改。这样下一轮复用本轮 fresh SCUI 的 `SYS/INC` 源时，会使用它实际编译时的公开头映射。原严格 SCO/SCDB、manifest、SDK source/ABI/native reader 检查保留。

引导及观察来源继续分层。previous 完整 SHA 为 `fbc1c4519c9f432a67772eb9b7989b166edb7ce29a20a65cc88c5bae63774f2f`；运行底座仍为 `build/m10a2-p6-final-02/core` 和对应 SDK 的旧八库，新生成产物不安装。实际观察 layout-proof 仍为 `95d1108d29e095223dca4f912e4ed3af57821d3ccbc0c7fdac3b699280ad79bd`。UI/Desktop 私有头从该旧 SDK 精确归档取得，kernel 观察源按 final02 实际 `.d` 固定摘要核原文件（旧 SDK 没有 kernel 源归档）；它们与本次新 SCUI 的 source/header map 分表。历史 `preflight.task_layout` 仍保原样，不能替换其旧 SHA。

固定旧 `refresh.py` 仍负责完整 8 库 / 21 artifacts / 27 SCO、原生 SCCC/SCLD 工具验证、formal NATIVE.JSON、SCDB 源原文、严格 SDK reader、原盘及输入守恒、一次 owner 终账。副文件仍从原实际 get 记录追溯，含固定 outer refresh-vm03 witness，不伪造缺失 case。v2 的 `followup.py` 对原 `inherit` 只加新报告 header-map 赋值；退出上下文即恢复内存中的原函数，不修改旧 helper 文件。

当前预期为 SCUI / SCSHELL / FILES2 / DESKTOP / NOTES2 / STUDIO2 / SETTINGS2，**不是脚本默认或授权推断**。最终以 Root 明确给出的标签集合及实际闭包为准；如果 SETTINGS2 未改、另有真实标签受影响，调用者必须给出对应精确集合，否则拒绝。

Root 后续从 sandcore 目录调用，下面仅表示当前预期的参数形状，HOST08 完整 SHA 必须换为 Root 固定值：

```powershell
python -B drafts/m10a2-p6-followup-refresh-v2/driver.py --host-report build/m10a2-main-studio-01/p6-unified-host-08.json --host-sha256 <Root固定的完整SHA256> --label SCUI --label SCSHELL --label FILES2 --label DESKTOP --label NOTES2 --label STUDIO2 --label SETTINGS2 --vm-out build/m10a2-p6-refresh-vm-04 --bundle-out build/m10a2-p6-target-refresh-04
```

不带 `--execute` 只读准备、不创建输出目录。Root 正常结束当前 GUI、确认真实计划后才在同一命令追加 `--execute`。新目录必须不存在；失败保留。outer 成功状态改为 `EXPLICIT_LABEL_REFRESH_GENERATED`，并记录 requested/changed 集合；inner 的唯一可用状态仍是实际 `DECLARED_P6_NATIVE_GENERATION_PASS`、严格 reader 和输入/原盘守恒全部通过。不能把 source-only 或 frozen-audit 当成本轮原生生成成功。

已执行的检查只有：两个新 Python 源的 AST、纯内存标签条件检查、旧 witness 的只读复核。实际 `python -B sandcore/drafts/m10a2-p6-followup-refresh-v2/driver.py --frozen-audit` 退出 0，85 项仍为 9 项直接 get、76 项继承 alias。HOST08 与新 API 尚未收齐，因此没有正式 preparation、构建、实际 inherit/header-map 生成、SCCC/SCLD 或客体执行。完整边界和摘要见 `READ-ONLY-CHECK.json`。

`driver.patch` 是从固定旧 driver 到本目录 driver 的精确 13 处文本变化，供审阅，不应应用到旧目录。两份新源与 PINS/README/检查记录即本次冻结交付。
