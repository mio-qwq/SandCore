# GUI02修复后的最小SCO/NATIVE接线

直接复用 `tools/check_m10a2_p6_objects.py`。它用实际SCCC后端生成SCO，按SCO未定义符号建立imports、按现有9库exports找到provider，再用原SCLD执行真实链接。没有新增编译器、链接器或清单格式。

本稿没有运行构建、VM、提升正式清单或改主树；HOST08仍待Root完成集成后提供最终完整报告SHA。`plan.py`只读来源并打印现有工具命令，不执行命令。预计变化标签是SCUI、SCSHELL、FILES2、DESKTOP、NOTES2、STUDIO2、SETTINGS2、INSTALLER2、CALC2、PALETTE2、MONITOR2、SOUND2；最终取与真实refresh03逐label闭包的差异。其余标签明确复用原真实来源，不自动重跑27项。

## 只读计划

在仓库根的Windows PowerShell执行，替换HOST08完整SHA：

```powershell
wsl.exe --cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore python3 drafts/m10a2-gui02-native-plan/plan.py --host-report build/m10a2-main-studio-01/p6-unified-host-08.json --host-report-sha256 HOST08_FULL_SHA256 --out build/m10a2-p6-sco-09
```

脚本核HOST成功及源码守恒，复用冻结generation model的literal闭包；每项当前源、模板、formal NATIVE及provider目录必须匹配HOST输入。SCCC闭包必须与refresh03完全相同，SCLD及本批之外标签变化会拒绝。它核固定工具和08报告完整SHA，输出实际changed_labels、准确CASES入口、同源闭包、现有工具命令及提升目标。输出目录必须未存在，路径遵守旧64B缓冲，打印前再次核输入。

HOST报告若没有覆盖当前formal NATIVE或实际源，必须由Root生成新的成功HOST报告及完整来源归档。不能把某个旧行的SHA替换为当前值后继续；原HOST08报告永久保留。plan不会创建HOST报告，也不会重生或提升清单。

## 实际宿主IO后端生成

Root在源码集成与HOST08成功后执行。若计划确为上述12项，确切命令如下；实际集合不同则使用plan打印的command：

```powershell
wsl.exe --cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore python3 tools/check_m10a2_p6_objects.py --out build/m10a2-p6-sco-09 --compiler build/m10a2-p6-adapter-02/sccc-host --case SCUI --case SCSHELL --case FILES2 --case DESKTOP --case NOTES2 --case STUDIO2 --case SETTINGS2 --case INSTALLER2 --case CALC2 --case PALETTE2 --case MONITOR2 --case SOUND2
```

必须显式指定compiler。helper的默认路径仍是旧 `build/m10a2-terminal2-agent-native/sccc-host`，其SHA不是08实际工具。这里沿用08真实执行的适配器02：

| 输入 | 完整SHA256 |
| --- | --- |
| 适配器02 `sccc-host` | `a6dfe92930876400b349ddc33924ac2c5bae76a5dcb8995c1bbbf93b08760696` |
| 固定 `scld-host` | `339561669ebc16143ac8b1e7099830503e818ef150b6dfaa5ceb8a5cdf0ed034` |
| `p6-sco-08/report.json` | `c35cb261372cb20067769d60833d36285af345622aee1e7da8e9b4fa807a2513` |
| 原native refresh03报告 | `fbc1c4519c9f432a67772eb9b7989b166edb7ce29a20a65cc88c5bae63774f2f` |

适配器02保留当前SCCC的预处理、语义、机器码发射、SCO及map实现，只用宿主文件IO启动它；本次不重建该工具。其原build.json精确记录直接s3c正文和生成C源码，不冒称该旧报告含每个编译时include的独立记录；这里固定08已实际使用的同一二进制，并核当前完整compiler闭包没有改变。旧G1、新native SCCC1和最终native SCLD都不重生。SCSOUND不进入CASES，原HOST codec例外保留。

每项实际命令由已有helper生成：

```text
sccc-host user/<CASES source> build/m10a2-p6-sco-09/<label>.SCO --object
scld-host build/m10a2-p6-sco-09/<manifest name> --scl|--dynamic build/m10a2-p6-sco-09/<label>.NATIVE.JSON build/m10a2-p6-sco-09/<label>.SCO
```

已有helper保留compile/link完整日志、返回码、工具/SCO/NATIVE/image SHA和actual_compiler_input_sha256。SCO source表逐项核原文长度、FNV和当前SHA；找不到真实源或内容已变会失败。SCLD仍严格拒绝unresolved external、kind/size冲突及unused manifest import。没有忽略unused、按旧模板补漏、删源表或更改source比较。

## 正式清单与后续真实refresh

仅在新report总体PASS且这批每项compile_exit/link_exit为0后，由Root按plan中的提升表核原正式清单before SHA，再提升真实输出；结果SHA必须与新report每项manifest_sha256一致。不要按label猜目标名：DESKTOP输出 `DESKTOP.NATIVE.JSON` 的目标是 `user/scshell/desktop/SCSHELL.NATIVE.JSON`，Sound与Studio原HOST模板后缀为大写 `.JSON`。其余目标也从现有CASES模板 `with_suffix('.NATIVE.JSON')` 唯一计算。

提升后，最终统一HOST归档必须记录这些正式清单的新字节；提升前HOST08若记录的是旧NATIVE，不能直接作为提升后完整输入证明。Root保留08原报告，另做HOST09增量make及完整来源归档，通常仅清单来源变化不会重编未受影响目标。不得修改、拼接或重写08的inputs。严格guest-generation preflight仍要求新report原输出SHA、主源promote SHA、当前闭包及最终HOST归档共同一致。原03/04/05/06/07/08对象来源只按当前真实compiler-input SHA筛选复用，不能因旧report总体PASS就选已过时行。

这轮宿主SCO证据仍是 `guest_compile=false/guest_runtime=false`。后续Windows VM沿既有target-refresh使用原native SCCC1与最终native SCLD，只重生成实际变化标签，未变项保留REUSED原报告/producer/源闭包，SCDB的旧源原文不可混入新内容。本稿不替代该真实生成或产品GUI回归，也不改变CORE issuer重绑规则。

修订：2026-10-16，收敛GUI02修复后12标签的现有宿主IO实际对象/正式NATIVE命令；只读准备，未执行。
