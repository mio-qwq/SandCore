# final-02：实际原生绑定 CORE 与严格 SDK

2026-10-16，执行、只读核验后冻结。所有新增输出只在 `build/m10a2-p6-final-02/` 和本目录；未改主树、旧草稿/旧产物、启动 VM 或复制磁盘。新软盘是两目标构建产生的 boot/loader 映像，不是复制数据盘。

实际来源固定：HOST07 `22b85409627ff02b46bc47093a4f2c9482e0229efb23b403d469892997a0e39c`；refresh03完整bundle `fbc1c4519c9f432a67772eb9b7989b166edb7ce29a20a65cc88c5bae63774f2f`。Root明确通知 PASS 后才执行。历史 native.preflight 仍指 HOST03，没有改写：当前352源快照逐项对齐HOST07 recorded及实际字节，外层refresh-vm03明确绑定HOST07、完整bundle和最终原盘/输入守恒。每个REUSED/FRESH原生成来源继续由正式SDK工具严格校验。

| 实际步骤 | exit | 墙钟 | 完整日志 |
|---|---:|---:|---|
| 固定 SCSOUND host codec 证明 | 0 | 0.110s | build/m10a2-p6-final-02/codec-proof-01.log |
| native-preview SDK staging | 0 | 11.437s | build/m10a2-p6-final-02/sdk-stage-01.log |
| 仅 CORE/loader 两个目标 | 0 | 22.531s | build/m10a2-p6-final-02/core-build-01.log |

精确执行程序/argv、退出值、日志SHA、源守恒与最终状态在 `build/m10a2-p6-final-02/finalization.json`。本次确实执行并观察上述进程，不把上次“只读Root已构建”凭证当作本次构建证据。`core-source-before.json` 固定1325个限定源/生成输入，构建后逐项相等；94份真实 `.d` 的294个非生成依赖均匹配HOST07和构建前快照。6个生成头、DHCP私有派生源单列。只宣称该明确集合与检查窗口，不声称观察整个仓库任意文件的所有历史变化。

## 交给 Root 下一轮 GUI 的路径

| 产物 | 字节 | SHA256 |
|---|---:|---|
| core/fs/SYS/CORE/CORE.SKM | 597712 | aed45e2f613e556cc4687582c5b79a39a55393c394ed0be9b57c58035dbd4102 |
| core/core.elf | 672892 | 7454542ea252b0e7e546de5a2f80546097b632f6d02849778f3a3a61e215aa8f |
| core/core.bin | 597584 | bbbc292ea707eb4b1001fdfd028cbec2b30b635ea5e4ca02b5ab01c175ccdd1d |
| core/sandcore.img | 1474560 | 47921cc2f9f0a23ea404cbd041a216931844023eedbcdbb4c824e9a55a8cfb3f |
| codec-proof.json | 3101 | 4299f1c9f1ef60abf2db10c86605621acb2ae34208832ff5247fcf529da9e020 |
| sdk/MANIFEST.json | 1389362 | 82a839cf68c32c7cae18427a7a949e74e80005359f312e76040a29b9072cf439 |

表中相对路径均从 `build/m10a2-p6-final-02/` 开始。SDK根目录是 `build/m10a2-p6-final-02/sdk/`：485个payload、17389865B，原生8库与唯一 SCSOUND host codec 分列；G1和最终SCLD均有真实guest原生成/执行来源，不使用SCLD0 seed冒充最终链接器。HASHES、SHA256SUMS和全部实际payload/manifest已逐一核相等，不放宽源、ABI或import约束。

CORE真实 ELF ALLOC正文/零填充与flat、SKM2 CRC/摘要/正文全部相等；从真实符号地址读取 LOGIN2、desktop 摘要及五库/UAX六资源发行表，与当前bundle及SDK副本逐项相等。三个issuer直接绑定refresh03目录，没有fallback或HOST库混入。实际公钥等于指定的32B `assets/trust/M10-OWNER-ED25519.PUB`；未读取私钥、签名或改变信任策略。

## 实际 CORE 命令

从 sandcore 经 WSL 运行，完整进程argv/转义保存在finalization；Python bytecode输出被关闭：

```text
PYTHONDONTWRITEBYTECODE=1 make -j4 BUILD=build/m10a2-p6-final-02/core M9_BUILD=1 M10_BUILD=1 SHELL_HOST_IMAGE=build/m10a2-p6-target-refresh-03/native-artifacts/SCSHELL.SCX LOGIN2_ISSUE_IMAGE=build/m10a2-p6-target-refresh-03/native-artifacts/LOGIN2.SCX LOGIN2_ISSUE_LIBRARIES=build/m10a2-p6-target-refresh-03/native-libraries M10_PUBLIC_KEY=assets/trust/M10-OWNER-ED25519.PUB build/m10a2-p6-final-02/core/sandcore.img build/m10a2-p6-final-02/core/fs/SYS/CORE/CORE.SKM
```

SDK由 Windows Python 调用当前正式 `make_m10a2_codec_proof.py` 与 `stage_m10a2_sdk.py`，显式HOST07、refresh03两份provenance、8项native-library以及新codec-proof；没有修改工具或用hash替换绕过检查。完整精确argv保存在finalization和PLAN。PLAN保留当时“等待PASS”的准备历史，实际最终状态以finalization及本批receipts为准。

本目录 core-build-receipt.json、sdk-staging-receipt.json、core-build-inputs.json 给出可核引用，不复制大报告或盘。finalize.py是受完整PASS/SHA门槛保护的一次性执行器，现有final-02输出会拒绝覆盖；inspect_final.py只读核验。旧ELF读取辅助只读复用本人已冻结源码，不调用其旧main。

本任务到此交付构建/staging；**没有启动这个新CORE、没有执行最终普通用户GUI、没有证明新SDK客体运行或完整M10a2完成**。Root后续按同一真实bundle/CORE/SDK做下一轮实际GUI，未变组件沿原有效证据复用，不重跑其它矩阵。
