# 单目标真实refresh接线

本目录仅实现计划、精确继承和接现有Root harness的执行函数；没有构建或
启动VM，没有改main、run03、GUI控制器或其它冻结草稿。最终prompt补丁及
当前正式NATIVE.JSON由Root应用/固定后再运行。本次实际变化应由闭包决定，
当前预期SCSHELL，不预设FILES2或无条件重跑27项。

## 准备

Windows Python中：

```python
import refresh
plan, payload, pins, previous = refresh.preparation(
    previous_report, previous_report_full_sha256,
    allowed=['SCSHELL', 'DESKTOP'])
```

run03原报告SHA为
`f5538ba547f2ae38e00f35d35b57e2c0a55ef8282955aefbf221042122c57ddc`。
preparation读取当前generation model同一张CASES/实际literal闭包，逐label
比较run03.preflight.source_closures，保原路径/原字节/正式NATIVE.JSON。
allowed只是Root批准范围，actual changed_labels仍须从摘要得出。空变化
不执行refresh；公共头或其它闭包变化超出allowed则明确停在计划阶段，
不能忽略变化。compiler closure或SCLD闭包变化时拒绝借旧工具假装当前。

CLI只会保存计划JSON，不会执行compile或VM：

```powershell
python drafts/m10a2-p6-target-refresh/refresh.py --previous build/m10a2-p6-guest-generation-run-03/p6-generation.json --previous-sha256 f5538ba547f2ae38e00f35d35b57e2c0a55ef8282955aefbf221042122c57ddc --allow-label SCSHELL --allow-label DESKTOP --out build/m10a2-p6-target-refresh-plan.json
```

Root应先提供当前严格HOST/archive报告，包含实际新源/NATIVE.JSON；不要
为消除差异伪造旧源SHA或改写旧generation。当前27份formal manifests仍从
主源同stem.NATIVE.JSON读取，不按import猜一套HOST清单。

## 执行

Root已有持续VM/Guest/harness可供显式调用，无需本helper再开QEMU：

```python
report = refresh.execute(harness, plan, payload, pins, previous, fresh_bundle,
    host_report=current_host_report,
    compiler='/SYS/BIN/SCCC.SCX', linker='/SYS/BIN/SCLD.SCX',
    work='/HOME/P6REFRESH')
```

fresh_bundle须为build/m10a2-p6-target-refresh*全新目录。当前VM先从SYS/BIN
真实get两工具并核SHA：SCCC1为ac7b2195…51a9e，最终native SCLD为
623954cf…50a6；实际校验用完整run03字段，不使用省略值。这里使用最终
SCLD.SCX，不重新生成compiler、SCLD0或SCLD。

只上传变化的实际SYS/SRC正文和变化标签的正式LINK/*.NATIVE.JSON；只对
changed_labels做一次SCCC --object和一次SCLD --scl/--dynamic。工作目录
仅收新结果，不覆盖当前SYS/LIB映射或APPS入口。命令/完整stdout/返回值、
工具SHA与SCO/map/SCDB/格式/源行校验直接复用现有harness和generation
contract，不再做不可靠PID观察。原盘SHA、输入保持和一次整体owner账仍核。

GUI会话中的输入和应用业务应在这两条CLI运行时暂不操作，以免把其它业务
资源变化归到refresh。helper不改变GUI控制器，不替Root关闭应用或切SID。
Root也可把同一接口接自己的单次已有编译VM；磁盘由外层持有，helper只复制
少量必要code/object/map/SCDB/log，不复制256MiB盘或重跑全部目标。

## 统一来源

未变8库/21artifacts/27objects逐项校SHA并继承到新bundle的对应目录。
其generated_this_vm=false，generation_origin.mode=REUSED，report、完整
report_sha256、compile_case、source_input_sha256和include_manifest_closure
仍指真正原生成轮。旧命令/stdout/QEMU PID不改成本次；新cases中标为
REUSED_PASS，严格SDK沿origin回原PASS case。副文件也必须有原SHA或真实
get记录；不靠同名文件继承。

新生成项mode=FRESH、generated_this_vm=true，指向本轮真实producer；
工具可以在本轮执行但从旧轮生成，两种事实分开。SCCC1的曾执行资格不清
成false来破坏SDK.compiler，也不伪称其在本轮重新生成。新的顶层input/
source snapshot只代表当前准备，provenance使用现有source_snapshot_scope
和artifact_generation合同。两个provenance.json都包含统一8库/21artifact。

执行末尾直接调用当前严格SDK actual_native八库、compiler和native_linker，
并对所有27对象的SCDB源alias核当前原文。这不改reader或放宽其条件。
Candidate使用相同最终status/集合/fields，预计无需补丁；若old未变SCDB
仍指向已变化原文，preparation就拒绝继承，不能把新文件塞进旧SourceMap。

最终正常报告仍为DECLARED_P6_NATIVE_GENERATION_PASS，scope另标
P6_TARGET_NATIVE_REFRESH，附source_reuse和严格reader audit。执行异常会
保FAIL/未齐状态，不以计划、原产物存在或新目录名宣布新生成成功。新的
SDK/Candidate由Root用新统一报告重做staging；五库发行不含SCSHELL，若
Desktop等固定发行入口不变，无需因本prompt库刷新重新绑定CORE。

修订：2026-10-16，实现最小闭包refresh与精确REUSED/FRESH来源；尚未构建、执行或取得新产物。
