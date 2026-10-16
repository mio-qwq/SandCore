# P6 单次客体原生生成

本 driver 不建立新串口、QEMU、截图或资源验证框架。执行时复用
RefreshHarness、QemuSession、Guest、prepare、wait_first_desktop 及现有
quiet/ledger/screen。当前任务只写实现和只读宿主检查，不启动 QEMU。

生成链固定为旧真实 G1 → 当前完整 user/sccc.c → 新 SCCC → 当前 scld.c 的
单源 --dynamic seed → 每个 CASE 的 --object → 同一 native seed SCLD 链接。
SCCC 强制输出 basename 与动态清单一致，所以 seed 在 BOOT/SCLD.SCX，
最终工具在 OUT/SCLD.SCX，保留原 name=SCLD.SCX 且不覆盖执行文件。

源码固定装在 /SYS/SRC，保留 user 以下的目录和真实原文字节；标准和公开
头只按当前 stage_m10a2_sdk.py 的 HEADERS、STANDARD 及显式SDK插件清单
进入 /SYS/INC，WIRE/private/旧SCAPI不进入INC。实际编译器/ROOT私有源和
SCAPI只在SRC闭包。literal include闭包保守包含条件分支，CLI外壳精确选
SCCC_CLI=1；缺失/非literal/超路径边界明确失败。SCDB最终引用可持久访问
的SYS/SRC路径，不把随机TMP路径写入最终调试来源。

最终HOST构建/CORE/精确HOST LOGIN2及五库/桌面/字体等由Root给显式SHA
输入。旧G1回到ui-platform-guest-05原报告和真实stdout核验完整摘要；
SCCLIB/SCLIB原生成头与当前SC32API.H不相同，Root已明确两库也各重生成一次，
八个普通库统一使用当前SCO链；原P3功能/性能证据继续复用而不重跑矩阵。
SCSOUND只接用户已授权HOST codec例外。原生新产物仅取回，不在生成VM安装
或执行应用。

每个真实tool命令直接由现有Harness.run/Guest.command前台执行，工具身份
记录NOT_OBSERVED，不把Shell BACKGROUND的spawn_fragment包装PID当成工具。
run-01真实启动/warm后在旧可选观察器处失败，准确旧源码/摘要已先归档，
不改写旧运行证据。未观测PID不阻断生成；真实输出/返回码/工具和产物SHA、
来源及整轮quiet/owner前后账继续严格检查。同一VM复用全部输入，
QemuSession只准备其必要数据副本。

每个SCO核实际source-file原文字节/FNV、行表与imports；每个链接产物用
现有SCL/SCD验证器和正式NATIVE.JSON作完整合同比较，再核SCDB的image
SHA、地址域、源身份、对象身份、行号及真实段范围。最终SCLD也走SCO链接。
生成成功不等于应用执行/安装/登录issuer重生/整里程碑验收。
