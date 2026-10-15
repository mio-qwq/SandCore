# M10a2 实现与恢复入口

正式名称：**SandCore M10a2 — The Application Platform Era**。

2026-10-14 已按用户要求开工；完整持续目标保持active。开工基线 `main@8e1c27abfe1b0c14ddff6d23464a961eabfd8cc0`；完整范围见根 `M10a2RULE.MD` 和 `M10A2-DESIGN.md`。此前SC32检查点为`9bf6d8e4fffffab8e159ee1dc1aa8277b6801463`；本批SCCLIB/SCLIB/R1与当前G1实际结果见[evidence/m10a2-stdlib](../evidence/m10a2-stdlib/README.md)，对应阶段提交从main日志定位。整个M10a2尚未完成，不能以阶段提交关闭目标。

## 当前状态

| 范围 | 源码/规范状态 | 真实验证状态 |
|---|---|---|
| Phase 0 总体架构 | 已记录旧加载器/SCCC/分页/安全基线、完整依赖及阶段出口，组件完整清单已写 | 源码审计，组件新运行验证待迁移阶段 |
| SCL v1 | 公共字节偏移头、freestanding C/Python 校验器、ELF32 宿主引导打包器及 dump/deps 已写 | 定向格式/宿主构建通过，真实 BASE/UPPER 各段/导入/重定位已运行 |
| SCLDR | 真正文件装载、迭代依赖/环检测、函数/数据导入、ABS/PC 回填、初始化/回滚、引用/地址固定寿命与独立三环启动根已写 | 宿主算法及真实两库/自动导入/并发进程隔离/显式卸载/正常及异常退出回收通过；进入SC32API基础 |
| 模块分页 | 独立 VA 区、0x280..282 分配/保护/释放、syscall 输出 RW、授权调试特例、退出回收接线已写 | 定向 WSL 构建与单次双盘 QEMU 保护原子性/只读写回拒绝/显式和退出回收通过 |
| SCCC 动态编译/动态 SCX 启动 | 动态编译/自动导入与G1已运行；本批按真实SCUI输入扩充源码/词法/池/SCL输出heap容量 | P3真实客体自举与四库执行通过；新容量编译器已宿主构建，SCUI原生生成/执行待本批 |
| SC32API | 原86入口保留；ABI1.2/v3共130入口，真实Font/Palette/Popup/Cursor/Wheel/Broker/Shared/Drop已接线 | P3旧行为复用；新HOST平台普通MIO全部87检查通过，但首轮整体因首次wait页账仍FAIL；新原生库与GUI尚待 |
| SCCLIB / SCLIB | 标准C32字节profile94导出与通用完整功能163导出、标准头/C1..C5/R1收齐 | 统一构建、实际客体生成/运行、九模式及七组253检查/精确22B文件、当前G1执行与暖态页回收通过；不宣称完整ISO C或整机性能 |
| SCUI | 完整控件/布局/主题/Document/虚拟模型/菜单/对话框正文已落盘；新增RootNext为158入口，诊断与对话框定向修复正在冻结 | 上版157入口build04通过；新158入口尚待build05。官方3013边界、G1原生库、真实键鼠/视觉/回收尚待，不宣布Phase4通过 |
| Desktop/Shell/主要应用重制 | 完整范围保留，Phase5并行设计同SID可信宿主与安全文件事务 | 尚未实施重制/验收；不将设计或旧桌面截图称新桌面完成 |
| SDK/性能/统一验收 | 出口标准已明确 | 待平台及应用完成 |

## 文件归属与接续

主代理：`SCLDR.H`、`scldr.c`、`scl_platform.c`、总体文档/构建/guest 主控。内核切片已由前子任务交接：paging/task/process/image_service/ringdebug 及 MEM/SYSCALL，已停止并行编辑，主代理负责最终构建和回归。

用户同日规定 subagent 使用 **GPT-6.1 Sol + max**，当前格式工具、宿主测试、只读审查按此配置继续。子任务只写自己明确拥有的文件，模型切换不删除落盘成果；运行状态以工具实际记录为准，不假定会话中断后仍在后台工作。

SCL 宿主引导产物将明确标 `HOST_BOOTSTRAP`。宿主 runtime 测试只用回调模拟 init/fini，不运行 i386 库机器码。只有实际 Windows QEMU 运行结果才可登记三环调用和权限/回收通过。

## 下一步及结束条件

1. 保存自动启动的真实源码/产物/失败修正和客体证据，同批提交已验证检查点。
2. 继续SC32API正式公共头、私有桥、统一错误/版本/对象和安全typed-query；新程序通过独立SC32API.SCL调用操作系统。
3. 按SC32API-DESIGN先完成有序窗口消息/真实通断键/可取消关闭/按几何提交，再完成真实可定位更新文件对象，给SCCLIB提供正确底座。
4. 随后完成SCCLIB/SCLIB/SCUI、Desktop/Shell、Files2/Studio2/Debug2与其它组件、SDK和完整统一验收。目标持续active；不以局部平台通过代替完整版本。

原 M10a1、M9、M8a 与用户盘保持；没有本轮 Release 或最终验收申请。

## 2026-10-14 首个实际引导切片

仓库中的小型证据入口为 [`evidence/m10a2-bootstrap`](../evidence/m10a2-bootstrap/README.md)，保存下述实际构建、宿主/客体报告与原尺寸截图；数据盘仅留本机。

`build/m10a2-bootstrap/bootstrap-build-01.json` 记录 WSL ELF 构建退出 0、21.43 秒与逐项输入摘要。新 CORE 完整 SHA256 为 `30928d3dd5778bd9a66f7f2e2badc12c916179a1978f8e7e4a957e44e1e12f43`；载荷 383216B，BSS 332840B。BASE/UPPER 分别为 616/1428B，由宿主 freestanding GCC 与 mkscl 产生，明确不是 SCCC 客体编译产物。

`build/m10a2-runtime-host/report.json` 为宿主算法证据，使用真实 mmap/mprotect、UBSan 和模拟 init/fini；`executes_i386=false`。覆盖依赖/符号/版本、重定位、初始化/分配/保护失败回滚、DAG逆序清理、引用固定和两个上下文隔离。格式定向证据为 `build/m10a2-scl-fixtures/format-tests.log`，C/Python 的代表性拒绝码与不同基址回填一致。

`build/m10a2-scl-guest-01/scl.json` 为 **DECLARED_SCL_BOOTSTRAP_CASES_PASS**。只启动一次 Windows QEMU/TCG，软盘+256MiB IDE，使用一个独立会话盘：准备回调直接修改已校验的本会话副本，没有长期保留另一份准备盘。只替换该副本 CORE 并添加三个测试文件；原基盘及其余对象的正文/UID/GID/rw/代数保持。

实际执行结果：UPPER 调 BASE 函数、导入数据、内部指针/字符串与 ABS/PC32 正确；两上下文实际函数地址为 `0x50000000` 与 `0x50008000`，修改一份可写全局不影响另一份。只读库字符串可作为 STREAMWRITE 输入，GETARGS/MONITOR/STAT 写回均拒绝；跨缺页保护失败保持原可写权限；中间地址、旧 USERFREE 和重复释放拒绝。引用重复装载只初始化一次，返回地址固定后 Close 保留调用能力，未查询地址的 Close 真正卸载。

探针内显式卸载/Destroy 的 PF 页精确恢复。外部全局页账首轮正常运行为 6838→6844，保留这 6 页的首次暖态差额，不凭猜测归因为某项缓存；之后未 Destroy 的普通 EXIT 和真实 #PF 回调退出均为 6844→6844。#PF 模式实际返回 14，未将异常暂停或卡片关闭伪装为正常返回。这里仍未证明同时运行的多个进程或全部调试模块边界；后续按具体改动补齐。

原基盘 SHA256 `3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64` 在运行前后相同。原盘已有 SCX `ls` 真实读取新库目录通过。HMP 实际 Esc/F1 输入与原尺寸 1920×1080 桌面截图已保存并查看：`build/m10a2-scl-guest-01/desktop.png`；它证明新核心启动后的现有桌面，桌面重制尚未开始。

已修复静态审查发现的两个真实寿命问题：Close 按实际初始化链清理整个退役闭包；MODULEFREE 先撤销该地址块的断点/pending/异常入口，避免同进程虚址复用污染新库。后者当前有构建与静态核对，尚无完整 GUI Debug 动态模块流程证据。SCAPI.H仍与开工 SHA256 一致。

下一项继续 SCCC 动态输出/显式导入导出和动态 SCX 启动。完整四库、SCUI、Desktop/Shell 与组件重制/SDK/性能和统一验收的范围不变。

## 2026-10-14 SCCC原生切片

检查点：[`evidence/m10a2-native`](../evidence/m10a2-native/README.md)。新编译器共享模式使用相同SCL v1和严格JSON，保存extern/static与对象限定/函数尺寸，字符串与const对象只读重排，完整RVA导入/导出/重定位；标准C支持仍按既有子集，不把本次当完整ISO C扩展。自编译输入217334B使SOURCE_MAX从196608调到262144；真实函数图增长使map从16KiB到32KiB；旧输出/SCX/code/token/栈限制继续。

native-build-02只构建编译器，退出0，SCCC0 load108337B/BSS3535343B。核心/运行时/格式源码及宿主引导库正文不变，复用首切片；m10a2.mk仅新增独立编译器目标，未重编所有应用或游戏。新的native-guest-02同一Windows双盘VM完成：SCCC0→两SCL→实际调用，SCCC0→CLI源码树→G1，G1→同两SCL→全字节一致→再次实际调用。G1 load190453B/BSS3535004B、完整SCX SHA256为`3830fca861b3c3a7dc3852f981b65d1ecd5fcdd2d3778f393ca1e60afc18432e`。

31个本切片步骤通过（不以数量作完成条件）：旧/新编译器代表SCX/SKM字节一致、map目录失败/坏JSON保留旧正文、日志输入别名保护和未Destroy退出暖页8869→8869。原盘/当前源码SHA保持，1920×1080截图已查看。执行上下文为外部SYSTEM Shell启动的Ring3程序；普通身份、真正GUI工作流和多进程/模块调试的完整出口仍待。

native-guest-01的重复RESOLVE失败保留：旧接口返回ROOTLESS规范名，共享CLI二次解析叠加了SYS。改为每输入仅解析一次、身份忽略可选根斜杠；host真实main的四项回归补齐旧host替换main未覆盖的接口语义。失败代码和SCCC0原字节已从自身验证盘提取到build/m10a2-native-guest-01/failed-code；按build清理授权仅删除此失败批两张生成盘，日志/JSON/截图/源码和当前可用盘保留，实际清单cleanup.json。

首runtime检查点b00b8ce已与远端7个既有许可证/英文README等提交合并为4b73d63并推送main，祖先与远端提交号已核对，未强推。后续仍沿main保存已验证成果，.zcodeignore继续不纳入；原M10a1/M9/历史包不改称新验收。

上述为SCCC原生切片当时的恢复状态；动态SCX启动和独立多进程已由下节新证据补齐。完整GUI调试器/SCL模块视图与四库/SCUI/Desktop/Shell/主要应用/SDK/性能仍继续，不假定会话结束后代理或VM继续后台运行。

## 完整持续目标与当前并行工作

用户2026-10-14明确“设立完整目标，直到完全完成M10a2”。本聊天目标已建立为active，不设自行推断的预算或收束范围。只有SCL/自动启动/SCCC、四大系统库、完整SCUI、Desktop/Shell、主要组件重制、SDK及真实功能/视觉/兼容/性能/回收验收均满足，才可标记完成。保存阶段代码后继续下一项，不再将“开工”当本轮工作的终点。

已验证的自动启动仍采用旧SCX1容器和固定基址，新增程序内部SCD1元数据及私有自动CRT，普通应用不手写系统调用或显式装载初始化。SYS0x283只协调装入无依赖的SCLDR.SCL并返回用户空间boot_info，所有应用依赖解析、初始化与调用继续在Ring3；根实例借用并固定到进程退出，避免重复装载和自卸载执行代码。系统库目录保护、精确SCX身份/元数据校验和失败回滚同批接线。

Phase1文件已冻结并验证。当前Phase2 ownership：sc32_contract_audit仅新SC32API.H/SCTYPES.H、user/sc32（SC32FILEWIRE.H除外）与SC32API.md；dynamic_boot_kernel仅新sc32_core.c/h、timer宽快照与SC32-CORE.md；dynamic_scx_toolchain仅新sc32_file.c/h、SC32FILEWIRE.H、必要fs增量与SC32-FILE.md；主代理负责task分发/构建/系统接线、运行验证与总体文档。全部子代理GPT-6.1 Sol+max，无并发覆盖同文件。新Phase2源码仍未验证，不纳入已验证Phase1声明。

## 2026-10-14 自动动态SCX检查点

证据入口为[`evidence/m10a2-dynamic`](../evidence/m10a2-dynamic/README.md)。WSL定向目标生成core/loader、无依赖SCLDR根、动态SCX和SCCC0，不触发全部应用/冻结游戏。CORE完整SHA256 `7d45089e45303b0fa21cb9e2934c0add365d83b5ebf914d0bba655654e503dcb`，payload390032B/BSS332840B；HOST根14612B，SHA256 `02be94772a654f32a8aff1c977840d95b5c51f3859d803d38a3c7fa368e96d1a`。新核心和库源码冻结后构建，第一次-Werror缩进拒绝保留，修复后通过。

动态SCX使用严格四字段JSON清单、64B SCD头和20/16/16B表；main和外部补丁不得落入64B CRT或元数据。CRT的cdecl调用前栈16B对齐，descriptor立即数位于RVA6/32，失败分支49，正常返回恢复48B。两端共同校验和真实host/native CLI/GUI生成针对性通过；新借用元数据入口保留完整表/字符串/段/导出/reloc约束，8项格式及metadata非法前缀检查通过。旧SCX/SKM代表字节证据复用，无ABI更改。

Windows双盘`dynamic-guest-02`在同一启动内实际完成自动HOST应用、客体SCCC0编译动态应用并执行、依赖init看见唯一root context、两个并发进程写11/22仍互相隔离、SCCC0生成并实际执行G1、G1编译并执行同应用、G0/G1输出字节相同。原生DYNAPP1416B，SHA256 `2e70b1d0944949b6cb3a5c501c37135e98570aa3140fe4a2151d447aad2cf580`；G1完整SCX SHA256 `b358fe578163e742b94502c524ebe5d44395892ae7707a56bde808d7f1931908`。实际根文件仍来自HOST GCC；客体生成的独立SCLDR另存并通过格式检查，不伪称已经执行该文件。

真实向根代码写入触发#PF，测试私有库预先注册旧异常回调后EXIT14；普通和异常退出暖态PF均8872→8872。坏ABI、缺库、覆盖CRT的重定位、包目录坏同名根、外部SYSTEM试图写SYS/LIB均安全拒绝；保护根原字节保留，既有SCX ls运行。原基盘与全部本轮安装源码摘要未变。HMP F1和原尺寸1920×1080截图已保存/查看，仍是原Aurora桌面，没有把它称为桌面重制。

首次`dynamic-guest-01`用例未注册异常回调却预期直接EXIT14，实际按既有调试合同留在故障暂停卡导致等待超时；这是验证流程修正，内核故障暂停行为未改。初次脚本、客体原生代码/日志/报告/截图保留；只有这批两张无用生成盘已按build清理授权删除269910016B，清单cleanup.json。第二批当前可用盘和原盘保留。

**下一项为SC32API基础与stdio底座。** Runtime基础/自动导入出口已有真实证据，完整GUI调试模块流程、四库、控件、桌面/应用/SDK和系统级性能仍未完成；不标记整个M10a2或基础内核冻结。

## 2026-10-14 SC32基础/有序GUI实施中

自动启动检查点`72783199e3d2e987d4124a89b45913a4290937a2`已提交并通过WSL推送main，远端API核对同一SHA。后续工作树是新Phase2实现，不能把此检查点的运行结果改称新代码通过。

CORE新增typed-query与整区RO/RW检查、真实PIT高字计数；SC32API首批65个公开函数、私有cdecl桥/稳定错误/高层对象源码已收齐。新版file正在接通动态句柄、真实定位/更新/保持打开flush、64KiB迁移与零洞续进、16扇区目录发布、完成结果按handle缓存、STD真实native-ref/弱legacy proxy和EOF；没有以旧顺序replace冒充stdio。

GUI新增消息窗口及owner/modal关系、CREATING→R3 CREATE→ACTIVATE、窗口过程表、可取消CLOSE/单窗销毁、按几何epoch提交矩形和独立EndPaint确认；ROOT负责键鼠共用64B事实FIFO，真实Ctrl/Alt/Shift/KEYUP/重复，按下位置与运动不能互相覆盖。主task已接0x300分发、GUI等待返回模式（旧EVENTWAIT不变）、退出owner清理；main接raw泵和文件后台短批次。公开API探针SC32PROBE/SC32GUARD只包含SC32API.H，检查只读输出/输入、PID代数、标准流继承、跨工具链cdecl、页计数、更新流和零洞，尚未构建/运行。

SCCC的通用cdecl调用只保证4B栈，新增官方GCC SCL flags接受4B入口并内部对齐，CRT继续16B引导。完整SC32八头静态已有340个distinct宏，超出320；MACRO_MAX增至384以容纳实际输入，输出/栈/代码预算保持。新增编译器尚未自编译或运行，不把静态计数当PASS。

最近中断已通过collaboration工具确认三个子代理均interrupted，从原文件和相同ownership恢复；没有假定后台仍运行或重新初始化覆盖落盘源码。Root拥有task/main/Make/公共API探针和raw/keyboard/mouse；GUIagent拥有WM/GUIkernel/header，SC32agent拥有R3 GUI与SC32 aggregator，FILEagent拥有fs/streams/filewire；全部GPT-6.1 Sol+max。源收齐冻结后统一针对本批构建/客体编译/真实Windows键鼠及回收，完整目标继续active。

## 修订

2026-10-15真实构建接续：stdlib-build03退出0、source_modified=[]，生成CORE payload451568B/BSS356584B、ROOT19473B(12导出)、SC32 101745B(86)、SCCLIB39256B(94)、SCLIB87101B(163)、Guard2047B及公开Probe17956B。前两批仅探针GCC预处理符号警告，已隔离SCCC专用#if与运行值Bool检查，不降Werror。test_stdlib_compiler三个分组通过，包括9个实际拒绝保旧、真正后端生成自身load234065/BSS3591864/tokens58316/source260508/code217510/data16553/fixes4844/macros268；未执行该生成G1。新批库/终止/stdio/utility仍无客体运行证据，Windows主控与UTILPROBE正在live子代理源码接线，目标active、所有库source已冻结，下一动作运行其定向主控。

2026-10-15新批实施中：已推送9bf6d8e作为P0恢复点。R1真实prepare候选/128K双栈/代码回调pin/初始化exit与旧九入口兼容源已收齐，ROOT1.1/v2增加至12导出；SC32新ScCallbackPin使库1.1/v2增加至86导出，原85不删。SCCLIB94实体导出/19导入与11标准头已收齐，SCLIB完整容器/UTF8/path/config/stream/event/model/cache源正在收尾。SCCC共同增量包括真实Bool/参数转换/stdarg/offsetof/__func__/unsigned整数规则与私有终止机器实体；实际cpp自身输入静态260502B仅余1642B，新的CV小链32KiB计入BSS。全部新批尚未构建/运行，P0旧PASS不代表这些新源；源码收齐再统一针对std/utility和生命周期验证，不扩大旧矩阵。

2026-10-15：GUI03实际执行前轮客体根/SC32库与本轮SCCC三窗口应用，27个约定行为通过。先实测父Shell背景表+1页，再GUI暖PF6878→6878，GUI/窗口池归零；保留首次FAIL与脚本basename拒绝，进入真正SCCLIB/SCLIB实现和R1/C1..C5增量，完整目标继续。

2026-10-15：SC32 build03退出0，真实公开探针/原生动态应用/更新零洞与标准流继承通过；SCCC实际生成85导出SC32库、退出暖页8894→8894。证据见evidence/m10a2-sc32；该生成库与消息GUI下一轮实际运行，完整目标继续active。

2026-10-14：持久化真实开工源码、基线、文件归属、模型设置及未验证边界，便于会话中断后恢复。

2026-10-14：记录首次真实宿主构建、算法/格式证据和单次 Windows 双盘 SCL 引导验证；保留 6 页暖态差额和完整后续目标，不把局部通过称 Phase 1 或 M10a2 完成。

2026-10-14：SCCC原生SCL及G1执行闭环通过，保存ROOTLESS首次失败/修复、源码/证据和小范围清理，下一步启动导入；完整目标未缩减。

2026-10-14：用户建立完整持续目标，开始自动SCX启动根/导入链及下一阶段合同审计；此批自动启动新代码尚未验证，目标保持active。

2026-10-14：自动SCX/G1/两并发进程/系统根保护/正常与真实异常退出通过，记录30个定向步骤和首次故障流程修正，进入SC32API基础，完整目标继续active。

2026-10-14：保存新SC32/文件/有序GUI接线、声明式公开探针、跨工具链栈与实际宏容量修正；新批仍未验证，中断后按真实状态恢复相同文件所有权。


## 2026-10-15 标准库/通用库与受控终止实际出口

统一build06退出0、39.36秒，SC32公共库版本宏修为与清单一致的2。ROOT/SC32/SCCLIB/SCLIB客体字节57040/82014/66892/146165B均生成并安装真实执行；12/86/94/163导出完整清单匹配。当前G1 load234065/BSS3591864、SHA dadf20e7f94370f3621b2f0a2006f49127bab23cade89fc19f7bc35ff4dd57bf，实际编译/运行标准app。九种std模式、共享STD BA/position2、空pipe/管理TTY及通用七组253检查/22B文件通过；所有暖态应用退出PF差0，原盘/源码保持。compact证据evidence/m10a2-stdlib已存原native字节、报告、失败和两张实际查看的旧桌面截图。

guest01功能全过但冷PF+2025；guest02真实HMP确认旧desktop.pages1793→3818、scaled_background0→2025。guest02 shared只在Shell拒绝未支持2>&1时失败；guest03 helper误重复Close已领取票据返回84。只修主控/新helper，库和内核未为三项改行为；guest02六个成功HOST模式在guest04核相同输入后明确复用，native05仍实际全模式执行。Build/host-adapter/native生成/native执行和截图边界分别记录，不以小流程时长称整机性能。

下一批实际SCUI156入口、正式Font/Palette/PaintCancel/Popup/Cursor/Wheel以及会话Broker/Shared/DropTarget。core owner仅user/SCUI.H及user/scui/SCUIPRIVATE.H、SCUI.C/json/core八片段与SCUI.md；controls owner仅ui_controls/edit/dialog/data、SCUIDEMO及说明；broker owner仅新增broker/kernel/分页草稿与规范。root负责字体/GUI/paging/任务接线、Unicode15.1资源归档、构建和Windows定向执行。所有子代理Sol+max；当前新批不在已证SOURCE集合，尚未接线/构建/运行。完整Desktop/Shell/应用重制/SDK/整版目标继续active，不创建Release、不重启M8。


## 2026-10-15 Phase4当前接力

P3功能检查点f632708已推送；其三个失败会话仅六份build重复盘精确清理809730048B，清单随d8e6ed0推送，代码/当前产物/原盘保留。SCAPI原SHA当前再次一致。

Root已实际接入Font/Palette/Popup/Cursor/Wheel、broker Clipboard/IPC/真PF Shared、Drop7的144B真实源/目标身份票与WM最高命中；SC32 ABI1.2/v3共130导出，原86不减。task/main/session/Make/paging正文已接；后台注册与真实可见输入授权分开，源At同票MOVE/最终UP/30秒handoff，可信Connect复用原IPC事务。SCUI主体约500KiB/32include，独立完整157 API/控制族/Document/虚拟Table/菜单/Dialog正在静态收尾。它不包含SCAPI/NUI副本。

compiler代理已冻结容量四文件并转真实PLATFORMPROBE：Source1MiB、Token3MiB、Pool2MiB、SCLCode1MiB一次heap、map128KiB，旧SCX限制保留。核心SCUI owner仍独占头/manifest/核心八正文，controls owner独占controls/edit/dialog/data/DEMO。全部GPT-6.1 Sol+max；勿跨所有权修改正在写的源。

Unicode15.1完整数据及许可已登记/生成37048B真实属性资源；官方1187+1826个边界oracle已独立转换为36152/58936B。公开SCUITEXT只调Document API，预期来自官方标记而非实现。新build_m10a2_ui与独立目标/许可审计已写。

**上述整批尚未构建/客体编译/运行**；等待所有源冻结后一次统一构建、真实G1/新库/普通会话双进程平台、SCUI定向键鼠/截图/回收。完整Desktop/Shell/主要应用重制/SDK/最终验收继续active，不标完成，不创建Release、不重启M8。

修订：2026-10-15，更新实际Phase4源码接线、容量冻结/所有权/固定Unicodeoracle和未验证边界，持续完整目标。


2026-10-15真实统一构建/首轮接续：ui-build-04退出0、45.7486秒、source_modified=[]；CORE full507120/payload506992/BSS374600，SC32API138410B/130导出，SCUI328844B/157导出/137导入，DEMO36388B/119导入，PLATFORM26148B，TEXT2724B，当前SCCC0 load128937。build01新Drop排版Werror、02内部几何前置声明、03文档NULL span/时钟失败及实际未使用errno导入均保原日志，据具体诊断修复，不降警告门槛。

Windows双盘ui-platform-guest-01已真实普通su mio：父PID6/gen1、childPID5/gen2、SID4、UID1000/realmNORMAL/flags0；全部87平台检查通过，131079B文本/旧snapshot/CAS、实际IPC发送者/队列/EOF/等待、18页Shared RO/RW与issuer退出、FONT/Palette符合探针。parent实际PTE89→125→89、PT4→4，运行0.781秒/一核CPU0.671875秒。该轮整体仍FAIL：退出全br/drop/window/file/job/pipe对象/页归原，Font owned952/Desktop3818/records5页不变，PF9007→9008，唯一变化wait_pages0→1，主控尚未计旧有限等待heap首次缓存。VM已结束，未记录wait_count/capacity不能追溯把该FAIL改PASS；保持报告/原stdout/截图，下一轮核这份旧缓存真实归属与空闲状态再继续官方边界/G1/原生库与GUI。原盘及构建source/output摘要保持。

上述只是实际平台首组子结果，不是SCUI/整个M10a2通过。当前GUI主控/source独立工具仍收束，完整目标active。

2026-10-15第二轮实际接续：build05退出0、37.1932秒、source_modified=[]，CORE与04原字节相同；SCUI330264B/158入口、DEMO43332B/133实际imports。ui-platform-guest-02真实普通MIO平台全案例及完整owner页账通过：首次有限等待缓存从空变为count0/cap32/实际allocated PF一页，精确解释PF+1，unexplained=0，所有broker/drop等池原状；不追溯修改guest01的FAIL。官方1187+1826=3013边界及页账通过；实际SCCC0生成并执行G1（235570B/load235534/BSS2193080），G1生成130入口SC32API103863B，尚未安装或执行该新库。

该轮整体仍FAIL：G1编译SCUI在ui_data.c:815报告“expression needs a value”，对应当前SCCC阶段profile不支持的复合字面量。原stdout/寄存器/截图/JSON保留，source_disk_unchanged/current_inputs_unchanged均true。只将owned三片段24处表达式改为等价局部初始化/字段赋值；语义/全部入口保留，不暗扩ISO C声明。新库/GUI和原生闭环仍待验证，完整目标active。为在下一VM前定位剩余同类语法缺口，check_m10a2_ui_native复用既有真实SCCC宿主适配器，仅编译SCUI与三app，明确不执行i386或G1、不替代客体证据。

build06实际退出0/39.2765秒/source_modified=[]；24处兼容转换、正式VISIBILITY事件37与有限后台hidden样本已构建。ui-native-host-01在完整SCUI输入发现ui_data.c:1394“aggregate by-value parameters are unsupported”，未启动新VM。现在compiler owner实现真实i386结构体/联合体参数与hidden-sret返回，core owner只读审阅；scalar/旧ABI与SCAPI保持。Root新增AGGABI/Callee/Probe、两个方向真实客体调用/回调/变参/栈平衡主控，全部新源仍待统一构建与运行。随后host全UI语法预检及actual G1/GUI继续，不把源码或宿主预检称原生闭环完成。

最新实际出口：platform-guest-04完整第一轮PASS，G1实际load242538/BSS2602680，GCC/SCCC两个方向各55检查及真实页账通过；SC32/SCUI/三app全部由执行的G1生成，原生caller平台与官方3013文本实际运行通过。host05完整SCUI实际symbols7659/types2806/params3007，容量修复后六输入全通过，仍是宿主语法/生成证据。SCLMAP与结构体ABI源码未再修改；原SCX预算/SCAPI保留。

GUI01五个真实native库安装与get回、普通平台及3013文本/页账通过；整轮仍FAIL：示例Startup创建Drop端点显式ACL_SESSION但未填allowed_session，内核真实INVALID拒绝。SCUI界面尚未呈现，失败日志/报告/PNG保留，VM结束。仅定向修ui_data默认严格同会话同主体ACL及清理原cause；准备严格依赖闭包复用guest04的单库G1刷新工具，重新生成修复SCUI后继续真实GUI。后续桌面/Shell、Files2/Studio2/Debug2和其它组件/SDK/整版目标仍active，不能以平台子结果关闭M10a2。

2026-10-15开发恢复检查点：build12与refresh01实际单库G1刷新已通过，当前native SCUI656796B（5de54249…5950），五native平台/官方文本在GUI02真实通过。GUI02至04只遇宿主换行/节点kind/身份行截断解析缺陷，原FAIL与source/original守恒保留。GUI05真实普通身份、SESSIONPAGE/PROCESSPAGE及showSID通过，第一次可见图显示旧Login/ses面板：真实WM无条件截现代F12，尚未执行完整SCUI流程。下一项最小修focused-modern F12路由，旧窗口快捷键保留；此次检查点不称GUI/Phase4/整版完成。GUI驱动三个正在修改的新工具及其SCUI-DEMO文档暂不纳本次提交。

2026-10-15：恢复检查点0d78cd4已推送；F12仅现代真实聚焦窗口保完整键事件的新CORE由ui-build13成功构建，五库/App/G1原byte。GUI06首snapshot、真实checkbox与combo通过，随后F7未切页及150%布局裁剪FAIL保留；只修对应源码再统一构建刷新，不称GUI完成。SDK actual staging 79文件/10865491B与仅公开INC的GCC/SCCC宿主后端双示例编译通过，小证据m10a2-sdk-public-compile；两个新App仍未客体编译或运行，Phase7/整版目标继续。

2026-10-16：build23/refresh09实际补齐node0上下文CAPTURE/BUBBLE，并由原G1生成SCUI及Demo；GUI21真实Drop/ACK已成功但主控行首边界误超时，原失败保留。只借紧邻start前的一个换行修正主控，实际原日志回归及旧标记拒绝通过。GUI22最终DECLARED_NATIVE_FIVE_LIBRARY_ORDINARY_GUI_SECOND_ROUND_PASS，共91项，GUI_behavior_verified/source_disk_unchanged/current_inputs_unchanged均true；两主题、100/150/200%实际字宽/换行、溢出菜单、文件保存/错误、Drop提交及Complete、静止/隐藏零提交、窗口恢复、正常return与最终owner/PF归账通过。主代理已查看Classic与200%原尺寸截图。公开158入口/137导入未变，SCAPI原SHA保持。静止200PIT实际2.032秒、QEMU一核CPU0.171875秒；checkbox观察127PIT包含F12/串口，不冒充纯输入延迟。证据evidence/m10a2-p4-context-and-p5-generation/index.json，不代表整版完成。

P5隔离候选unified-host-07/08整体构建通过；原实际G1生成SC32API/SCUI/SCSHELL及worker/desktop，所有编译owner页账通过。native-issued-core-01已绑定同一119969B原生桌面（79b9c8f5804055ca4abaa007785b594663403a0c24e562a2ed77faf2a09c207d）。runtime01/02真实启动早退并有限重试回退，尚未ACTIVE；保持失败，当前只加可信host实际退出码诊断并定向重试。最新完整目标见M10A2-CURRENT-GOAL.md；P5、Files2/Studio2/Debug2、其余组件和SDK/最终验收继续。

2026-10-16实际结果：FILEJOB04通过普通UID1000的精确路径查询、131079B COPY/MOVE、持久ACK、客体逐字节和宿主取回内容对照；两份832B恢复日志的头/frame CRC与START/提交/FINISH独立审计通过。退出/注销13830→13830 PF，各池/历史/字体/未归因增量0，输入/原盘保持。证据入口evidence/m10a2-p5-file-jobs/README.md。完整P5、主要组件和SDK仍未完成，持续目标active。
