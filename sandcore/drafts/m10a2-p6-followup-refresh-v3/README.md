# HOST09 followup refresh v3

v3只解除冻结v2把输入文件名写死为HOST08的阻断：新 `driver.py` 把一个报告文件名常量改为 `p6-unified-host-09.json`，另6处HOST08说明/错误文案改为HOST09。除此以外driver原字节全部保留；`followup.py` 与冻结v2逐字节相同。

HOST08真实FAIL及SCO09两库export schema失败保留。Root已从实际SCO09十应用、SCO10两库的成功输出提升12份正式NATIVE；本稿不更改这些源、清单、报告或主树，不执行构建、preflight、frozen-audit或VM。Root随后报告HOST09实际exit0、41项输出、源码变化0；本稿仅只读复核该报告完整SHA及这些字段，没有执行v3 preflight。

完整SHA绑定、`sdk.verified_build`、当前全部source/formal NATIVE守卫、显式标签等于实际闭包变化、compiler/SCLD拒绝刷新、严格SCO/SCDB与SDK reader、八库/21artifact/27object集合、旧SDK/CORE/bootstrap布局及85项sidecar来源均保持。旧G1和实际native SCCC/SCLD不改，SCSOUND保留唯一HOST codec例外。

`followup.py` 的HERE按本文件实际路径取v3，因此driver会固定本目录两份 `.py` 与本目录 `PINS.json`。BASE仍直接指向冻结v1 `m10a2-p6-followup-refresh`；原4项BASE_PINS未改且本次逐项SHA复核一致，没有改成v2或重新包装旧witness。v2原文件及其检查报告不改，也不把v2已执行的只读检查冒称v3执行。

Root在sandcore目录，取得成功HOST09的完整SHA后先执行同一实际12标签只读命令；两个输出目录须仍不存在：

```powershell
python -B drafts/m10a2-p6-followup-refresh-v3/driver.py --host-report build/m10a2-main-studio-01/p6-unified-host-09.json --host-sha256 879ceb99215972f60d1dfc76f0102d1a2255c8616685dd980eb4e8d5aba77ae4 --label SCUI --label SCSHELL --label FILES2 --label DESKTOP --label NOTES2 --label STUDIO2 --label SETTINGS2 --label INSTALLER2 --label CALC2 --label PALETTE2 --label MONITOR2 --label SOUND2 --vm-out build/m10a2-p6-refresh-vm-04 --bundle-out build/m10a2-p6-target-refresh-04
```

不带 `--execute` 的原行为保持：只读准备，不创建输出目录或VM。只读preflight确认HOST09、正式清单及实际变化集合全部一致后，Root沿原授权在同一命令追加 `--execute`；不重写HOST08或放宽任何来源比较。若这两个目录已被Root使用，则由Root选同前缀的全新编号，不能覆盖历史失败目录。客体工作目录仍保留v2的 `/HOME/P6FOLLOWUP2`，本次不改另一项行为。

`driver.patch` 是固定v2到v3的5个局部hunk，仅供回读，不能应用到旧目录。`PINS.json` 记录v2 before、新driver result、字节一致的followup、BASE链和所有检查边界。仅执行两份源的AST、7个固定字符串替换的逆变换字节相等检查、v1四pin与v2五文件守恒；未执行新driver或任何客体操作。

修订：2026-10-16，独立复制冻结v2，仅接受HOST09及对应说明；准备完成，实际preflight/生成待Root。
