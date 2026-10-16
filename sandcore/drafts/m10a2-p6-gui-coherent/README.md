# 下一次 GUI 的一致 owner 接线

`driver.py` 沿实际 acceptance driver 的候选验证、单副本安装、Windows QEMU、普通 `su mio` 会话、JSONL 输入、AC97 与短录音以及 finally 守恒逻辑。只把基点与 `finish` 接到冻结的 `owner_snapshot.py`。`coherent_runtime.py` 继承原 runtime；产品输入、截图身份、去重和 `serve` 都复用原实现。

GUI01 的原报告 `FAIL_OR_INTERRUPTED / p6-gui-single-final-owner-account FAIL / +205` 只读固定并继续列为未归属，不作豁免，也不改写。新结果不会把旧失败变成 PASS。

先在开 VM 前生成**真正当前**的 i386 布局，随后做新 driver 的纯预检。把 `<...>` 换成 Root 刚验证的实际新批次路径；没有默认猜测当前 refresh/SDK/CORE 路径。

```powershell
wsl.exe --cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore python3 drafts/m10a2-p6-gui-coherent/layout_current.py --out build/m10a2-p6-gui-coherent-layout-01
python drafts/m10a2-p6-gui-coherent/driver.py --native-report <new-PASS-generation.json> --host-report <current-HOST.json> --sdk-directory <new-SDK> --core-build <native-bound-CORE> --source-image <original-data.img> --layout-report build/m10a2-p6-gui-coherent-layout-01/layout-proof.json --out build/m10a2-p6-gui-run-02
```

第一行供 Root 显式执行一次现成 `m10a2_shell_inspect.build_layout` 的真实 `gcc -m32`/objcopy。交接期间本代理没有执行它。第二行没有 `--execute`，只读取并严格验证来源，不创建目录或启动 VM；预检成功后给完全同一条命令添加 `--execute`。JSONL stdin 的接口和实际截图/点选约束与原 runtime 相同，末尾使用唯一 id 的 `{"id":"finish-current-gui","op":"finish"}`。

Root 已执行第一行并成功取得 `build/m10a2-p6-gui-coherent-layout-01/layout-proof.json`，SHA256 `95d1108d29e095223dca4f912e4ed3af57821d3ccbc0c7fdac3b699280ad79bd`，实际尺寸 `1248 / 672 / 184`。本目录已只读核对这份 proof、全部 o/bin/TU/log 摘要、JSON 与真实编译常量及当前头；无需再次生成同一布局。

当前 `SCUIPRIVATE.H` 与 run03 SDK 的精确差异只有新增 `ui_argb_over_partial` 静态函数声明，struct 文本没有变化。这是静态阅读事实，不能把旧 layout 的头 SHA 直接替换成新 SHA。`layout_current.py` 把当前真实 sizeof/offsetof 对象、二进制常量、生成 TU、日志、头与提取器摘要一起记录；driver 必须先取得并固定这份新凭证。

运行来源与待生成来源必须分开。旧 refresh VM 运行旧八库时，观察器应使用真正旧 SDK 中匹配 hash 的头；其当前待生成 source pins 另记。**本 driver 是新 GUI 入口**，逐项核 layout 的私有 SCUI 头来自将实际运行的 SCUI native 闭包，Desktop 头来自实际复用/新生成的 SCSHELL.SCX 闭包，kernel 头和固定私有布局来自实际 CORE 的 HOST 编译输入。若新 native 行还指向旧头而 layout 使用新头，预检直接拒绝，不开第三次盲目 VM。

完整原 `report.input_sha256` 继续保存所有当前输入。观察器使用只读 view，其 `input_sha256` 只含已经按上述角色核实的真实源路径和摘要。这个映射和各组件/CORE 来源在 `coherent_owner.source_binding` 单独落盘；没有把当前源重命名成旧路径，也没有放宽冻结 helper 的原校验。归档目录中不参与当前运行的同名旧头不会与运行结构混淆。

基点在真实 `sessionctl list` 确认唯一原普通 active SID 后、创建测试 SID 前取得。冻结 helper 核实际 native MAIN 的完整 SHA、PID/代数、Desk 图标/刷新/保存终态、host 已进入等待、root 绘制及现有 GC/loader/FS 源状态，然后在一次短暂停中同时收 PF、resources 和全部活 PID/代数的页表归属。完整快照、观察、暂停与恢复确认都留在新报告。

`finish` 先复用真实 list 核测试 SID 已注销、原 SID 已实际 active；再用同一 helper 取一致终账，并强制原 host PID/代数与基点一致。两次交给**原 ledger**的 PF/resources 各自来自同一次暂停，不再使用旧 `quiet()` 页数配之后的 resources。`ledger` 的归属、字体、池和失败规则完全沿用，不增加 cache 或 205 页特例。

本目录代码、补丁与静态自审已经写齐。本代理未运行布局编译、Candidate 预检或 VM；已核对 Root 单独生成的实际新 layout。当前 refresh VM 及全部旧 helper/草稿/主树保持原样。严格 Candidate 预检及本接线的客体 GUI/owner 结果仍待 Root 执行。
