# M10a2 交付检查与实际证据

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
