# final03 准备：新原生 CORE、严格 SDK 与独立新布局

本目录只交脚本和计划，尚未生成新 layout、codec proof、SDK 或 CORE。默认 `finalize.py`、`layout.py` 的计划调用均已退出0，四份新 Python 源 AST 通过；没有调用 WSL/GCC/make/VM，没有复制盘、修改主源或旧脚本/历史产物。必须等 Root 明确提供 native04 完整守恒 PASS 后才执行。

固定 HOST09：`build/m10a2-main-studio-01/p6-unified-host-09.json`，完整 SHA256 `879ceb99215972f60d1dfc76f0102d1a2255c8616685dd980eb4e8d5aba77ae4`，Root 实际 exit0 / 41 outputs / source变化0。计划引用 `build/m10a2-p6-target-refresh-04/p6-generation.json` 和 outer `build/m10a2-p6-refresh-vm-04/refresh-vm.json`；两者 SHA 尚待实际 PASS，执行时必须显式给出，不能自动用当前文件摘要代替 Root 固定值。

`bindings.py` 共同执行：原严格 SDK verified_build 的 HOST09 输入/输出检查；native04 的27 SCO、8普通库、21 artifacts 和严格 reader/守恒；当前完整 source snapshot、compiler/include closures、真实新 public_header_sources 与实际 source_plan/HOST09逐项相等；outer 必须同 HOST09、同 bundle、同生成 QEMU PID 和最终守恒。outer 成功状态按新 runner 合同为 `EXPLICIT_LABEL_REFRESH_GENERATED`。历史 preflight 的 HOST03、旧 task_layout 继续保留，不能把它们改名冒称当前来源。

## 执行顺序

以下命令都从 sandcore 目录用 Windows Python 运行。变量中的两个完整 SHA 必须来自 Root 对实际 PASS 报告的固定通知。

```powershell
$p6NativeSha = '<native04实际PASS报告完整SHA256>'
$p6RefreshSha = '<outer refresh04最终报告完整SHA256>'
python -B drafts/m10a2-p6-final-03-receipt/layout.py --execute --native-report-sha256 $p6NativeSha --refresh-report-sha256 $p6RefreshSha
```

`layout.py` 先核全部绑定，再通过 WSL 调用固定的旧 `layout_current.py`，输出全新 `build/m10a2-p6-gui-coherent-layout-02/`；原 helper 与 layout95 均不改。该调用实际编译当前 i386 `sizeof/offsetof`，不复制旧 values。完成后核 TU/object/bin/JSON、ELF32、-m32、五个结构头与本轮真实 SCUI/Desktop 编译 closure及HOST09内核源匹配，并为这个新 proof 写入独立 native_source_binding 和实际退出/日志。即使偏移值相同，也不能用旧95 proof冒充新提取。若失败保留目录；要重试须用 `--out` 指定另一个不存在的同前缀新目录。

```powershell
$p6Layout = 'build/m10a2-p6-gui-coherent-layout-02/layout-proof.json'
$p6LayoutSha = (Get-FileHash -Algorithm SHA256 -LiteralPath $p6Layout).Hash.ToLowerInvariant()
python -B drafts/m10a2-p6-final-03-receipt/finalize.py --execute --native-report-sha256 $p6NativeSha --refresh-report-sha256 $p6RefreshSha --layout-report $p6Layout --layout-report-sha256 $p6LayoutSha
python -B drafts/m10a2-p6-final-03-receipt/inspect_final.py
```

finalize 仅在全新 `build/m10a2-p6-final-03/` 执行三步：固定 HOST09 的唯一 SCSOUND codec proof；严格 native-preview SDK staging；WSL make 的 `core/sandcore.img` 和 `core/fs/SYS/CORE/CORE.SKM` 两目标。SDK 八普通库必须都是 `ACTUAL_SCCC_NATIVE`，SCSOUND 是唯一 `USER_APPROVED_HOST_CODEC_EXCEPTION_NOT_GUEST_NATIVE`；G1/SCLD 必须是真实原生来源。发行绑定直接取 native04 的 Desktop/LOGIN2/五库，staged SDK 同字节复核，不退回 final02 或 HOST 普通库。公钥仅为 `assets/trust/M10-OWNER-ED25519.PUB`，不涉及私钥。

final02 与旧 layout95 **只属于 refresh VM 的旧运行底座**；本组不把旧引导角色等同新发行。新布局校验不读取历史 task_layout 的旧 SCUI 私有头 SHA；它按 native04 中真实 SCUI/Desktop producer及HOST09内核源分角色核验。

inspect 只读复用冻结的 ELF 解析函数，不调用旧 main。它核实际 ELF/flat/CORE、loader/软盘、实际发行 Desktop/LOGIN2/五库/UAX 摘要、32B公开key、真实 `.d` 编译闭包与HOST09/构建前快照；生成头及DHCP派生源单列；SDK payload/manifest摘要逐项相等。新receipt绑定本次新layout，并引用完整命令、退出、日志。执行由 finalizer 记录，不将准备代理冒称为已观察构建的执行者；guest_boot/runtime/full_M10a2 均保持false。

失败不覆盖旧目录或改写历史。最终仅 `CORE_AND_STRICT_SDK_VERIFIED_NOT_GUEST_BOOT` 表示构建/staging及只读核验完成，仍不等于新CORE已boot、普通GUI通过或M10a2完成。Root后续统一客体，已有未变证据复用，不增加大矩阵。

`finalize-from-final02.patch`、`inspect-from-final02.patch` 只供审阅差异，不应应用到冻结旧目录。实际输出必须由后续执行生成；本目录的 PLAN、LAYOUT-PLAN、PINS、DELIVERY 当前均明确是准备态。
