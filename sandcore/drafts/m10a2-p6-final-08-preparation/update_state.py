"""状态文档不参与运行源冻结；实际GUI结果仍由当前VM产生。"""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def put(p,s):p.write_bytes(s.encode('utf-8'))
p=ROOT/'docs/M10A2-LIVE-GUI.md';s=p.read_text(encoding='utf-8')
begin=s.index('<!-- CURRENT-BEGIN -->');end=s.index('<!-- CURRENT-END -->',begin)+len('<!-- CURRENT-END -->')
current='''<!-- CURRENT-BEGIN -->
2026-10-17。完整M10a2目标保持active，主代理独立工作。四文件试玩仍为native06；用户盘未覆盖。源码新批为HOST14/native08/final08。

| 项目 | 实际状态 |
|---|---|
| 默认显示与QEMU | GTK实际per-monitor/144与宿主150%一致，WHPX优先；原尺寸设置/文件对话框清楚。用户现已实际确认“我试了现在可以快捷键”，与代理先前未确认的自动按键分列 |
| 定时器错误修复 | SCUI Timer回调沿普通消息ERROR通知合同，不直接结束上下文，保留firing/retired/revision生命周期；SCO16、HOST14、native08通过。尚不声称已证明Files313原始根因 |
| 原生刷新08 | 只重新生成SCUI，四个07应用经逐项实际对象/链接/SCDB/源闭包/get资格审阅后复用。27对象/8普通库/21产物；最终PF零差额，严格SDK reader未改，原盘/输入保持 |
| 新CORE/SDK | final08三步骤退出0，inspect_final通过；CORE 83d5c7c91ee3648cfa74cc12cb715e20e7bdd8da14820e42b074b3fc93a24294；SDK494项/17488158B。构建通过不等于GUI验收 |
| 原16页调查 | 独立只读VM已正常关闭：唯一新64KiB arena内两块分别为数字9/3的1600B字形，原盘保持。没有改写native07原FAIL，也未豁免GUI01的205页 |
| 当前GUI08 | exec42995、Windows QEMU3328，显式WHPX；实际HOST14/native08/final08。启动普通会话中，SID以真实READY为准；主源/tools/SDK/CORE/活helper保持冻结 |
| 已有可复用业务 | GUI04复杂SCW/独立SCL/四TU/真实调试、PCM与零差owner；GUI06安装73B持久提交和正常exit0。GUI06 Files取消exit313及Settings临时.C31仍按缺陷保留 |

新证据：[native08](../evidence/m10a2-native08/review.json)、[只读heap调查](../evidence/m10a2-parent-heap-02/review.json)。原07报告未改，资格审阅使用独立DECLARED_P6_ARTIFACT_FACTS_AUDIT_PASS，明确original_owner_account_pass=false；不将原轮次失败换名为PASS。新08必须且已经另取真实owner终账。

接续：查看GUI08实际READY与新屏，普通Terminal重定向启动Files采原始错误→取消拖放/正常退出→Debug快捷键→Installer默认及窄布局→必要代表分辨率/主题→恢复原SID并注销测试SID、唯一finish。未变业务复用，不重做完整开发示例或无关矩阵。全合同剩余会话/权限/性能/兼容/交付继续，不能停在构建或局部GUI检查点。

SCAPI.H冻结摘要81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1。只用main、无Release、不读私钥、不重启M8；用户盘与旧成果保留。
<!-- CURRENT-END -->'''
put(p,s[:begin]+current+s[end:])
p=ROOT/'docs/SCUI.md';s=p.read_text(encoding='utf-8');s=s.replace('仅实现收齐，尚未构建或客体回归；Files取消313的实际根因仍需原始错误诊断确认，不以此改写历史结果。','SCO16、HOST14及真实native08生成通过，最终PF差额0、严格SDK reader通过；新GUI08正在进行，Files取消313的实际根因仍需原始错误诊断确认，不以此改写历史结果。');put(p,s)
for name in ('M10A2-P6-IMPLEMENTATION.md','M10A2-CURRENT-GOAL.md','M10A2-IMPLEMENTATION.md'):
 p=ROOT/'docs'/name;s=p.read_text(encoding='utf-8');at=s.index('\n')+1
 put(p,s[:at]+'\n> 2026-10-17当前：HOST14/native08/final08真实通过，SCUI定时器错误通知合同已修；原生最终PF零差额。新GUI08显式WHPX运行中，完整目标active。当前句柄、源冻结、失败保留和接续以[M10A2-LIVE-GUI](M10A2-LIVE-GUI.md)为准；下文按原时点保留。\n'+s[at:]+ '\n修订：2026-10-17，保存native08定向刷新、final08严格SDK/CORE与GUI08待验边界。\n')
p=ROOT/'docs/M10A2-DEFAULT-ENTRY.md';s=p.read_text(encoding='utf-8');s+='\n2026-10-17后续：用户实际确认“我试了现在可以快捷键”。代理本次暂停无盘窗口检查被用户Esc中止并正常关闭，未取得可引用的自动按键结果；用户的实际确认与代理自动化证据分列。新HOST14/native08/final08已有实际构建/生成与零差owner，尚未覆盖用户试玩盘，修后GUI08继续。\n';put(p,s)
p=ROOT/'docs/PERFORMANCE.md';s=p.read_text(encoding='utf-8');s+='''
## 2026-10-17 常驻桌面时钟字形归属

只读诊断复用旧原生运行底座和失败07盘的临时写入层，没有重新编译五目标或复制完整数据盘。实际44→49字形，allocator2364→2380页；唯一新65536B arena的两个有效1600B分配分别由真实ScResourceCache的数字9、3持有，face12/18实际像素，原盘未变。该结果说明此独立调查的内存用途，不回填原07已经结束的16页快照，不改GUI01的205页FAIL。

新的08观察在同一次暂停中取得PF/全部owner/allocator/cache/PTE，只有精确的新字形块、实际arena帧与新增私有PTE完全一致才允许归属；没有固定16页宽限。08实际最终页差0、归属0、未解释0，未用到缓存差额归属。这不是速度测量，不据此声称整机性能达标。详见evidence/m10a2-parent-heap-02及m10a2-native08。

修订：2026-10-17，记录真实字形缓存页用途和严格同暂停归属边界，保留原失败与无性能结论。
''';put(p,s)
p=ROOT.parent/'README.md';s=p.read_text(encoding='utf-8');s=s.replace('其它分辨率与本批应用修复仍在定向回归，范围见','快捷键已由实际使用确认可用；HOST14/native08/final08通过，新应用操作与其它分辨率仍在定向回归，范围见');put(p,s)
p=ROOT.parent/'README_EN.md';s=p.read_text(encoding='utf-8');start=s.index('Current development is ');end=s.index('\n\n',start)
put(p,s[:start]+'''Current development is **SandCore M10a2 — The Application Platform Era**. Ring3 dynamic libraries, the system libraries, SCUI, the desktop, native application generation and a complete SDK have execution evidence. The current HOST14/native08/final08 build passed; the native generation's final physical page difference was zero. Focused ordinary-user GUI checks remain in progress, and the full goal stays active. The Windows launcher uses DPI-aware GTK and prefers WHPX; keyboard shortcuts have been confirmed in actual use. See the [current execution record](sandcore/docs/M10A2-LIVE-GUI.md) and [launcher evidence](sandcore/docs/M10A2-DEFAULT-ENTRY.md). The accepted runnable baseline remains **M10a1**, accepted on **October 7, 2026** and distributed as a **GitHub Preview pre-release**. Development uses `main`; M9, M8a and earlier artifacts remain preserved. Full M10 has not been declared complete and the base kernel has not been frozen.'''+s[end:])
print('current state saved; GUI08 remains running and unaccepted')
