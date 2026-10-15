# Studio 2 与 Debug 2 原生开发工作流设计

所属版本：**SandCore M10a2 — The Application Platform Era**。2026-10-15。范围来自根 `M10a2RULE.MD` §7.2/7.3及平台、兼容、SDK和性能合同。本文是完整源码审计后的**设计稿**；Studio2/Debug2尚未实现，没有新增构建、测试或QEMU。只新增本文件，不修改现Studio、Debugger、SC32、ROOT、工具或其它规范。

## 1. 审计基线与准确状态

本次完整读取 `user/ide.c`、`user/debugger.c`、[STUDIO.md](STUDIO.md)、[DEBUGGER.md](DEBUGGER.md)，并核对 `SC32API.H`、`SC32GUI.H`、`SCUI.H`、`runtime/SCLDR.H`、原DEBUG/DBGSPAWN2实现、CLI编译/调试入口及C/SCL/SCD规范。历史规范中的阶段结果只对应其精确源/产物，不能把旧草稿PASS改称重制完成。

| 基线 | 本次真实观察 | 边界 |
|---|---|---|
| `ide.c`，20892B | SHA `440b533360316175c227a1788355cd943d523cc54657c27ce951b7211a1a43ba`；单文档、NUI、原生SCCC/SandAsm子进程 | 没有多文件工程、共享Document、Undo/Redo、查找替换、语法高亮或结构化错误导航 |
| `debugger.c`，24897B | SHA `8d00db75a3a3bc898e346314f4e39df836f4d905afe7fffaab0497d66d50f963`；真实owned目标、TF/INT3、上下文/内存/暂停栈 | 没有typed Debug会话、target模块枚举/源行表或动态Breakpoint模型 |
| SC32 ABI1.2/v3 | 有真实进程PID/gen、受管job、文件、GUI、Broker/Drop与本人library统计/Resolve | 没有DebugStart/目标Snapshot/模块表；没有typed pipe创建或以ScFile端点启动child |
| ROOT公开SCLDR | ScApplicationRuntime和本进程Open/Resolve/Query/Close；地址逃逸固定模块寿命 | 统计不是模块枚举；Resolve不是他人进程符号查询；RuntimePlatform是引导适配接口 |
| Phase4 | build04与首轮真实普通87平台功能检查已有单列证据 | guest01整体因旧wait-cache缺计账仍FAIL；完整SCUI/native/G1/GUI按主代理新报告继续，不能称成熟完成 |
| 新aggregate/SCLMAP批 | by-value/sret与函数RVA修复已冻结；HOST03在ui_edit1531触Symbol6144后仅扩16384；build10 source_modified=[] | HOST05六输入生成exit0/13.654秒/changed=[]；guest03已启动G1/55check交叉/native SCUI，尚未收终态，不预写执行PASS |

聚合ABI和容量的源码事实见 [SCCC-AGGREGATE-ABI.md](SCCC-AGGREGATE-ABI.md)、[SCCC-CAPACITY.md](SCCC-CAPACITY.md)。本设计不顺手扩语言/后端；具体新诊断由主代理按实际输入处理。SCSHELL仍处设计阶段，依赖及文件条件事务见 [SCSHELL-DESIGN.md](SCSHELL-DESIGN.md)。

HOST05完整SCUI的真实后端state为types2806/symbols7659/params3007/fixes4491，生成656592B SCL；Demo76096B、Platform63265B、Text5242B、AGGLib1947B、Probe5948B也均为HOST真实emitter产物。Symbol16384有此实际规模依据，TYPE4096/PARAM4096/FIX12288仍有限profile。生成字节不等于这些文件已在客体执行，HOST13.654秒也不是guest编译或GUI性能结论。

## 2. 原有效功能完整保留

### 2.1 Studio

| 原入口/行为 | 重制必须保留 | 新增工程能力 |
|---|---|---|
| Open/New/Save，F1/F3/F2 | 完整候选路径、取消/坏文件/短读/保存失败保原文档与脏标记；CORE保护由内核决定 | 多文档/项目树、Save As/Save All、外部修改冲突 |
| 任意位置编辑 | UTF-8整标量、ASCII输入、Tab/Enter/方向、鼠标定位、F8文首/F9文尾 | SCUI Document共享编辑、选择/剪贴板、Undo/Redo、Find/Replace与Unicode边界 |
| 行索引/滚动 | 末尾空行、可见行直接索引、手动滚动不被旧光标拉回、横向宽字裁剪 | 真实metrics/虚拟视口、多视图独立selection/scroll、增量语法高亮 |
| Source/诊断切换，F4 | 保原文档、光标、诊断原字节和独立滚动 | 常驻Output/Errors面板，实际文件/行/列/byte offset导航 |
| Build，F5 | 先成功保存；真正SCCC/SandAsm生成；错误不执行残留旧SCX | 不可变Build输入清单、G1来源、多目标/依赖、SCX/SCL与SDK工程 |
| Run/Debug，F6/F7 | 只有本次真实成功且文档版本一致才启用；原C/ASM执行/首指令调试 | typed job与owned Debug模型、模块/符号/源行、运行配置 |
| 响应工具条/菜单 | 窄窗口/高缩放重排、极小窗口Enlarge、外点关闭不穿透、排队首字 | 标准Menu/Command/Toolbar、可停靠/浮动面板与布局持久化 |

旧65535B是单文档实现容量，不作为新Document/项目的固定门槛。新文档可按实际内存/FS/编辑器budget扩展，失败明确并保旧模型；编译仍按真正SCCC当前输出/工作区限制。旧F1..F9命令入口保留并配标准菜单/快捷键，不让重制丢掉原操作。

### 2.2 Debugger

| 原入口/行为 | 必须保留的事实 |
|---|---|
| DBGEXEC首指令暂停 | 目标真实由调试器创建、绑定owner，不能以普通PID查询冒充attach |
| F8 Step、F5 Continue、F4 Pause | 真CPU TF/陷入保存的现场与state；Step未完成不能展示新暂停快照 |
| F2 Restart、Esc Close | 先结束并回收旧独占目标，再启动新目标；普通Run程序寿命不被误杀 |
| B断点/U撤断点 | 真0xCC、原字节、EIP回退、TF执行后重插；撤失败不假称成功 |
| M/I与地址导航 | 完整1..8位hex候选、0x可选、非法/越界/未映射明确失败，取消不改视图 |
| Registers、R | 目标EAX..EIP/FLAGS真实暂停副本；运行时标LAST PAUSE，不用本调试器寄存器 |
| Stack、S | ESP向高地址、逐4B映射核验、未知槽`????????`；同一stop副本，翻页不再读目标 |
| 反汇编 | 字节来自真正目标；未知/短/未支持编码显示真实`db XX`并推进1字节 |
| 响应布局与节流 | 宽Code/Memory+Registers/Stack、窄页切换、极小Enlarge、菜单不穿透、输入即时刷新 |
| CLI scdbg补充工作流 | 真实job代数、stdin pending/EOF/背压、stdout/stderr、where/map、step count及串口开发；不倒退成仅GUI标签 |

旧DEBUG88B和SCAPI原语义保留；新的typed会话不要求历史SCX重编译。M10a2的多断点管理不能沿用全局固定8槽：旧接口原8槽行为可保留，新owned模型必须动态。

## 3. 已指出的真实缺口

| 源路径 | 具体问题 | 所需处理 |
|---|---|---|
| ide.build/check_compiler，debugger.start/refresh | `cpu[16+PID*6+2]`读64字CPUINFO；旧CPUINFO只有8行，动态PID≥8越界 | 新工具完全用ScJobInfo/ScProcessRef与typed Debug原子返回身份；不把CPUINFO扩写或复制旧推算 |
| ide编译资格 | 已有document_revision/build_output有效保护，却只覆盖单一编辑缓冲 | 项目输入包括所有TU/headers/manifest/config/toolchain的snapshot；dirty清零不足以代表同一次输入 |
| debugger.symbol_at/draw | 只读16383B普通map、按最近地址取label；function_label没有进入当前draw | 真模块归属/完整符号范围/源码导航；不能宣称旧GUI已有这套流程 |
| ROOT/SC32库查询 | ScRuntimeQuery/ScLibraryQuery只统计，ScGetSymbol只查本人明确库且固定provider | 新self模块公开值接口与kernel-authorized target模块快照，不能解foreign ScRuntime或SCLPRIVATE |
| Compiler map | 旧SCL header写RVA而函数行仍BASE+code_used，typed EXPORT才是真RVA | 此批已统一shared函数行减BASE，旧SCX/SCD绝对保持；仍待实际验证，仍无statement行表/image绑定 |
| Compiler CLI | 单入口TU+include聚合，至多source/output/模式/manifest；无-c、多TU object/link、-I/-D接口 | 项目必须诚实区分现聚合工程与真正独立TU；完整目标仍需原生对象/链接和typed配置 |
| 编译/调试输出 | ScProcessSpawn接受旧i32 streams；ScFile opaque ID不是旧fd | typed pipe端点/ScFile child绑定，不用Broker packet冒充stdin、不为modern alias逐个占旧16fd |
| GUI/Debug等待 | 当前SCUI泵原子等INPUT/STREAM；Debug变化尚无typed生产/等待 | 真owned debug event/sequence与GUI/pipe联合原子等待，不永久5tick轮询或虚构消息 |
| SCSHELL | 新关联/打开方式、标准操作、文件条件publish/lease、session-lifetime Run仍未实现 | 按Phase5新合同接入，不在应用内写SCAPI桥/系统提权proxy |

现 `kernel/process.c` 已有真实debug_owner_generation、owned child链、TF/INT3、module unmap撤旧断点；`paging_debug_patch`已拒Shared页。它们是可复用的内核事实，不是新SDK的公开私有struct。不能给第三方头导出process_t、task_t、保存frame指针、PD/PF、canvas或ROOT内部Module对象。

## 4. Ring3应用架构与依赖

新正式程序为 `STUDIO2.SCX`、`DEBUG2.SCX`，独立源码/manifest，均用SC32API/SCCLIB/SCLIB/SCUI；Shell行为只从SCSHELL公共入口取得。系统库按保护目录解析，应用不复制NUI、SCUI/ROOT正文或SCAPI/中断桥。新示例默认使用标准头与signed int main(void)、自动SCD启动。

共同应用层放在拟新 `user/devtools/`：project/document registry、build controller、诊断/符号解析、debug controller与i386 decoder。它们是本项目自己的共享职责源码，不是复制一个应用的私有控件；控件仍是真SCUI。初版可由两个app聚合同一私有DEVTOOLS.H/正文；若实际code超过旧SCX载荷预算，由主代理拆真正独立SCL并冻结其公开ABI/系统名称，不靠复制库正文或扩大旧Stage。

Studio集成debug时，controller与DebugSession在Studio同进程，直接复用同模型/面板；独立Debug2则拥有自己启动的目标。跨进程“打开Debug2”不能把Studio的session数值转交就变成另一个owner。需要独立只读观察窗时用显式授权IPC的值快照，所有control仍回原owned controller；正式owner移交若以后需要必须有新的内核原子合同，当前不承诺attach/任意转移。

所有长期UI callback/provider/timer先ScCallbackPin，候选失败不留引用；Document/ScString/Model/Menu/Icon在本人引用生命周期内。显式Close先停止jobs/debug、撤订阅和UI，再Release；fini quiet不再进入已经结束的应用callback。异常退出依kernel owner/paging回收，不承诺用户析构执行。

## 5. 多文件项目与共享编辑

### 5.1 项目模型

工程descriptor采用版本化、严格解析的普通用户配置文件，先完整候选再提交。包含project ID/name、绝对project/work/package root、源/头/资源/SDK文档项、targets、依赖DAG、toolchain profile、run/debug配置及布局。稳定item ID与显示排序分开，动态容器/索引，只受真实资源预算限制，无固定文件/目标/面板数量。

目标类型至少为旧SCX兼容、自动动态SCX、SCL、独立ASM；可选SKM保留现开发能力但Run SKM继续原真实外部授权链，Studio本身不取得Ring0。SCX+SCL的name/ABI/export/import/dependency/init/fini是严格manifest模型，参数个数/签名/引用要求由真实工具链校验。项目依赖环、重复输出、输入与副产物别名、无权路径先拒绝，不在首次失败后留下半个项目注册。

源树以实际直属目录页建立节点，lazy展开、过滤/查找、Open With/Properties/Rename来自SCSHELL。path保留完整UTF-8/ASCII大小写身份，63B FS上限仍真实；label省略不改变打开/编译对象。删除项目引用与删除磁盘文件分开；source文件新建/改名/移动先真正提交，成功后更新树/文档身份，取消或错误保原。

### 5.2 Document registry

每个规范文件身份在同Studio实例只拥有一个ScUiDocument，多个tab/window/edit view Retain同Document，共享正文、revision、Undo历史；各view有自己的selection、scroll、wrap和focus。文件稳定观察用真实open-pin/revision，不能把它当跨重启birth ID。磁盘外部修改以重新stat/read与内容stamp识别；有未存编辑时给Reload/保留/另存，不能用同名路径静默覆盖。

所有Open先READ完整候选/UTF-8/二进制/容量验证，再一起提交Doc/path/selection/saved stamp。Save使用Phase5 conditional publish与expected disk identity/revision，失败不MarkSaved；保存成功后若用户又编辑，只将被保存的revision记为saved，当前仍dirty。Save All逐文档报告结果，不宣称多文件原子；其全部成功且输入snapshot匹配才可Build。

编辑直接使用ScUiEditBind与Document Replace/Undo/Redo/Find/ReplaceAll/Boundary，保CRLF/LF/Tab原字节。默认Tab显示宽度可配置，旧四空格插入作为明确选项；F8/F9原文首尾保留，选择和整Unicode grapheme/word导航依实际UAX能力。输入、hit、cursor均用真实Phoenix metrics，不再把每个非ASCII固定算两格。

Find/Replace提供当前文档/选区/所有打开文档/项目范围、字面及明确匹配profile、下一个/上一个、结果列表、整批Undo。当前公共Find只承诺字面/已声明ASCII_CASELESS，不能冒称完整Unicode大小写或regex；完整工程检索按有界文件批次，失败保旧结果/原文档，不在UI线程读整个卷。

语法高亮是自己的增量词法状态（C/ASM/manifest/config），按行记录comment/string/preprocessor状态与doc revision，用ScUiEditSetSpans；从修改处到状态收敛重算，不每次键入全文件重新parse。它不复制SCCC私有Tok/Type/Node或声称等同编译诊断。宏未展开、条件分支、未知语法可以明确中性色，最终真实C错误仍由编译器报告。

## 6. 真正的原生Build与发布

### 6.1 Build输入与身份

Build记录双字job ID、tool路径/字节摘要/代数、target与所有源/header/manifest/resource/config的immutable snapshot，含每项内容stamp、字节数和Doc revision。输入在发起者凭据下resolve一次；worker接绝对规范值，不把ROOTLESS再次套cwd。未保存文件先Save/Cancel或显式使用工作区snapshot，不能暗中编译未保存buffer却把产物标为磁盘版本。

Build workroot必须显式获准、可写、长度足够，用exclusive候选/本人job目录与确定basename；检查output/log/map/SDB不能覆盖任何输入、SDK或其它job。长路径明确选择其它workroot或失败，不再静默落共享HOME/BUILD.SCX。初始完整快照需要Phase5 stable lease；未有此能力时只读“前后revision看起来相同”不能证明包含过程中无混合正文。

snapshot按实际变更内容建立；正式不可变SDK/已有source stamp可引用或复用拥有的内容存储，不每个Build复制整个SYS/SRC/SDK或数据盘。SandFS没有hardlink则不假设硬链接去重；只有符合权限/版本/lease的内容才复用，释放引用不删除用户原文，GC归本人store链并有限预算。

编译器是一个真实被执行的SCX。先保存本机宿主bootstrap SCCC0来源；客体从完整当前SYS/SRC生成G1，提取并校验真正G1字节/旧SCX载荷/BSS/stack，然后以那个G1构建应用和SCL。项目tool profile必须记录具体路径、SHA、cap与generation；日志写G1标签或仅生成G1文件不算使用它编译。M7/M9旧固定点不冒充新工具链输入通过。

实际模式仍分别为：

```text
sccc entry.c NAME.SCX
sccc entry.c NAME.SCX --dynamic app.json
sccc entry.c NAME.SCL --scl library.json
sandasm entry.asm NAME.SCX --ide
```

库九字段与动态app四字段不同；SCL没有main/EXIT，app的main为int(void)。依赖库正文不静态复制到app，未使用manifest import必须由真实诊断处理。SCL运行须有显式consumer/runner app，不能把“Run .SCL”伪装成EXEC一份非SCX。

### 6.2 多TU的完整目标与当前缺口

现SCCC是**一个入口翻译单元**，可显式include多个自写片段；SCUI/SCLIB等aggregate正使用这个模型。Studio可正确管理/编辑这种多文件工程，但不能将生成include-all.c称为拥有独立TU static作用域、分别编译和原生链接。两个源都有同名static时自动拼成一个TU会冲突，不能改名字掩盖。

完整独立TU需后续获准的原生object输出+linker：建议沿仓库已有严格ELF32/i386 ET_REL子集输出真实relocatable对象，由新的**客体Ring3 linker**解析符号/段/ABS32/PC32，完成内部连接后生成原SCX/SCD/SCL。不得在客体工作流中偷调用宿主GCC/ld或用Python产物冒充。保留每TU局部/static身份、外部definition/prototype、未定义符号及全局对象尺寸；多个TU duplicate/unresolved/不兼容签名/容量先诊断，外部真正SCL imports仍按manifest。

ELF符号本身不能证明C函数原型，因此对象还需版本化的类型/调试信息，使用canonical公开值表示而非compiler内部Type数组；布局/签名检查由compiler/linker同批冻结。GOT/PLT/TLS/COMMON/weak/FP/64等超当前子集明确拒绝，不能借多TU宣称全ELF/C支持。SCL/SCD格式与旧SCX装载限制保持，SDK/manifest/native library构建路径真正共享同一后端。

这一项是**未实现的语言工具链依赖**，本设计不改s3c或写linker。源码收齐后只由实际profile/诊断推进，不为设计提前扩AST或删应用功能。

### 6.3 输出管道、状态与发布

Build controller状态为PREPARING→STARTING→RUNNING→CHILD_EXITED→DRAINING_OUTPUT→VALIDATING→PUBLISHING→SUCCEEDED/FAILED/CANCELLED/UNKNOWN。child退出不等于其stdout/stderr已读完；两个真实pipe分别有公平预算，EOF以最后真实writer关闭为准，AGAIN不当EOF，暂停目标输入满时UI仍可Continue/Cancel。

ScJobInfo直接提供child PID/gen及真实退出码，不采旧CPUINFO、不用STATUS数值猜另一个task。只有exit0、输出真实格式/manifest/identity验证成功、publish确定且当前项目输入revision/stamp仍匹配，才给“Run current build”资格；编辑又Save导致dirty0也不能偷认上一Build。旧好产物保留为明确Last successful build，失败后不会被自动执行为本次结果。

编译器目前map/log与正文不是跨文件原子事务。Studio从独立workdir取得新候选，全部核对后以版本化artifact bundle/条件publish发布可运行入口，避免“map新、正文旧”混配；发布失败明确保原成功bundle或UNKNOWN，不猜成已回滚。Cancel在工具真实安全点/owned job取消执行，已提交文件效果不假撤销。

发布工程导出包含真正native SCX/SCL、严格manifest、公开头、文档、debug sidecar、资源及必要许可证/source出处。系统库更新不能经普通用户写SYS/LIB；普通包只放获准用户目录，系统名始终从受保护目录解析。导出不读取用户签名私钥，也不把SCL变SKM权限。

## 7. 结构化诊断与源码导航

现SCCC fatal已有真实`SCCC ERROR path:line: reason`、512B消息和输出.log；map_function只写函数名与定义源行，位置打包采用file/16bit line。GUI目前最多4095B日志，不是完整Errors数据库。新Output保存真实stdout/stderr/log原字节与来源，长输出用受控滚动存储/明确budget，不在显示前截掉后面的诊断。

拟新增compiler诊断协议：version、build/request ID、stage/severity/code、source ID/path、u32 line/column/byte offset、span、message/cause、include/macro origin。跨process用pointer-free counted记录/chunk，输出EOF/失败/取消与最终result分明；既有文本/日志兼容保留，旧text可按其准确语法解析，但不存在的column/byte offset显示UNKNOWN，不伪造。

源码导航绑定**产生诊断的输入snapshot**。当前文档已改时打开Build source或显示差异，不无条件按旧line移动到不同代码。行/字节offset/视图像素各自分明，UTF-8不能在续字节放caret；诊断双击和Next/Previous error共享命令。SCCLIB/SDK头错误可打开只读正式头，不因文件位于SYS/INC而禁止查看。

超过65535行的定位不能继续打包后误当别的file ID。未来structured source-position需要独立u32位置索引或先明确诊断上限；若扩大Tok/位置表，必须据实际Source/heap/BSS/self-build成本验证，不能在本设计顺手改变当前已冻结Node/Tok。

项目错误面板用动态provider/table；按job/target/source分组，Build完成不抢走正在编辑的focus。Result已提交而observer通知失败只记录notification_error，不把磁盘编译结果回滚成未发生。

## 8. 所需typed Debug公共合同

以下全部是**拟新增公开合同，尚无SC32DEBUG.H/selector/cap/export**。名称/精确layout由主代理在实现前统一冻结；不把下面字段抄成当前已授权SDK结构。所有cdecl记录size/version，输入完整RO、输出完整声明RW先验；成功只写已知前缀，错误不改tail。句柄核资源kind、owner PID/gen及captured target PID/gen，数值不是权限。

### 8.1 会话与快照

拟 `ScDebugStart` 以版本化desc接受完整command、真正ScFile std端点、flags和caller结果，执行与owned bind在同一内核事务中，目标在首用户指令前暂停。任何参数/out/OOM/bind失败必须回收候选task/端点/会话，不返回已经运行的假调试目标。默认继承caller真实凭据/环境/CWD/SID，不能由desc改UID/realm或任意attach。

配套拟 `ScFileCreatePipe(desc, read_file, write_file)` 与 `ScProcessSpawnFiles(desc, job_info)`：pipe desc给version/真实queue budget/byte-stream profile/flags；输出两个不同的本人ScFile能力，receiver空队列AGAIN、最后writer关闭EOF。SpawnFiles接受拥有文件句柄的stdin/out/err和明确inherit/null策略，整区预验后在kernel内为child标准端点增加真实引用，返回captured child PID/gen。opaque句柄与旧fd域不混用，重复/默认std继承沿现弱proxy/真实refs，现代alias不逐个占旧16fd。来源Close不复活该PID已关闭std，child仍活合法引用也不会使父永久保活writer。现ScProcessSpawn原i32 streams ABI保持，新增API只在真实pipe/bind/cleanup接线后置cap。

拟64B `ScDebugSessionInfo` 的16个word：`size,version,session_handle,owner_pid,owner_generation,target_pid,target_generation,SID,state,stop_reason,stop_sequence_lo,stop_sequence_hi,module_epoch,exit_code,flags,reserved`。state区分STARTING/RUNNING/STOPPED/EXITING/EXITED/CLOSING，stop_reason区分START/STEP/BREAK/PAUSE/EXCEPTION/MODULE，exit_code有valid flag。它是值快照，不导出process_t或旧rawframe索引。

拟128B `ScDebugSnapshot`：12word共同事实（size/version/session/target PID/gen/双字sequence/state/reason/vector/error/fault_address）、16个命名寄存器word（EAX/ECX/EDX/EBX/ESP/EBP/ESI/EDI/EIP/EFLAGS/CS/SS/DS/ES/FS/GS）以及module_epoch/flags/reserved2。只返回真实CPL3保存上下文；目标尚在kernel continuation时defer/明确BUSY，不泄Ring0 EIP/栈或拼虚拟全零frame。Step请求返回accepted状态，必须等新stop sequence才认为完成。

拟 `ScDebugQuery/Snapshot/Control/Wait/Close` 均以owned session工作。Snapshot带expected stop sequence，不一致STALE；running期间只能请求明确LAST_STOP副本，不能当实时寄存器。正常exit保最后真实stop/exit事实在session对象内，task/PID可回收却不会由新slot填入旧面板。Close默认终止本会话独占目标并分步清理；Restart先收旧，再建新gen，普通Run程序不受影响。

调试器UI与这些控制共享真实状态机，暂停/继续/Step/Restart按钮根据query启用；失败保原BP/视图。普通/root/SYSTEM的进程管理资格不能替代debug owner。Ring0/COM1调试仍只属于外部硬件管理链，新Debug2不连接客体UART、不读kernel内存。

### 8.2 内存、断点与事件

| 拟公开族 | 必要真实语义 |
|---|---|
| `ScDebugRead/EnumerateRegions` | 读取目标用户映射，完整range/加法/targetgen/输出先验；VA、长度、R/W事实/declared X、mapping kind/gen是值，不返回PF/PD；单批有界 |
| `ScDebugBreakpointAdd/Remove/Query/Page` | 动态BP ID；绑定session/targetgen与module mappinggen；实际byte/INT3/原值/TF重插；add/remove失败不假成功 |
| `ScDebugControl` | 真Step/Continue/Pause/Terminate；默认保现异常callback顺序，first-chance若后续提供须独立新flags合同 |
| `ScDebugWait` | 在同一IF0操作核last stop/module/event事实并装等待，组合GUI INPUT/pipe STREAM/deadline；即时ready消费本次实际事实，不持续poll |
| `ScDebugSessionClose` | 先撤BP/owner/事件/端点引用，再目标清理/PTE释放；全部按本人链预算，不能扫描所有task或固定session数组 |

新Read可比旧256B分批窗口大，但每批应有明确上限，失败不填零；多批只在同expected stop取得的私有数据才称同一stop。Shared即使目标暂停也可能被peer改，显示LIVE_SHARED/非全局冻结事实。只读页软件断点是现明确调试特例，必须拒Shared或其它进程物理页，不能复用成任意memory write服务。

断点有逻辑项与实际installed实例：用户file/function/module+RVA/启用设置可保留，模块未加载时为PENDING；load后真实绑定，unmap立即撤installed实例，same VA重用不能恢复旧original byte。当前process_unmap已有这个基础，现代dynamic列表也必须接同mapping生命周期。

SCL函数按声明X段检查；旧SCX没有完整硬件分段/NX证据，raw地址模式如保留必须显示真实范围/资格，不宣称数据页硬件不可执行。目标原生0xCC不能被当作自己可恢复的断点；Continue命中路径仍执行原指令再重插。旧DEBUG8槽语义保持，新API不能以同一个8槽数组冒充动态管理。

## 9. 目标模块、符号与真实地址

### 9.1 本进程runtime公开值与target授权查询

ScApplicationRuntime是**本人**opaque runtime；Debugger不能把target中的地址转换为这个类型，更不能调用ScRuntimeOpen/Resolve去执行target loader或其init。只在Debugger自己加载同名SCL得到的是另一个实例/BSS/base，不是目标模块。

拟ROOT自有公开值接口 `ScRuntimeModulePage/Query/Segments/SymbolPage/Imports/Dependencies`，复制当前本实例的值：runtime/module ID、mapping generation、name/path、ABI/version、真实base/allocation/image bytes、state、segment RVA/bytes/access、export name/kind/RVA/size、import所属dependency及实际resolved target、ref/pin/init状态。不返回内部链表、文件buffer或函数指针作为“模块info”。Query不得调用用户callback或重入load/init；正在事务时返回明确状态/一致snapshot。

要在目标停住时查询，由ROOT装载过程向新增kernel模块观察登记发布**已复制值**；kernel核真实caller PID/gen、本人实际mapping/范围/代数、SCL严格表与现保护事实，存动态记录/epoch，回滚/unmap/exit撤销。target module query仅由真正owned DebugSession获准，不能经普通process query读别人数据。普通kernel mapping未登记时显示UNREGISTERED，不能猜成SCL。

目标自报name/symbol只作诊断资料，不能授SYSTEM/文件/Debug权限；kernel信任身份/映射/保护，不因一个label就是ROOT而给角色。格式/table长度/offset/multiply/UTF-8完整检查与拷贝，metadata不得指向foreign页；后台推进不保用户指针或借回收后的CR3。大表注册/撤销/分页预算有界，PID复用、最后PD释放顺序由主代理统一。

自动CRT首指令仍在0x400000；真实顺序为CRT→SYS根装载→ROOT→dependency mapping/relocation/init→APP main→prepare/fini。模块state区分MAPPED/RELOCATED/INITIALIZING/READY/FINALIZING/UNLOADING/FAILED，不把失败init标READY。ROOT登记点应覆盖bootstrap根、成功模块和暂时事务模块；可选stop-on-module事件必须有真实生产点，不能从UI计时猜库已经加载。

### 9.2 地址与符号

1. 先按真实module base+image/segment范围定位EIP/read地址；mapping ID/gen区分同VA不同寿命。
2. SCL地址为`base+rva`，RVA0合法；当前本批已修SCL函数map的旧BASE混合，修复未验时不能据header外推成功。
3. 普通SCX/SCD旧map函数列是绝对VA，新增SDB统一用module RVA。IMPORT/REQUIRE数列仍为index，绝不可当地址。
4. 有匹配private symbol/行表时查精确range；无侧车时仅以SCL实际export的RVA/size显示真实`name+offset`。没符号显示hex，不把“最近低地址”扩成整个后续data/BSS。
5. import面板展示声明依赖/version/minsize、实际resolved module/symbol/VA；loader未完成或失败显示原错误，不自己Load同名库补数。

source/map身份必须绑定实际已加载映像原字节及toolchain/build ID。磁盘后来换同名文件不代表target用了它；module卸载/重新装载后更新gen，旧侧车/地址/BP全部失效或转PENDING。只读符号查询不得为了显示名字Pin target模块或阻止正常卸载；对象引用只保证短期snapshot复制安全。

### 9.3 拟原生debug sidecar

当前函数定义行不支持语句级Step Source/行断点。完整流程需compiler/原生linker提供optional `.SDB`，而非从反汇编猜行号。拟v1 `SDB1MIO\0`、64B LE头，其后32B image_id，再sources/symbols/lines/strings规范连续表；**这是待冻结格式草案，无writer/reader实现**。

头在8..60为14个u32：version/header_size/file_bytes/image_kind/image_bytes/image_id_offset/sources_off/source_count/symbols_off/symbol_count/lines_off/line_count/strings_off/strings_bytes。image_id绑定原SCX/SCL容器字节摘要，不是含重定位/0xCC/可写data变化的运行内存hash。

source row48B：path_offset、source_bytes、SHA256八word、encoding、flags；symbol row32B：name_offset、RVA、bytes、kind、source_index、source_byte_offset、flags、reserved；line row32B：RVA、extent、source_index、source_byte_offset、line、column、flags、reserved。字符串为完整NUL起点，source-index可用显式unknown sentinel，乘加/长度/排序/重叠/映像段范围严格拒错。地址域不包含PF或kernel指针。

native compiler记录真正机器指令对应语句/源位置，linker重排后回填真实RVA；初始化/宏展开等多对一行允许真实range，不要求“一行一条指令”。Sidecar输入源hash能打开当时snapshot，当前文本已变则标差异。源行/u32位置宽度与frame/unwind元数据增量必须按实际compiler容量处理；没有匹配行表时只提供CPU Step/函数入口BP，不把它称完整源码级调试。

SOURCE_ONLY下不引入DWARF/第三方decoder，不复制其实现或改变许可。HOST/GCC侧车若后续提供，来源与映像验证单列；它不能冒充由客体G1产生的侧车。

## 10. Debug2模型、面板与生命周期

模型统一维护session状态/target identity、stop snapshot、modules与epoch、BP逻辑/实际实例、code/memory/stack views、stdout/stderr/stdin队列及源码绑定。所有面板消费同一stop sequence；一次Pause成功只是请求事实，须Snapshot成功后才换整批显示。RUNNING仍可显示明确LAST_STOP，不能刷新某个panel后让同界面另一panel用别的stop。

反汇编解码真实读取字节，按bounds检查prefix/ModR/M/SIB/immediate，扩大到当前SCCC/GCC实际指令族与明确SSE2原字节显示；未知/短/未实现仍db XX、长度1。CPU TF不依赖decoder。Code view的地址/原bytes/instruction/symbol/source/当前EIP/BP是并列列，branch/call导航仍核真实range，读取失败不失去返回入口。

Memory视图hex+ASCII、分组/页/地址输入，invalid槽以unknown和reason显示，不补零；raw4B/字符等解释是显式显示profile，不能自行把任意字节当合法C变量。当前没有变量location/type/unwind完整元数据，不把几个寄存器旁标签称Locals/Watch evaluator；如果要完整变量观察，后续必须正式记录scope/type/location及安全取值，表达式不在target任意执行。

Stack先提供原ESP向上逐槽读取和模块/symbol解释；真正Call Stack只在debug profile有可验证frame-pointer/unwind规则时建立，逐frame验证range、单调/边界与循环，无法继续时明确unknown。现后端有栈/寄存器优化，不能仅按任意EBP链猜全部callee或通过kernel恒等页读取。多panel同步改变地址但不修改真实CPU，Register写或任意memory写不在原合同内，不能假声明入口。

Breakpoint管理支持新增/删除/启禁、地址/模块/函数/源位置、hit count、installed/pending/error、查询真实原字节状态；来源/目标gen变化不静默重绑。Step/Continue/Pause/Restart与菜单/工具栏/快捷键同一Command，保持F2/F4/F5/F8、B/U/M/I/R/S与原方向地址操作。新源码Step/Step Over/Out只有真实行/call/unwind支持才能启用；隐藏时仍处理debug/stdio，停止绘图timer和提交。

target stdin为自己的真实pipe：暂停时不能消费，满队列明确AGAIN且允许用户Continue/清候选/Cancel，EOF在接受的pending全部写出后才close最后writer。stdout/stderr分栏/合并视图均保真实顺序标签与字节，末尾无LF照显示。Debugger关闭退出先终止owned目标/撤BP/关真实端点，再释放session/模型/窗口；target异常/自然退出亦留准确exit事实，Kernel清理不得保活一个永久stdout writer。

## 11. Studio集成、SCSHELL与SDK

Studio目标配置支持Run arguments/CWD/environment、stdin/stdout profile、package root、break-on-entry、源码/debug bundle。普通Run采用Phase5 session-lifetime launch，Studio退出不自动杀正常独立GUI；Build/Debug则是明确owned作业，退出要提示仍活任务并按选择真正Stop/Wait，不把关闭panel当终止证明。

Studio File/Open With/Properties/标准FileDialog、project文件创建/改名/发布、应用注册/图标、错误确认通过SCSHELL；它当前还没有实现。新增typed Debug与模块能力属于SC32/ROOT，不能让SCSHELL服务以自己的PID替任意caller调试或提升文件权限。不同SID和SYSTEM边界保留，只有真实授权端点的同SID开发协调IPC可互传值。

SDK浏览器以实际只读目录树和文档索引工作，能打开正式SC32API/SCUI/SCLIB/SCSHELL头、11个标准小写头、SCL/SCD/ABI/例子、许可证/source出处，并从源码错误跳到相应头。默认template覆盖native Window/Menu/Toolbar/Document、动态SCX、SCL导出/依赖及consumer；没有SCAPI或内联bridge要求。

manifest编辑器给真正library ABI/export/import/kind/minimum-size/init/fini模型，并验证实际编译结果。只选一项“依赖SCUI”不自动把整个头所有prototype当import；当前SCCC严格拒未引用manifest import。若需要自动导入清单，可增加明确前端scan/typed build协议，从真实未定义引用生成候选再由用户确认，不能靠UI猜名字或加载库执行init探接口。

正式INC只安装公开大写头与11标准小写头；私有WIRE、SCLPRIVATE、SCUIPRIVATE、compiler片段、kernel头只进SRC/开发源归档，不作为第三方API。新typed debug/pipe/模块头必须有真实桥/实现/版本再公布cap；文档方案不等于可链接出口。

## 12. 布局、性能与建议ownership

Classic与Aurora共享SCUI行为/metrics，命名颜色沿GFX/THEME，原Phoenix字体。以96基准Menu24/Toolbar28起点和真实字高padding验算；不是把旧窗口截图放大。

| 窗口 | 默认组织 | 窄窗/高缩放 |
|---|---|---|
| Studio2 | 上Menu/Toolbar，左Project/Files，中央Tabs+Edit，下Output/Errors，右可选Debug/SDK，底Status | 左右dock可折/浮动；中央Edit先保可用，Output按Tab切，不挤掉所有正文行 |
| Debug2 | 左/中Code+Source/Memory切换，右Registers，上下Stack/Modules，底BP/TargetIO与Status | 所有pane能Tab切和键盘访问，仍显示stop性质/targetgen；极小Enlarge保模型 |
| 浮动/停靠 | SCUI DockMove/Float、Splitter、LayoutSave/Restore；panel ID与数据模型独立 | saved几何clamp到真实workarea，主题/DPI变重新layout，坏布局保默认 |

树/列表/代码/Errors/Modules/BP全部虚拟provider和可见行绘制，cell/cache以doc revision/stop sequence/module gen/font/theme/display epoch失效。执行state变化损伤对应pane，鼠标hover不重绘整1080p。行表和符号用排序索引二分；高亮只扩到状态收敛，jobs/pipe/调试消息每批预算后让出，无固定全局面板/任务表。

| 建议未来owner | 源边界 | 必须提供/接受 |
|---|---|---|
| 主代理kernel整合 | 新sc32_debug.c/h；必要process/paging/task/main/exit | owned session/真实frame/BP/module hooks/atomic wait与typed std绑定，旧ABI保留 |
| SC32 public桥owner | 拟SC32DEBUG.H/SC32PIPE.H+private WIRE及bridge/manifest | 正式records/selector/version/cap/error/fullrange，ScFile不混旧fd |
| ROOT模块owner | SCLDR公开值接口及ROOT真实登记/回滚/fini/unmap | 无foreign runtime指针、正确base/mappinggen/state/dep/import、epoch/publish短步 |
| compiler/linker owner | 诊断/SDB/u32 source位置、真object/multiTU linker | 各输入/输出format/type/provenance、原容器边界；由具体实际诊断逐项获准 |
| Studio工程/编辑owner | 新user/studio2及devtools project/document/build/diagnostic | 正式SCUI Document/树/输出、native G1、输入snapshot/资格、旧功能完整 |
| Debug模型/decoder owner | 新user/debug2与devtools debug/symbol/disasm | 只用typed Debug/模块值，严格stop/bounds/lifetime，Shared拒修补 |
| Shell/SDK/资源owner | SCSHELL正式file/launch/registry；SDK示例/索引/包资源 | 文件条件提交、session权限、只读公开头/文档、旧程序/用户数据保留 |

此表是分工建议，当前仅本设计doc获准写入。公共header/aggregate/json及同一kernel源只设一个owner，接口先冻结后正文，不并发覆盖本批仍在验证的compiler/SC32/ROOT。历史ide/debugger源码与已发布SCX归现历史入口保留，不大规模重复拷贝原盘。

## 13. 实施依赖与真实验收出口

先完成当前aggregate/SCUI/平台定向诊断与实际运行，再Phase5 host/文件事务/SCSHELL。Studio/Debug可独立写模型但不能在缺typed能力时给假成功按钮。typed Debug/pipe/target模块及编译元数据收齐后，按实际模块分工实现完整应用；项目的独立多TU能力还需真object/linker，不能从本批by-value修复推断已经拥有。

后续验证只按实质风险安排少量工作流，由主代理统一Windows双盘执行；本设计不启动它们：

1. 一份多文件工程使用真正G1构建app+独立SCL，记录完整source/tool/input/output/manifest/SDB摘要；实际调用生成库并运行生成app，含不同TU同名static及链接失败保旧bundle，证明非include-all冒充。
2. 共享Document两view、UTF-8编辑/Undo/Redo/Find/Replace、外部修改冲突、编译中编辑再Save，Errors跳到对应snapshot，未存/失败/Cancel保原。真实菜单/鼠标/键盘、Classic/Aurora原尺寸及窄布局，不重复旧19显示组合。
3. owned目标首指令→TF→INT3/恢复→Continue/Pause→正常/异常退出，动态PID超过旧8行仍用真实session；非owner请求拒绝，Restart/PID复用不接管旧目标。真实目标输入背压/EOF、双输出/最后writer和页/owner账。
4. 目标动态加载/失败init/卸载/再装SCL：真正base+RVA/导入/export/statement源码绑定，unmap撤BP、同VA新gen不写旧byte、错SDB/sourcehash拒绝，BOOT/ROOT/main/fini状态正确。寄存器/stack/memory同stop与LAST_STOP分明。
5. 同实际项目/分辨率/输入测Build装载时间、PIT、QEMU一核CPU、输入长尾、viewport repaint/submit、cold/warm PF与动态对象回收；静态预算和小decoder夹具不代表整机性能。

只有这些完整实际出口及保留清单成立后才称重制完成。现源码审计、设计稿、build08/09或平台87功能子结果都不能充当Studio2/Debug2验收；M10a2完整目标继续。

修订：2026-10-15，完整审阅现Studio/Debugger及现代SC32/SCLDR，建立多文件原生工程/共享编辑/诊断/owned Debug/模块符号/SDK与分工；仅设计，记录CPUINFO越界、typed能力/多TU缺项及本批aggregate/SCLMAP收齐未验边界，未实现或运行新应用。
