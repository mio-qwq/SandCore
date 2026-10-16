# GUI02 三标签后续刷新接线

本目录已写齐可执行接线，尚未刷新三项。当前 GUI02 / SID4 / QEMU2364 由 Root 持有；本轮没有构建、启动或连接 VM、发送输入，也没有改 main、旧 helpers、历史报告、旧产物。Root 正常结束 GUI02、合入三处补丁并取得 HOST08 完整成功报告后，再显式运行。

唯一刷新范围为实际闭包变化的 `SCSHELL`、`NOTES2`、`STUDIO2`。仍由原 `refresh.py.preparation/execute` 完成源码、formal NATIVE.JSON、SCCC/SCLD、SCO、SCDB、格式、八库严格 SDK reader、27 对象源 alias、原盘守恒和一次 owner 终账。wrapper 不改这些检查；必须精确出现三个实际 changed_labels，少一项或多一项都拒绝。未变 SCUI 和其它产物保留真实 REUSED 来源，不重新生成。新产物只取回，不安装进运行中的 `/SYS`。

固定底座：

- previous：`build/m10a2-p6-target-refresh-03/p6-generation.json`，完整 SHA256 `fbc1c4519c9f432a67772eb9b7989b166edb7ce29a20a65cc88c5bae63774f2f`，8 库 / 21 artifacts / 27 SCO 的守恒 PASS。
- CORE：`build/m10a2-p6-final-02/core`；`CORE.SKM` SHA256 `aed45e2f613e556cc4687582c5b79a39a55393c394ed0be9b57c58035dbd4102`，`core.elf` 为 `7454542ea252b0e7e546de5a2f80546097b632f6d02849778f3a3a61e215aa8f`。使用既有构建，不运行 make。
- SDK：`build/m10a2-p6-final-02/sdk/MANIFEST.json`，SHA256 `82a839cf68c32c7cae18427a7a949e74e80005359f312e76040a29b9072cf439`。其八库字节逐项等于 previous，SCSOUND 仍只用已固定的唯一 HOST codec 例外；IMAGE 沿此 SDK 对应 HOST07 的原件，绝不借 HOST08 新应用替换引导底座。
- 当前实编 layout-proof：`build/m10a2-p6-gui-coherent-layout-01/layout-proof.json`，SHA256 `95d1108d29e095223dca4f912e4ed3af57821d3ccbc0c7fdac3b699280ad79bd`。核实际 TU/object/bin/JSON 摘要、bin 中编译常量、final02 CORE 实际 `.d` 闭包及真实 SCUI/Desktop 各自原生编译闭包。旧 `preflight.task_layout` 的 SCUIPRIVATE.H `374c2c…` 原样保留；实际底座对应 `62a62b…`，两者不互换摘要。

副文件 witness 只补既有证据的位置。refresh03 inner `cases` 没有 harness.file 记录；其外层 `build/m10a2-p6-refresh-vm-03/refresh-vm.json` 固定 SHA256 为 `f03755bcb535ee872661f7061bf57b12e8fcc4ea973a9475229ce1ff10bd0a33`，状态、bundle 路径、生成 QEMU PID、HOST07 摘要及守恒均核验。其真实 get case 31 / 32 分别记录 SCUI.SCO.log 12B / `dbd43d9e…a26f975`、SCUI.SCO.map 32915B / `18c877d3…170d07`。

`followup.Basis.bind_sidecars` 仅在本次调用中临时绑定副文件读取，结束即恢复原函数。每项必须在固定 previous 的产物清单内，有唯一真实 get 的 path/bytes/SHA/generation/PID；复制过的副文件还须沿 `inherited_sidecars.source_path` 与原 get 逐项一致，当前副本和原件都验长度与摘要。未知路径、缺 witness、冲突和长度不一致均拒绝。新报告中增加 `actual_get_witness` 指向实际历史 case；不向旧 `cases` 填造 get，不改历史报告。85 项只读核验已过：9 项直接 get，76 项经原继承 alias 回 get。

当前 HOST 绑定独立于历史 `preflight.build_report`。正式运行要求显式 `p6-unified-host-08.json` 完整 SHA，通过原 SDK `verified_build` 后逐项比较本次实际 current source snapshot 和 27 formal NATIVE.JSON 的来源。旧 HOST03/HOST07 字段仍是历史事实，不改成 HOST08。底座旧编译源码优先取 final02 SDK 归档；本次新正文由原 preparation 固定，分别纳入前后输入守恒。

只读交接检查已实际执行并退出 0：

```powershell
python -B sandcore/drafts/m10a2-p6-followup-refresh/driver.py --frozen-audit
```

两份 Python 源已由 `ast.parse` 检查，固定旧 runner/refresh/owner/model 的实际导入也已检查；均不调用 VM。HOST08 尚未提供，三补丁尚未在本接线执行，因此未执行正式 preflight、SCCC、SCLD、SDK staging 或客体流程。`READ-ONLY-CHECK.json` 记录本次真实边界。

Root 在结束当前 GUI、合并补丁并固定 HOST08 后运行。以下默认只读计划，不创建输出目录：

```powershell
Set-Location C:/Users/Administrator/Desktop/projectos/sandcore
$followupHost = 'build/m10a2-main-studio-01/p6-unified-host-08.json'
$followupHostSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $followupHost).Hash.ToLowerInvariant()
python -B drafts/m10a2-p6-followup-refresh/driver.py --host-report $followupHost --host-sha256 $followupHostSha --vm-out build/m10a2-p6-refresh-vm-04 --bundle-out build/m10a2-p6-target-refresh-04
```

确认上述实际计划仅三个标签后，Root 对同一命令显式追加 `--execute`。该入口复用旧 runner 的 Windows QEMU/prepare/CoherentHarness 接线；所有新文件只落两个全新 build 目录，不停止或接管当前 GUI02，不覆盖任何旧目录。失败保留实际 FAIL 与部分输出。native 报告仍须实际达到 `DECLARED_P6_NATIVE_GENERATION_PASS` 和严格 reader/守恒后，才能供后续 staging 使用；source-only 或 frozen-audit PASS 均不算新生成成功。

本轮不重建 CORE、不重新生成 SCUI、不扩大功能矩阵；后续安装、普通 GUI 的三缺陷复测和验收由 Root 统一执行。
