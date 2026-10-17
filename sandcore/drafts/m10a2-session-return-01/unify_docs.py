"""统一当前交付入口；保留旧检查点及失败的日期和原文。"""
from pathlib import Path
import re,json,shutil
ROOT=Path(__file__).resolve().parents[2];REPO=ROOT.parent
def read(p):return p.read_text(encoding='utf-8-sig')
def write(p,s):p.write_bytes((s.rstrip()+'\n').encode('utf-8'))
def revision(p):
    write(p,read(p)+'\n\n修订：2026-10-17，统一HOST21/native11/final12、GUI12与登录回归实际范围，SDK及发布入口同批整理。')
def current(p,body):
    old=read(p);title,rest=old.split('\n',1)
    if '<!-- CURRENT-BEGIN -->' in rest:
        rest=re.sub(r'<!-- CURRENT-BEGIN -->.*?<!-- CURRENT-END -->',body,rest,count=1,flags=re.S)
        write(p,title+'\n'+rest)
    else:
        write(p,title+'\n\n<!-- CURRENT-BEGIN -->\n'+body+'\n<!-- CURRENT-END -->\n\n<details>\n<summary>此前日期的原始记录；旧“当前/待验/正在运行”不作为本次状态</summary>\n\n'+rest+'\n\n</details>')

def main():
    acceptance='''# M10a2 交付检查与实际证据

SandCore M10a2 — The Application Platform Era。2026-10-17。本页按根M10a2RULE.MD的完整合同交叉检查，真实构建、系统内生成、GUI与历史失败分别记载。正式发布操作在文件/SDK检查后执行，实际提交号、GitHub签名验证结果、Release URL和包摘要由发布凭证记录；本页不预先声称已经发布。

## 完整合同交叉表

| 阶段 | 已落地交付 | 实际证据入口 |
|---|---|---|
| P0 审计/架构 | SCL二进制/依赖/错误合同，组件迁移审计、公共API分层 | M10A2-DESIGN.md、M10A2-COMPONENT-AUDIT.md、SCL.md |
| P1 动态运行时 | Ring3运行期装载、重定位、GetSymbol、依赖/版本/初始化回滚、卸载、异常/跨进程隔离；动态SCX | evidence/m10a2-dynamic、m10a2-native；SCL.md |
| P2 SC32 | 独立无标准库循环依赖桥、有序窗口事件、文件对象/稳定读/租约/条件替换/持久ACK；旧ABI冻结 | evidence/m10a2-sc32；SC32API.md、SC32-FILE.md |
| P3 标准/通用库 | 32位C兼容范围、真实stdio/退出生命周期；字符串/容器/路径/配置/编码等通用工具 | evidence/m10a2-stdlib；SCCLIB.md、SCLIB.md、SCCC-AGGREGATE-ABI.md |
| P4 GUI | 标准窗口/菜单/工具栏/树/表格/文本/文件对话框/焦点/模态/虚拟列表、Classic/Aurora、100/150/200%与Unicode | evidence/m10a2-ui-platform、m10a2-ui-refresh、m10a2-p4-context-and-p5-generation；SCUI.md、SCUI-TEXT.md |
| P5 桌面/Shell | 真Ring3桌面、图标多选/持久布局/快捷方式、任务栏/开始菜单、关联、跨应用剪贴板与完整Drop/FileJob | evidence/m10a2-p5-desktop-layout、m10a2-p5-file-clipboard、m10a2-p5-file-jobs、m10a2-p5-desktop-resources、m10a2-gui12 |
| P6 主要组件 | Files2/Studio2/Debug2及其余11个GUI入口和桌面；统一标准控件、worker、真实SCW多源编译/调试/发布 | M10A2-P6-IMPLEMENTATION.md；evidence/m10a2-p6-gui02、m10a2-p6-gui04-sdk、m10a2-pio-gui10、m10a2-gui11、m10a2-gui12 |
| P7 SDK/交付 | 9库/完整公共头/原生SCCC及SCLD/固定源码许可/独立多源示例，风险相关GUI和性能证据；最后仅文档restage和交付包检查 | M10A2-SDK.md、SDK-RESEARCHDESK.md、M10A2-RELEASE.md、evidence/m10a2-session-return及本次delivery审计 |

上表证据路径均相对于sandcore/，规范位于docs/。这些证据按实际源闭包复用，不把每个历史PASS说成最后CORE的整机重测。

## 最终运行字节与本次定向检查

- HOST21：41产物、exit0、源码变化0；报告SHA256 `738e41d51bc4cd026cdc658c0b8d484614c59e7952b4908be389eaa919a76749`。
- native11：仅变化的DESKTOP由客体SCCC/SCLD刷新；严格保留8普通库、21产物、27对象的实际来源；整体physical/unexplained为0。报告SHA256 `66396e2bb8eaf81eefffa02fb4fbaffb23c99a0fddf749ba81559b0d07dad9c0`。
- final12：实际构建CORE与严格SDK；CORE SHA256 `44d0b635f4f2597ef8009067de0da299bb36270c367589a8898711a64361f6b2`。CORE发行表逐项绑定实际原生LOGIN2、DESKTOP及登录五库/UAX资源。
- GUI12：无额外按住延时的快拖、Cancel保源、真实Copy168B与REC CRC/DURABLE、自动刷新；普通用户实际编译的DROPUP在真实释放手势下9项通过。Installer结果列可见，正常退出0；唯一资源账13950→13950。
- GUI13：真实PS/2输入，“退出桌面”返回固定LOGIN2且原会话保留；root空口令拒绝；默认未设密码mio空输入进入普通SID5；实际注销SID5消失，重新登录创建SID6。驱动最后退出1，原报告FAIL_OR_INTERRUPTED，原因和资源范围见下一节。
- 用户实际确认SDL快捷键与全屏鼠标正常；不把这项反馈冒充所有宿主后端的自动化覆盖。

## 保留的限制和失败

GUI13收尾原始资源谓词FAIL：13950→13958页，view对象+1、池页0，其余池/字体/当前桌面增量0；原全部活进程物理帧和页表不变，无残留登录或临时应用。首次保留的LOCKED SID3视图/desktop_state生命周期与实际desktop.o中32660B的状态尺寸（8页）匹配。这是源码与编译尺寸归因，未直接捕获这8页的地址归属；原FAIL及unexplained8保持，不改为零差PASS，不设8页通用宽限。主代理在该失败谓词后停止自己的控制器，QEMU已关闭。完整记录见[登录修复证据](../evidence/m10a2-session-return/README.md)。

SCCLIB明确是C32/byte/C locale范围，不是完整ISO C：浮点、64位、scanf、完整locale/wide尚不支持。SCL初版每进程私有代码，禁止写代码页；不宣称跨进程物理代码共享、NX或完整W^X。SCSOUND为用户明确允许的宿主codec例外，其余8库和所列SCCC/SCLD是实际客体产物。SCRES不是单独发行库。

当前分辨率最高1920×1080；16:10宿主全屏等比例呈现会有黑边。中英内容并存，不宣称全系统完整双语。网络沿M10a1的IPv4/e1000和HTTP边界，HTTPS/TLS未实现；GPU/SMP/IPv6不属于本次完成宣称。M8封存的游戏/影片及旧组件原样保留，不以本版重启或宣称它们的新验收。

PIO对照只证明同230424B/CRC697678241文件的读盘：guest PIT 0.59→0.31秒、宿主3.188→0.437秒；同冷资源177.593→26.109秒。没有据此宣称全系统帧率或所有启动场景达标。早期GUI03心跳失败、native07/GUI01资源失败以及GUI12输入失误均保留原报告，后续证据按具体修复范围闭合。

## 用户复核入口

解压四文件运行包，安装Windows QEMU与Python后运行run.bat。默认SDL、最近邻/物理DPI、WHPX可用时优先；Ctrl+Alt+F全屏、Ctrl+Alt+G输入捕获、Ctrl+Alt+2/1显示monitor/客体。默认mio没有设置密码时可在固定本机登录界面留空；设置过密码后按实际密码验证。退出桌面保留会话，注销关闭会话。

Files中可检查目录/类型/多选/复制移动与持久结果；Studio打开SDK中的SCW工作区，先构建RDSTATS，再构建四TU的RDESK；F6运行、经Debug2查看模块/符号与断点。SDK中的BUILD.SH/响应文件和README是实际开发入口，细节见SDK-RESEARCHDESK.md。无需再次完整运行历史矩阵来复核文档整理。

修订：2026-10-17，统一完整合同、真实证据复用、最终字节与登录资源限制。
'''
    write(ROOT/'docs/M10A2-ACCEPTANCE.md',acceptance)
    release='''# SandCore M10a2 — The Application Platform Era

本版建立可复用的Ring3应用平台：真正SCL动态链接、SCCLIB/SCLIB/SC32API/SCUI、统一桌面和主要应用，以及可在系统内编译复杂多源应用的SDK。发布前检查与实际证据见[M10A2-ACCEPTANCE.md](M10A2-ACCEPTANCE.md)。GitHub发布在检查后执行；实际签名提交、发布结果及附件摘要另由delivery/publish凭证记录。

## 交付内容

| 附件 | 内容 |
|---|---|
| SandCore-M10a2.zip | 根目录严格4文件：run.py、run.bat、sandcore.img、sanddata.img；干净基盘迁移，含新平台/应用和内置SDK |
| SandCore-M10a2-SDK.zip | 独立公开SDK：INC、9库、实际原生编译/链接工具、SCW/多源例子、规范、固定源码与完整许可 |
| SHA256SUMS | 两个附件的SHA256摘要；Release上传后按GitHub资产摘要核对 |

不捆绑QEMU/GCC/Python本体。使用已安装Windows QEMU（默认C:/Program Files/qemu）及Python，双击run.bat；启动参数见run.py --help。run.bat为纯ASCII。默认SDL原生相对鼠标、物理DPI/最近邻，WHPX不可用时回退TCG；当前最大1920×1080，16:10宿主全屏黑边是保持比例的结果。

## 主要变化

- 动态SCL、版本/依赖/符号/生命周期与真实SCCC→SCO→SCLD开发闭环；新应用通过大写公共头和库开发，旧SCAPI.H原字节/ABI保留。
- Ring3桌面、图标持久化/多选、开始菜单/任务栏、关联、跨应用剪贴板和有释放凭证的拖放；文件作业支持持久REC、权限与条件提交。
- Files、Studio、Debug、Notes、Canvas、Lens、Settings、Monitor、Calculator、Terminal、Palette、Sound、Session和Installer统一框架。Studio的工程后缀为SCW。Installer是既有文件安装流程的GUI，不宣称完整软件包依赖管理器。
- 实际多源ResearchDesk开发/运行/动态插件/调试证据；缩放/Classic/Aurora、文件/图形/音频/会话定向证据复用。
- 修复高DPI入口、全屏鼠标、快拖取消、终端滚动与安装结果列；退出桌面返回登录并保留会话，注销关闭当前会话，固定本机LOGIN2允许未设密码的默认mio空输入登录。root、普通su及网络没有此空口令例外。

## 准确范围

8个普通系统库与SCCC/SCLD来自实际客体编译；SCSOUND是明确记录的宿主codec例外。SCCLIB只承诺文档中的C32范围；SCL初版不宣称跨进程物理代码共享或NX。当前无HTTPS/TLS、GPU、SMP、IPv6或全系统完整双语承诺。

**登录回归功能通过，但该轮自动资源账没有通过。** 首次保留LOCKED登录桌面后+8页，与编译出的32660B状态及生命周期匹配；未直接抓取地址归属。GUI13原FAIL_OR_INTERRUPTED、未归因8及驱动退出1保持。native11及GUI12分别有独立零差证据，不冒充GUI13零差。详细记录和截图在evidence/m10a2-session-return。其他历史失败同样保留。

C:/Users/Administrator/Desktop/test中的现有用户盘另行做系统载荷迁移，1626个未列入更新的对象（含34个HOME/桌面对象）正文、权限、属主、代数保留；原盘备份在本机build。Release使用独立干净基盘，不收集用户test数据，不覆盖它。M8封存、M9/M10a1正式产物保持；完整M10及基础内核冻结并未在本版宣称完成。

修订：2026-10-17，形成检查后发布的交付说明与已知限制。
'''
    write(ROOT/'docs/M10A2-RELEASE.md',release)
    shared='''2026-10-17。当前最终运行绑定为HOST21/native11/final12；真实构建/原生生成均结束，没有后台构建或QEMU仍在运行。CORE SHA256为44d0b635f4f2597ef8009067de0da299bb36270c367589a8898711a64361f6b2。GUI12快拖/取消/持久复制、独立UP检查及Installer列宽通过，owner13950→13950；GUI13退出/注销/默认mio空输入重新登录和root空输入拒绝已实际验证，原资源账+8与FAIL_OR_INTERRUPTED保持，不能称该轮整体PASS。

完整合同与证据交叉表见[M10A2-ACCEPTANCE.md](M10A2-ACCEPTANCE.md)，交付内容及限制见[M10A2-RELEASE.md](M10A2-RELEASE.md)。当前工作为统一文档、同字节最终SDK restage、交付包核验，然后按用户授权签名提交/Release；不再扩展QEMU矩阵。当前正式发布结果以实际publish凭证为准，不预写成功。主代理独立工作，不使用subagent或computer-use。'''
    for name in ('M10A2-IMPLEMENTATION.md','M10A2-P6-IMPLEMENTATION.md','M10A2-CURRENT-GOAL.md','M10A2-LIVE-GUI.md'):
        current(ROOT/'docs'/name,shared)
        revision(ROOT/'docs'/name)
    current(ROOT/'docs/SCSHELL-IMPLEMENTATION.md',shared+'\n\n桌面仍为Ring3 SCSHELL.SCX；退出走SC32_SESSION_LOGIN_PANEL，保留SID与后台任务；注销走SC32_SESSION_LOGOUT。真实文件worker/持久REC、图标CAT/LNK和独立SCUI/SCSHELL系统库继续复用。')
    revision(ROOT/'docs/SCSHELL-IMPLEMENTATION.md')
    p=ROOT/'docs/M10A2-SDK.md';s=read(p);parts=s.split('\n\n',2)
    parts[1]='''SandCore M10a2 — The Application Platform Era。最终来源为HOST21/native11/final12；文档统一后只restage到build/m10a2-sdk-delivery-01，不重编未变库。8个普通原生库、实际guest G1/SCLD和唯一SCSOUND宿主codec分别绑定来源，最后SDK清单/文件摘要以该目录MANIFEST、HASHES、SHA256SUMS为准。SDK staging本身的SDK_runtime_verified=false保持；真实复杂开发流程另由GUI04/GUI12和[M10A2-ACCEPTANCE.md](M10A2-ACCEPTANCE.md)证明。不能将staging工具自己的旧PREVIEW/未运行状态字段误解为抹去独立GUI证据，也不能把GUI证据写回旧staging原报告。交付包与限制见[M10A2-RELEASE.md](M10A2-RELEASE.md)。'''
    write(p,'\n\n'.join(parts));revision(p)
    p=ROOT/'docs/SC32-AUTH.md';s=read(p).replace('真实构建/客体验证正在接续。','HOST21/native11/final12实际构建完成；GUI13真实root空输入拒绝、mio空输入登录SID5、注销后再登录SID6成立。原资源账+8/FAIL保持，详见M10A2-ACCEPTANCE.md及evidence/m10a2-session-return。');write(p,s);revision(p)
    p=ROOT/'docs/SESSION2.md';s=read(p).replace('本草稿没有装盘或启动QEMU。','本段保留最初实现时态；最终native11/final12已装入独立验证盘并实际启动，见下述2026-10-17结果。').replace('guest/视觉待验。','原实现阶段guest/视觉待验，后续结果见2026-10-17记录。').replace('实现与真实客体结果分别记录于本批接力。','GUI13真实离开桌面后SID4保留、root空输入拒绝、mio登录SID5、注销后SID5消失、再次登录SID6，LOGIN2正常退出。原资源账+8及FAIL保持，见M10A2-ACCEPTANCE.md。');write(p,s);revision(p)
    p=ROOT/'docs/AUTH.md';write(p,read(p)+'\n\n## 2026-10-17 固定本机LOGIN2策略\n\n仅内核按发行字节核验的固定LOGIN2角色，可用空输入进入启用且未设置密码的默认mio/UID1000。普通Session2/su/网络、root与其它账户不获该例外；设密码后仍PBKDF，坏账户库/禁用仍失败关闭。未改账户库格式/SCAPI字节/旧寄存器语义。真实功能结果与资源边界见M10A2-ACCEPTANCE.md。');revision(p)
    p=ROOT/'docs/M10A2-COMPONENT-AUDIT.md';old=read(p);head,rest=old.split('\n',1)
    write(p,head+'\n\n## 2026-10-17 迁移落地索引\n\n原审计保持2026-10-14基线时态。现在Files2位于user/files2，Studio2位于user/studio2，Debug2位于user/debug2；桌面为user/scshell/desktop，Notes/Canvas/Lens/Settings/Monitor/Calculator/Terminal/Palette/Sound/Session/Installer的入口和原生来源见M10A2-P6-IMPLEMENTATION.md与native11完整清单。CLI及旧组件/LEGACY仍保留，不把旧源码表写成新版尚未实现。Installer承接原CLI文件安装流程，SCW为正式工程后缀。完整证据与限制见M10A2-ACCEPTANCE.md。\n\n<details><summary>2026-10-14原始基线审计</summary>\n'+rest+'\n</details>');revision(p)
    current(ROOT/'docs/M10A2-DEFAULT-ENTRY.md',shared+'\n\n**最终默认后端是SDL。** 原GTK诊断为历史阶段，现SDL使用原生相对鼠标、物理DPI与最近邻。用户确认快捷键/全屏鼠标正常。宿主CIM为1920×1200；交付默认1920×1080/150%，故等比例全屏上下约60px黑边。没有增加1920×1200模式。试玩盘已迁移新系统载荷，保留1626个未更新对象；不以旧摘要覆盖用户修改。')
    revision(ROOT/'docs/M10A2-DEFAULT-ENTRY.md')
    zh='''当前版本为 **SandCore M10a2 — The Application Platform Era**：Ring3 SCL、四大系统库、统一SCUI/桌面及主要组件、真实系统内SCCC/SCLD和多源SCW开发流程已交付实现及对应证据。完整范围见[交付检查](sandcore/docs/M10A2-ACCEPTANCE.md)和[版本说明](sandcore/docs/M10A2-RELEASE.md)。HOST21/native11/final12是最终运行绑定；GUI12资源零差，GUI13登录功能有效但首次保留登录桌面+8页的原资源FAIL保持，详情已列出。文档/SDK/附件检查后按用户授权签名提交并发布；实际发布状态以发布凭证为准。M10a1 Preview、M9、M8a及用户盘保留，完整M10及基础内核冻结并未宣称完成。'''
    p=REPO/'README.md';s=read(p);s=re.sub(r'^当前开发工作为 .*$',zh,s,count=1,flags=re.M);s=re.sub(r'^M10a2试玩入口.*$','M10a2运行包仅含run.py、run.bat和两个盘，默认SDL原生相对鼠标/物理DPI/最近邻，优先WHPX；用户已确认快捷键及全屏鼠标正常。最大1920×1080，16:10宿主全屏保留比例黑边。退出桌面回登录且保留会话，注销关闭会话；未设密码的默认mio仅固定本机LOGIN2可空输入登录。独立SDK及实际来源见[SDK说明](sandcore/docs/M10A2-SDK.md)。',s,count=1,flags=re.M);s=re.sub(r'^\| M10a2 — The Application Platform Era \|.*$','| M10a2 — The Application Platform Era | 平台、重制组件、原生多源SDK与真实GUI证据已汇总；检查后签名提交/Release。GUI13登录资源账+8/FAIL等准确限制见版本说明 |',s,flags=re.M);write(p,s);revision(p)
    p=REPO/'README_EN.md';s=read(p);s=re.sub(r'^Current development is .*$',"Current version: **SandCore M10a2 — The Application Platform Era**. Ring3 SCL, the four core libraries, SCUI, the desktop and major applications, and the native SCCC/SCLD multi-source SCW workflow have implementation and execution evidence. See the [delivery audit](sandcore/docs/M10A2-ACCEPTANCE.md), [SDK](sandcore/docs/M10A2-SDK.md), and [release notes](sandcore/docs/M10A2-RELEASE.md). Final runtime inputs are HOST21/native11/final12. GUI12 ended with zero resource difference; GUI13 verified leaving the desktop, logout and local sign-in, while its original resource FAIL (+8 pages on the first retained login view) remains disclosed. The launcher uses DPI-aware SDL raw mouse input and prefers WHPX; the user confirmed shortcuts and fullscreen mouse behavior. Maximum guest mode is 1920x1080; a 16:10 host uses aspect-preserving bars. Documentation, SDK and package checks precede the authorized signed commit and Release; actual publication is recorded separately. M10a1, M9 and M8a are preserved. Full M10 and the base-kernel freeze are not declared complete.",s,count=1,flags=re.M);s=re.sub(r'^\| M10a2 — The Application Platform Era \|.*$','| M10a2 — The Application Platform Era | Platform, rebuilt components, native multi-source SDK and GUI evidence collected; signing and Release follow delivery checks. GUI13 resource FAIL and other limits remain disclosed |',s,flags=re.M);write(p,s);revision(p)
    p=ROOT/'README.md';s=read(p);s=re.sub(r'^> \*\*M10a2：.*$','> **M10a2：SandCore M10a2 — The Application Platform Era。** 平台/组件/原生多源SDK及GUI证据已统一到[交付检查](docs/M10A2-ACCEPTANCE.md)、[SDK](docs/M10A2-SDK.md)与[版本说明](docs/M10A2-RELEASE.md)。最终HOST21/native11/final12，GUI12资源零差；GUI13退出/注销/重新登录有效，其首次保留登录桌面的+8页资源FAIL如实保留。检查后按最新授权签名提交和Release。下方M10a1及更早状态为历史记录，不限制本轮已授权发布。',s,count=1,flags=re.M);write(p,s);revision(p)
    p=REPO/'m10a2handoff.md';old=read(p);head,rest=old.split('\n',1)
    handoff='''## 2026-10-17 最终交付接手点

下面2026-10-15的详细记录是历史，不能按其“GUI未验/P5未集成”重新开工。唯一当前事实为HOST21/native11/final12、GUI12闭合及GUI13登录功能/资源边界，详见sandcore/docs/M10A2-ACCEPTANCE.md。本轮没有活QEMU/构建，也没有恢复任何子代理；用户已明确禁止subagent和computer-use。

### 固定输入与结果

HOST21报告SHA738e41d51bc4cd026cdc658c0b8d484614c59e7952b4908be389eaa919a76749；native11内报告SHA66396e2bb8eaf81eefffa02fb4fbaffb23c99a0fddf749ba81559b0d07dad9c0；CORE44d0b635f4f2597ef8009067de0da299bb36270c367589a8898711a64361f6b2。对应build/m10a2-main-session-return-01、m10a2-p6-target-refresh-11、m10a2-p6-final-12。最终SDK从同来源restage到build/m10a2-sdk-delivery-01；不再重编或扩展矩阵。

GUI12证据sandcore/evidence/m10a2-gui12，review SHA3b96f6369e85ca4d7ff98eb631dcbaa0a2b2cb6f5266a314e66dc2b9a9dc0680，快拖/取消/168B持久复制、实际客体DROPUP9项和Installer列宽、正常关闭/owner0。GUI13证据sandcore/evidence/m10a2-session-return，review SHA3fdfa7469a0f099ec78903431e6bbbc04cf7dd6483dca309e6b66f7aea456c0a，真实退出/注销/两次mio空输入登录/root拒绝，原资源FAIL/8页地址归属未直接捕获，驱动由Root CtrlC结束为exit1，QEMU已关闭。不要改旧FAIL或追求全绿重跑矩阵。

### 用户盘与最终包

test保持严格四文件；数据盘已更新SHA362e3ce3ec4df694561443b761270bcae4fd0e13382c066681e10ba91d55a983，保留1626个非更新对象含34个HOME/桌面对象；其他三文件不变，账户库不重置。原盘备份sandcore/build/m10a2-test-session-update-01/sanddata-before.img，update.json保留明细。首次prepare因持有Windows读句柄而os.replace失败，原目标未改；完整准备盘复核后释放句柄完成原子发布。helper现改为build内准备再关闭句柄提交，仅静态审阅此helper修正，没有再次迁移用户盘。

最终Release使用原M10a1只读基盘3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64新建一次干净迁移；不打包用户test盘。SCAPI.H必须保持81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1。SDK staging字段保持自身未运行语义，独立delivery凭证链接实际GUI，不改写旧报告。

### 用户最新授权与发布顺序

用户要求同步文档/SDK，检查完再做密钥验证的commit和Release。原“不自行Release”由此次明确授权覆盖；仍main、不强推、不读SKM私钥、不自动清理用户或旧代理目录。先完成文档/SDK/zip/摘要检查，再推送保留本地历史、用GitHub自动签名接口创建最终提交并实际核Verified，最后发布附件。没有可用用户GPG私钥，不伪造本地签名；GitHub自动签名不涉及用户SKM密钥。

WSL gh网络直连超时，但Windows现有127.0.0.1:7890代理可通GitHub；drafts/m10a2-session-return-01/github_api.py通过WSL gh凭据管道使用现有账户，令牌不写磁盘/日志。WSL→Windows临时TCP中继被拒绝且已停止，不改VPN/防火墙。远端main已读为01da6943c82364c059df8be9a7128e9006ea1c83，本地提交前HEAD c72923c；任何推送必须重新核远端祖先，不强制覆盖。

### 剩余交付动作

检查SDK当前规范与manifest/实际9库、final12发行表/SCAPI、独立干净盘和4文件zip、SDKzip及SHA256SUMS。保存evidence/m10a2-delivery-01的审计与发布凭证。只显式stage本轮拥有的文件；.zcodeignore及defaults-upgrade/image-tools/native-syntax-fix/source-capacity/sdk-final/session2/settings-installer/sound-cover/sound-review/studio2/terminal2/ui-layout-fix未跟踪目录属于旧代理/用户，不能批量加入。签名commit与Release必须以GitHub实际结果为准，未完成不得写成已发布。达成完整目标后再complete。
'''
    write(p,head+'\n\n'+handoff+'\n<details>\n<summary>2026-10-15详细历史交接，原文保留</summary>\n'+rest+'\n</details>');revision(p)
    print('DOCS_UNIFIED_CURRENT_FACTS_HISTORY_PRESERVED')
if __name__=='__main__':main()
