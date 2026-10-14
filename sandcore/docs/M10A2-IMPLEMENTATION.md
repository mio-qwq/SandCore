# M10a2 实现与恢复入口

正式名称：**SandCore M10a2 — The Application Platform Era**。

2026-10-14 已按用户要求开工；用户随后明确设立持续完整目标，直到完全完成M10a2，目标状态active。开工基线 `main@8e1c27abfe1b0c14ddff6d23464a961eabfd8cc0`；完整范围见根 `M10a2RULE.MD` 和 `M10A2-DESIGN.md`。最新已推送验证检查点为`72783199e3d2e987d4124a89b45913a4290937a2`，整个M10a2尚未完成，不能以阶段提交关闭目标。

## 当前状态

| 范围 | 源码/规范状态 | 真实验证状态 |
|---|---|---|
| Phase 0 总体架构 | 已记录旧加载器/SCCC/分页/安全基线、完整依赖及阶段出口，组件完整清单已写 | 源码审计，组件新运行验证待迁移阶段 |
| SCL v1 | 公共字节偏移头、freestanding C/Python 校验器、ELF32 宿主引导打包器及 dump/deps 已写 | 定向格式/宿主构建通过，真实 BASE/UPPER 各段/导入/重定位已运行 |
| SCLDR | 真正文件装载、迭代依赖/环检测、函数/数据导入、ABS/PC 回填、初始化/回滚、引用/地址固定寿命与独立三环启动根已写 | 宿主算法及真实两库/自动导入/并发进程隔离/显式卸载/正常及异常退出回收通过；进入SC32API基础 |
| 模块分页 | 独立 VA 区、0x280..282 分配/保护/释放、syscall 输出 RW、授权调试特例、退出回收接线已写 | 定向 WSL 构建与单次双盘 QEMU 保护原子性/只读写回拒绝/显式和退出回收通过 |
| SCCC 动态编译/动态 SCX 启动 | SCL与SCD1动态应用模式、自动64B CRT、SYS0x283固定根、包内导入和同一进程运行时已接通 | SCCC0及实际执行G1都生成并运行动态SCX，输出字节一致；原生根库已客体生成，尚未执行该生成文件 |
| SC32API / SCCLIB / SCLIB / SCUI | SC32首批85真实导出、typed-query、更新文件与消息GUI源已收齐；SCCLIB/SCLIB实际实现与受控终止/标准头前置增量开始 | SC32公开应用、实际客体根/SC32库、三窗口27个约定行为与精确暖态回收通过；剪贴板/IPC/共享映射等后续范围和完整四库未达成 |
| Desktop/Shell/主要应用重制 | 完整范围保留，平台成熟后按次序推进 | 尚未实施重制/验收 |
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

2026-10-15：GUI03实际执行前轮客体根/SC32库与本轮SCCC三窗口应用，27个约定行为通过。先实测父Shell背景表+1页，再GUI暖PF6878→6878，GUI/窗口池归零；保留首次FAIL与脚本basename拒绝，进入真正SCCLIB/SCLIB实现和R1/C1..C5增量，完整目标继续。

2026-10-15：SC32 build03退出0，真实公开探针/原生动态应用/更新零洞与标准流继承通过；SCCC实际生成85导出SC32库、退出暖页8894→8894。证据见evidence/m10a2-sc32；该生成库与消息GUI下一轮实际运行，完整目标继续active。

2026-10-14：持久化真实开工源码、基线、文件归属、模型设置及未验证边界，便于会话中断后恢复。

2026-10-14：记录首次真实宿主构建、算法/格式证据和单次 Windows 双盘 SCL 引导验证；保留 6 页暖态差额和完整后续目标，不把局部通过称 Phase 1 或 M10a2 完成。

2026-10-14：SCCC原生SCL及G1执行闭环通过，保存ROOTLESS首次失败/修复、源码/证据和小范围清理，下一步启动导入；完整目标未缩减。

2026-10-14：用户建立完整持续目标，开始自动SCX启动根/导入链及下一阶段合同审计；此批自动启动新代码尚未验证，目标保持active。

2026-10-14：自动SCX/G1/两并发进程/系统根保护/正常与真实异常退出通过，记录30个定向步骤和首次故障流程修正，进入SC32API基础，完整目标继续active。

2026-10-14：保存新SC32/文件/有序GUI接线、声明式公开探针、跨工具链栈与实际宏容量修正；新批仍未验证，中断后按真实状态恢复相同文件所有权。
