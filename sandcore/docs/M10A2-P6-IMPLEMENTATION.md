# M10a2 主要应用与工具链实施状态

## 当前最高检查点

2026-10-16：完整 run03 已实际生成 8 普通库、21 工具/应用记录及27 SCO，整体 physical_delta=0、源码/原盘不变。已生成旧 native-bound CORE 和九库 SDK，GUI01 实际普通身份运行了 Files/Notes/Studio/Terminal 的部分流程，但保存和构建暴露共享层缺陷，最终 +205 页未归属，原报告 FAIL 保留。

SCW 工作区、Save As 精确查询、类型列、图标透明合成、紧凑名称框、Studio namespace lease 已合主源。HOST07 exit0/41产物/源变化0，SCO08 三变化目标后端检查通过；refresh-vm-03 已实际刷新 SCUI/SCSHELL/STUDIO2，其他原生来源严格复用；六次编译/链接、取回与严格审计通过，PF14036→14036，源/原盘守恒。新 CORE/SDK 正在接续，修复后的 GUI 尚未宣布通过。实时状态、命令和恢复边界以 [M10A2-LIVE-GUI.md](M10A2-LIVE-GUI.md) 为准，下方记录保留原检查点时态。

修订：2026-10-16，同步完整原生来源、SCW 与共享层修复、GUI01 失败边界和三目标刷新状态。

## 当前客体生成检查点

2026-10-16，当前统一HOST凭证为 `build/m10a2-main-studio-01/p6-unified-host-02.json`：exit0、41产物、1320输入、构建期间源码变化0；`codec-host-proof-02.json`绑定该批SCSOUND。Files、Calculator、Palette、Monitor、Sound、Settings、安装GUI和Terminal的菜单快捷键已通过既有ScUiCommandSet接线，八目标SCCC/SCO/SCLD宿主后端预检07通过。HOST01与旧codec证明保留为历史，不作为这八个新字节的凭证。

实际Windows QEMU `m10a2-p6-guest-generation-run-02` 已用旧真实G1生成新版SCCC（load257350B），再由该SCCC生成动态BOOT/SCLD.SCX及SCLDR.SCO。执行BOOT/SCLD时停止于ROOT通用提示 `dependency or symbol (-11)`；-11实际是SC_SCL_IO，尚未确定发生于读取、映像保护或调试模块登记，不能称为缺符号或原生链接通过。失败日志、完整stdout、产物及桌面截图保留，源码/原盘守恒检查通过。此前run01止于主控错误地把后台Shell包装进程当编译器身份，已改为现有前台command，旧源码和报告另存history/run-01。

已确证并修复：debug来源只读open传writer=0，FS严格返回INVALID；现内部调用使用稳定非零task_generation(pid)，close仍为0，不改FS权限或pending合同。HOST03增量构建41产物/1320输入/变化0，通过preflight03；真实run03已执行原生SCLD，ROOT/SC32API/SCCLIB/SCLIB/SCUI的系统内编译、链接、取回与SCDB校验通过，余下目标仍在同一VM继续。源码已存main@cfdcd16，WSL推送退出1，远端更新未确认。

当前出口：完成同批27目标原生生成；随后以真实native LOGIN2/五库/桌面字节重新生成发行摘要和CORE，并完成普通用户GUI/SDK真实流程。完整M10a2仍active，不能用本段的工具编译成功替代应用运行或最终验收。

修订：2026-10-16，保存HOST02、八应用快捷键预检及真实客体原生生成的I/O失败边界。

## 统一生成前的当前状态

2026-10-16，本批实现、资源接线和正式原生清单已接齐。统一HOST构建 `build/m10a2-main-studio-01/p6-unified-host-01.json` 返回0，41个产物齐全，构建前后源码变化0。包含实际CORE/引导、九库、十五个界面入口、文件/音频worker、SCLD/SCCC与PTY所需SH/nano；没有构建封存游戏，也没有生成或修改数据盘。CORE映像597520B/BSS384840B只是本次宿主链接事实，还没有新客体启动结论。

SC32当前193导出，SCUI1.3/v4当前170导出。新增Session2/Login2、typed Auth与专页Secret控件；固定LOGIN2角色只允许发行摘要绑定的五库/UAX读取和本人必要调用，不能取得普通Broker对象或硬件SYSTEM。正式native库/LOGIN2出来以后必须重新生成发行头再构建CORE，不能拿HOST摘要配另一组字节。

Sound2完整GUI、普通音频worker和封面提取现已接齐。HOST GUI23716B/83导入，worker17540B/39导入，SCSOUND122536B/五导出。实际SCCC→SCO→SCLD宿主适配也通过。切歌QUIT ACK后结束当前接收批次、双方按Broker flags识别EOF；旧STOP交接已提交以后不再允许取消新候选。封面保ID3/FLAC/Base64和旁置回退，内存分块传输，正常播放不建临时图片。证据仍不等于客体音频/交互验收。

原生预检中发现并解决了两个实质缺口：新组件的C99复合字面量/柔性尾数组改为当前SCCC可表达的等价代码，图像tile的24B头/4120B最大记录与历史预算不变；Source/once表从固定64项改为真实动态分配，保15bit位置格式、路径、行号和稳定正文arena。新编译器实际后端自编译load257350B，没有扩大旧262108B加载上限。P6对象检查03/04/05/06按改变闭包共覆盖27个目标，全部真实后端/严格链接通过；这些是HOST IO适配结果，正式NATIVE.JSON已逐项固定到主源。

最终SDK和ResearchDesk四独立TU/独立RDSTATS.SCL已整合。SDK严格区分HOSTLINK与NATIVE.JSON，普通八库和G1/SCLD必须有真实客体来源；仅固定SCSOUND接受既有用户授权的宿主codec例外。`codec-host-proof.json`已从本次实际构建提取，未把它称为native。SCCLIB/SCLIB此次依赖的公开SC32头确有变化，将在同一VM各重生成一次，原P3功能/性能证据继续复用。

默认入口升级也已接入：十四个新版应用注册及关联；仅全文和UID/GID/mode完全等同发布基线的MENU/LNK作候选转换，用户自定义内容保留；保存CAT只投影完全等同原默认的ACCOUNT Imported项，SYSTEM记录继续按旧Load合同拒绝。原盘只读，旧SCX保留。新目标缺失明确回退旧默认，安装GUI缺失则跳过，不拿CLI代替GUI。

下一步为一次Windows QEMU实际系统内生成、按新native字节重签发发行摘要，再做同批普通用户GUI/SDK/权限/回收与截图验收；不扩无关矩阵。完整P5/P6/P7和M10a2目标仍active。

修订：2026-10-16，完整主源整合、27目标原生后端宿主预检、41产物统一HOST守恒构建与最终生成前状态。

## 最新主源检查点（覆盖下方较早状态）

2026-10-16，十二个应用已经接入主源：Files2、Notes2、Debug2、Calculator2、Palette2、Monitor2、Studio2、Canvas2、Lens2、Settings2、文件安装GUI、Terminal2。`build/m10a2-main-components-02/host-build.json` 固定本批十八个HOST产物、日志及构建后输入摘要；这些应用尚未取得本轮真实客体流程/视觉验收，不把HOST和原生后端宿主适配混称系统内执行。

- Studio2保留真正逐TU的SCO/SCLD工程流程，Capture20B、SCCC_INCLUDE和共享Tab的关闭/重命名已合入。旧目标保存使用Preserving，旧Publish语义不变。
- SCGFX为独立Ring3库，Canvas2/Lens2复用共享Canvas。SC32图像内存输入已接真实IMAGE.SCX服务，不能只换SCGFX库而继续使用旧服务。Canvas/Lens覆盖已有图像保留原所有者/权限。
- Settings2包含六页设置、预览回退、配置草稿和条件激活。安装GUI只提供原install的文件/目录/UID/GID/rw功能；没有包格式、包数据库或新包管理目标，旧CLI保留。
- SCTERM为独立Ring3终端库，Terminal2通过真正双向PTY持续运行同一个SH。新增20C..20F及kind8/9，旧串口/TTY保持。nano既有compound literal仍不在SCCC支持范围；新nano候选仅有GCC对象证明，不伪称系统内编译成功。
- SC32API维持1.4/v6并保留前166项，Capture、Audio/Simd、Preserving、Config、Image和Terminal累计189导出；当前HOST149402B。SCUI1.2/v3共165导出，FLOW实际换行测量及普通滚动范围/命中/EnsureVisible已合入并HOST重建354076B。SCSHELL1.1/v2共96导出；CAT/LNK旧目标保存同样保留原元数据。
- SCSOUND五函数/19导入库HOST构建通过。主代理已写Sound2 GUI/受管候选worker状态机；当前GUI真实SCCC→SCO→SCLD宿主适配46160B/85导入，属于宿主IO证据。封面元数据与最终清单正在接齐，没有客体音频或画面结论。

Session2/Login2和专用Secret控件、完整SDK与多TU独立第三方示例仍在隔离草稿实施。随后生成实际客体产物，再对本批改变的行为作一次合并流程；复用未受影响的P3/P4/P5证据。下文166导出/六应用等数字属于较早检查点，保留作沿革。

修订：2026-10-16，图像、设置、终端和共享布局整合；保存HOST证据边界及音乐/会话/SDK未完成项。


2026-10-16。完整目标继续 active。下列是实际主源整合与证据边界；没有宣告完整 Phase6、Phase5、SDK 或 M10a2 验收完成。

## 已接入主源

| 组件 | 实际实现 | 当前证据 |
|---|---|---|
| Files2 | 分页目录树/虚拟详情表、增量排序/搜索、历史/书签、关联/文件菜单、剪贴板与跨窗Drop、受管文件事务/冲突/属性 | 独立模型定向检查通过；主树HOST SCX 75076B，168导入；客体流程待验 |
| Notes2 | 共享Document/Edit、菜单、查找替换、Undo/Redo、文本剪贴板、稳定读取、私有候选条件保存及未保存确认 | 主代理真实SCCC宿主后端43238B；主树HOST SCX 24356B，79导入；客体流程待验 |
| Debug2 | 真实TF/INT3动态断点、反汇编/寄存器/内存/栈、模块与符号/断点表、分割/浮动面板；真实源映像/重定位/摘要登记 | 主树HOST SCX 62765B，102导入；解码器定向检查通过；新CORE/ROOT/SC32联合客体待验 |
| Calculator2 | 32位有符号表达式/优先级/括号/十六进制/错误守卫、动态历史、ANS、编辑与结果复制 | 主树HOST SCX 19684B，67导入；同源数学检查通过；客体待验 |
| Palette2 | 实际256槽/24角色、登记边界、排序/过滤/色样、HEX/RGB复制、程序壁纸 | 主树HOST SCX 18276B，66导入；新增壁纸查询/持久化与权限待客体 |
| Monitor2 | 动态分页任务、增量排序过滤、PID+代数操作、CPU/内存/卷、120点有效历史、五视图/冻结与隐藏策略 | 主树HOST SCX 32932B，80导入；客体负载/交互/回收待验 |
| SCO/SCLD | 真正独立TU对象、符号/重定位/静态隔离/聚合ABI、响应文件、SCX/SCL封装、map/SCDB调试sidecar | 同源宿主8项及真实i386执行通过；主树HOST SCLD 47268B/22导入；真实客体工具链待验 |

所有新应用只使用公开系统库。历史源码、旧SCX和默认验收盘保留；本批没有把HOST产物覆盖正式客体入口，也没有借旧程序的证据宣称重制应用已通过。

`m10a2-apps.mk` 提供定向应用目标，`m10a2.mk` 接入对象工具和调试/壁纸增量；不触发冻结游戏或完整历史构建。整合收据在 `build/m10a2-{applications,debug2,system-tools,object}-integration-01/`。Debug既有文件按13份精确原像SHA应用，新增文件另列收据；对象编译器四份原像同样固定。SCLD仅拆开18个误导缩进的复合行，C token流逐项相同，未改算法。

## 系统库与调试底座

SC32API ABI1.4/version6：原154入口保留，追加10个调试函数和2个壁纸函数，共166导出。ROOT ABI1.2/version3共12导出。主树HOST字节分别136110B和20361B；SCSHELL96、SCUI162、SCCLIB94、SCLIB163导出仍保留。SCAPI.H原SHA仍为 `81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1`。

调试入口使用独立900..91F私有selector族；现代断点动态拥有，不占旧八槽。可信SCX加载事务登记主映像；ROOT必须在首条用户指令之前由scl_boot登记，因为Adopt时其BSS已真实使用。其余SCL在init前核源文件/段/重定位，init后发布READY，释放/异常/退出撤销。ROOT通过自己私有桥登记，不反向依赖SC32形成环。合同见SC32-DEBUG.md。

公开SC32WALL.H的两个函数已接入主源。Set沿旧0x18保持权限与先写后改；Query使用新selector006取得本人真实view，无view返回NOT_FOUND。image-selected只表示配置路径，不证明解码或显示。合同见SC32-WALL.md。尚未将未来Capture、配置007或图片API声明当作现有166导出。

## 构建证据

`build/m10a2-main-components-01/host-build.json` 固定16个实际输出的摘要、389份构建依赖与清单。成功日志为 `m10a2-main-components-01-retry4.log` 和 `m10a2-main-components-01-compiler-wm.log`；早期缺包含目录、格式/缩进失败日志保留。SCCC宿主引导容器139409B/load139373B。它是HOST_BOOTSTRAP，不是新客体G1；对象代理的实际后端容量254319B及Studio include增量容量另有同源宿主后端证据，不混淆两者。

本轮按明确目标构建六个应用、链接器、六库及process/task/sc32_debug/sc32_core/scl_boot/wm相关对象；尚未把新调试底座的CORE链接/客体运行计作通过。不要将389份构建后输入pin冒充构建前后冻结检查。完整原生生成仍需实际来源链。

## 仍在实施

Studio2主功能已收齐：真实逐TU SCO/SCLD、工程输入资格、现代stdout/stderr捕获、取消/回收、多文档、着色、停靠、调试和条件发布，最后定向检查/冻结交付中。仅在其专属draft，尚未整合主树。

Canvas2/Lens2由图像工具草稿负责真正R3 SCGFX及共享Canvas控件，保原尺寸/图片格式/透明/异步解码和旧画笔；Settings保六页配置/预览回退/权限。用户已确认安装入口仅围绕原install的文件源/目标/目录/UID/GID/mode功能，不创建软件包格式/数据库。图片API唯一由图像代理维护。用户指定代理均为GPT-6.1 Sol/max，文件所有权分离。

终端还需要真实双向交互端点/PTY模型，不能用Capture的stdin EOF冒充交互终端；音频需保MP3/WAV/FLAC、封面/跳转/音量/迷你模式及许可证；登录/会话、完整SDK与复杂第三方原生示例、P5实际跨Files拖放/关联/窗口操作/会话回归及最终交付继续。没有删减完整合同。

修订：2026-10-16，六应用/对象工具/调试与壁纸底座主源整合及定向HOST构建，明确真实原生/客体验收缺项。
