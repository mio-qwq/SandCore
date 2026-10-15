# UI 原生SCUI定向刷新主控

2026-10-15，**SandCore M10a2 — The Application Platform Era**。工具为 [refresh_m10a2_ui_native.py](../tools/refresh_m10a2_ui_native.py)。原单ui_data模式的refresh01已有真实生成/owner账PASS；下述新增发布修复模式目前仅完成源码和实际build14→16只读preflight，尚未执行VM。旧报告原字节与原FAIL/PASS保留。

## 为什么只刷新一个库

原guest04首轮已真实生成并执行G1，两个方向aggregate各55checks及owner账通过；G1生成SC32/SCUI和三个app，HOST/G1平台与官方3013文本工作流均完成。GUI01随后安装五份实际native库，平台与文本通过，但SCUIDEMO startup在ScIpcCreate因endpoint的ACL_SESSION未填allowed_session失败。

controls owner只修 `user/scui/ui_data.c`：同SID/subject ACL和cleanup cause，110016B/SHA `d73dc2c8ef3ffafb2bcc493aace143bc7fd59c65fb0872d9dc28e4e3b252e1bd`。主代理实比build11/12，input唯一差该源，output唯一差HOST `SCUI.SCL`（330816B/158exports/137imports）；CORE/ELF/SYM/boot/SCCC0、ROOT/SC32/STD/UTIL、所有头/manifest/Apps/AGG/Unicode全原字节。

因此本轮只用原实际G1在一个新Windows VM编当前SCUI，get回真字节。它不重新自举、不运行aggregate/platform/text、不跑GUI、不安装或执行新native SCUI；旧GUI01startup FAIL也不会被这个编译结果改成PASS。

## 成功ACTIVATE后的模型发布修复

GUI07已实际通过新五库平台、3013文本、DEFAULTAPI和各owner账，但Combo物理输入可能先于CREATE→ACTIVATE后的VIS通知被UI消费，模型仍用creating时的hidden cache，导致按键落到主窗default。Root只修`ui_tree.c`、`ui_menu.c`、`ui_dispatch.c`、`SCUIPRIVATE.H`：成功ACTIVATE立即真实Query本人窗口并同步surface/visible/geometry，失败回原rollback；私有`notified_visible`保随后第一次真实VIS业务通知。这不是强设可见、伪造消息或放开公共ABI。

`--post-activate-publication-repair`只接受完整`platform-guest05`原报告SHA `5535d2fdb7c6736aa7321d250bf521564055253b319199b6bf85d7f594c7e993`、成功build14凭证和已执行G1 `dd7ba9d45fbd1abbafa3f5468dc2d9ee5822625aca052df2e6655945aa302138`。对当前成功build16核相同路径集合，仅上述四输入和HOST SCUI.SCL输出可变，四源新旧SHA/长度逐项固定；失败build15不作为成功来源。CORE、编译器、其它库、头/manifest、App、boot必须原byte。

```text
python tools/refresh_m10a2_ui_native.py --out build/m10a2-ui-native-refresh-02 --build-report build/m10a2-ui-bootstrap/ui-build-16.json --previous-report build/m10a2-ui-platform-guest-05/ui-platform.json --post-activate-publication-repair
```

这个模式复用同一真实G1、SC32/SCCLIB、PLATFORMPROBE/SCUITEXT/SCUIDEMO/DEFAULTAPI四份动态App，以及SWDEFAULT原legacy机器产物；全部小字节、原编译case、manifest和include闭包独立核对。SWDEFAULT不是SCD动态程序，不套用动态格式；原六项普通main与owner账继续指向原报告。只有SCUI在一个VM重新编译，不自举、不重跑ABI/平台/文本，不安装或执行新SCUI。原default修复资格保留，但旧caller/aggregate结果明确标`REUSED`并保原安装库SHA、PID、CPU、PIT和stdout。

新`source_reuse.kind=POST_ACTIVATE_PUBLICATION_REPAIR`；`generation_origin`区分原生成和本VMSCUI新生成，当前准备snapshot不能冒充共同生成来源。纯`validated_refresh`仍返回`(report,libraries,apps,metadata)`，apps仅四份动态App，G1与SWDEFAULT只作为有独立来源的编译/机器证据metadata。GUI reader只在这个精确kind走新validator，旧single-ui_data模式入口与限制保留。新GUI必须运行新SCUI；GUI07三组旧执行不能标成“当前五库完全同byte”，DEFAULTAPI可按其ROOT/SC32/SCCLIB依赖单独复用。

## 运行入口与一份盘

在上述源码和统一build冻结后，由主代理显式运行：

```text
python tools/refresh_m10a2_ui_native.py --out build/m10a2-ui-native-refresh-01 --build-report build/m10a2-ui-bootstrap/ui-build-12.json --previous-report build/m10a2-ui-platform-guest-04/ui-platform.json
```

只允许Windows QEMU，--accel为TCG或既定WHPX；默认readonly原盘 `build/m10a1/sanddata.img`、256MiB客体内存。QemuSession自己建立全新out及唯一必要数据盘副本，在prepare回调直接写该副本；不预建会话目录，不额外复制旧源码树/数据盘，不覆盖旧out。preflight失败才建立诊断目录，没有VM。

准备复用first.payload/prepare：当前完整SRC/公开INC/UAX/许可归档，排除draft/private头进SDK；真实old G1单独安装于TMP，GCC库仅为本编译会话的既定系统环境，native生成库不装SYS/LIB。旧小G1、3Apps、SC32字节复制到新native目录，SCUI只保存本VM新get回字节。

旧Shell/旧Desktop warm与实际get/编译/owner账是本轮必要观测；没有新库预热、登录平台或文本测试。G1只执行一次当前SCUI的 `--scl` 命令，manifest完整比较且明确137imports/158exports，原load/BSS/stack/SCX格式没有放宽。

## 严格preflight与闭包

1. previous-report必须是原完整 `DECLARED_PLATFORM_TEXT_G1_FIRST_ROUND_PASS`、原scope、全部case PASS、source/current保持成立、原盘SHA相同；拒失败、仅生成或再刷新链。
2. 原build JSON自身SHA绑定旧report，完整scope/exit0/source_modified=[]及input/output ledger一致。**旧inputs只有path/SHA，outputs才有bytes**；不以当前已经改变的ui_data重建旧长度或旧全树。原G1/Apps/native库、aggregate真文件、stdout和双provenance另逐件核实际SHA/长度/生成来源。
3. 当前build由first.verified_build真实检查全部inputs/outputs/CORE与ELF边界；新旧路径集合相同，只有ui_data input SHA及SCUI output SHA/bytes可变。修复源还核当前实际110016B和冻结SHA；其它代码/头/资源/产物变化拒绝。
4. G1源闭包严格锚定 `sccc.c` 的 `SCCC_CLI=1` 与s3c.c确切CLI/GUI分支，仅取真正COMPILECLI；不将inactive COMPILEUI/NUI补造到旧snapshot。其余literal includes按保守全部分支递归，manifest加入种子，记录方法/源SHA。G1、SC32及3App闭包必须old=current；SCUI闭包必须包含当前修复源，独立记录。
5. helper当前采集差异如有仅记录为HOST_EVIDENCE_TOOL_ONLY，不把采集脚本变动当旧运行环境已重验。Artifact生成和运行origin永远指向其真实原report/库SHA；SCT、Unicode、headers/compiler等真实输入变化不豁免。

这不是另一套C预处理器，没有运行HOST AST或compiler验证来证明闭包；G1仅对已经固定的真实CLI条件做明确选择，无法解析的include形式明确拒绝。现一源修复不需要重新检验所有理论输入。

## 新报告与诚实来源

输出 `ui-platform.json` 保原首轮scope，成功仍为 `DECLARED_PLATFORM_TEXT_G1_FIRST_ROUND_PASS`，其意义由source_reuse明确为**合法旧运行证据+一次新SCUI生成的合并状态**，不是本VM重新执行全部首轮。

| 字段 | 真实意义 |
|---|---|
| input_sha256 / source_input_snapshot / pinned_build_input_output_sha256 | 当前准备与新build的固定快照，不是所有artifact共同生成时间 |
| source_reuse，schema1/kind ONE_SCUI_SOURCE_REFRESH | 原report/build SHA、唯一diff、原/当前snapshot、每artifact未变闭包与fresh SCUI闭包，no_rebootstrap/rerun与未执行新库明确 |
| case.status REUSED_PASS / original_status PASS / execution_class REUSED | 旧stdout/退出/样本属于旧VM；source_report SHA/source_case/runtime_dependencies_origin留原库SHA，executed_in_this_vm=false |
| 新SCUI case.status PASS / execution_class FRESH | 本VM真正编译/输出/format/log/map及必要warm/get/owner账，实际qemu_pid、生成SHA和old G1 SHA |
| native_artifacts、native_libraries逐item generation_origin | REUSED保旧完整source snapshot/原report SHA/compilecase及闭包；FRESH SCUI记录当前源、本VM和实际G1。current_compatibility单列未变契约 |
| executed_this_generated_file | same-byte历史事实；另有executed_this_vm。G1本轮执行只为编译，Apps本轮不执行，native系统库false安装/执行 |
| superseded_scui_cases | 旧SCUI生成/旧GCC SCUI保持记录，只作历史，不当本次新库case |

旧平台/文本曾用旧HOST五库，报告保留这些完整SHA作为旧execution origin。新SCUI代码即便ABI/Document算法其它源不变也没有被本VM执行；不把旧3013文本结果换成新native SCUI的运行声明。下一GUI原生库VM由主代理接着验证实际新字节。

两个目录 `native-artifacts/`、`native-libraries/` 各保存完整schema1 provenance：必要legacy字段保持，新增 `source_snapshot_scope=CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN` 与 `artifact_generation`。旧G1/Apps不会因为顶层准备快照包含新ui_data就被改写生成输入；真正origin逐item可追到原报告。

## Root GUI reader的窄接线

新工具提供纯函数 `validated_refresh(path, build_report, proof_hashes)`，返回与原native_first_round相同的 `(report,libraries,apps,metadata)`，只读验证、不写文件、不启动VM。Root reader仅在source_reuse.schema1/kind相符时调用它；旧原首轮路径仍使用原strict入口。

纯校验重新核原凭证/diff/CLI及artifact闭包、当前文件/格式、REUSED原case与SHA、FRESH唯一允许的12个必要case、真实SCUI137/158与G1来源、双目录provenance，保持其它strict边界。不会仅因新bool叫“pass”就接受旧证据，也不会放宽到任意source变化或fake当前generation。

Root旧reader若仍要求所有case.status=PASS或把统一current source_input当共同generation，须用上述分支精确适配；本工具不修改已有reader、firstVM脚本或原报告。

## 失败与保持

preflight先在内存完成；QemuSession建立唯一out之后才复制小artifact、写provenance，避免构造器的exist_ok=False冲突以及Harness local相对目录错误。失败保完整stdout、JSON/cause/traceback、只读寄存器与截图，关闭本工具owned VM，不自动重开/扩测试。

当前源/构建/工具、旧report/build/provenance/artifact/stdout、复制的小native及新SCUI输出都在结束前重新核SHA。原数据盘前后SHA相同，实际生成盘只为本VM；任何保持失败终态FAIL_OR_INTERRUPTED。Raw旧PASS/FAIL不改，失败刷新不会替换Root当前可用原生产物。

本批工具/窄文档先静态收齐与只读review，再由Root统一运行。成功仅提供可交第二轮的新SCUI和可核对旧来源链，不是完整SCUI或M10a2验收。

修订：2026-10-15，收齐单VM实际旧G1重编SCUI、精确one-source diff/生成与运行来源分列、pure reader validator及原盘/输出保持；源码未自行执行，正式结果由Root记录。

修订：2026-10-15，保留旧单项模式，新增固定四私有源POST_ACTIVATE_PUBLICATION_REPAIR、完整first05/正确G1与STD/SC32/四动态App/legacy SWDEFAULT来源；实际build14→16只读preflight通过。新增模式与后续新SCUI GUI仍未运行，不继承GUI07整体PASS。

## 已发生的 publication 执行与选择修复入口

`m10a2-ui-native-refresh-02` 已实际仅用原 G1 重新生成 SCUI，输出658552B、SHA256 `c8141f5d664f07fd48b34edc52a7924dd32ad8381445c4dbbcc3bc6c2be8cdea`，编译和 owner 回收通过；该刷新 VM 没有安装、执行新库。GUI09 随后实际使用它通过3013条文本语料和 owner 账，但 Popup 的 F12 诊断等待失败。GUI10 的真实三项 Combo 选择/关闭通过，随后 Ctrl+A 仅选择1项，整轮仍 FAIL。原报告均不修改，紧凑副本见 `evidence/m10a2-ui-default-publication/`。

选择故障是 View 将虚拟根ID0传给单项 `ScUiSelectionSet`；根0按原合同不可选择。修复后 flat/List/Table 取真实首尾 stable ID 调已有完整 `SelectionRange(REPLACE)`，空模型原子 Clear；Tree 按当前展开段创建一次拥有ID候选，回调修改 view/provider/selection 或段序则拒绝旧候选，失败不发布半选择。Range 与 IDs 提交前新增 selection revision 核对，避免 provider 回调中的合法 Clear 被覆盖。公共158入口、137imports、manifest和App字节保持。

独立 `VISIBLE_SELECTION_RANGE_REPAIR` 模式只接受成功 build17，报告SHA256 `a05e3e76a478fda829a17b677cec4be134e0ee58d07a110cd3a4694e6266af6a`。它仍从完整 first-guest05/build14 开始，固定原真实 G1 `dd7ba9d45fbd1abbafa3f5468dc2d9ee5822625aca052df2e6655945aa302138`；允许输入精确为既有 publication 四源及 `ui_data.c`、`ui_provider.c` 两源，输出仍只能变 SCUI。两新增源冻结SHA分别为 `fdb60e57dbb086613a333075cafa6ec3919d6b1afa427b86870085914883acc6`、`acdae2b9a38fbef3513340342c564cd9f702a15a867a1eb33d074c5a11882c79`。原四源前后SHA和六源长度由producer/pure validator同一常量表核对，不接受任意第七源、刷新链或新G1。

```powershell
python tools/refresh_m10a2_ui_native.py --out build/m10a2-ui-native-refresh-03 --build-report build/m10a2-ui-bootstrap/ui-build-17.json --previous-report build/m10a2-ui-platform-guest-05/ui-platform.json --visible-selection-range-repair
```

该模式与原模式互斥；旧单ACL入口、四源 publication 入口各保留自己的冻结门。它只编译一次SCUI，保留原SC32/SCCLIB、四App、SWDEFAULT生成来源，不自举、不重复ABI/平台/Unicode。刷新后GUI按真实依赖仅复用07未加载SCUI的PLATFORM/DEFAULT两项及其owner，新的SCUI在同一GUI VM实际执行一次现有3013语料和owner，再继续完整GUI；不把09旧库执行改称新库通过。

修订：2026-10-15，记录publication02真实生成、GUI09文本PASS/整体FAIL、GUI10 Combo后Ctrl+A FAIL；新增精确六源选择修复刷新入口，统一build17及宿主SCCC仅SCUI预检已通过，客体刷新和GUI仍待执行。

## 高DPI与换行修复：同一G1生成两个真实产物

后续 `refresh03` 已真实生成SCUI 662584B，SHA256 `1f38ab0b4510bbb644f4fb809cbae43b9443cf0647fbf3ba07382e798beb5153`，与同源宿主SCCC预检字节一致。GUI11/12 使用该库完成实际全选、全文编辑、拖放、属性/文件、主题/DPI和局部损伤步骤；GUI12仍因隐藏行解析终态FAIL，且逐张截图发现200%工具栏和Resources裁切。因此保留原失败，不把数值或子项PASS提升为完整视觉验收。

新增互斥入口 `DPI_OVERFLOW_WRAP_REPAIR` 固定成功 build18：报告SHA256 `9be0ddfa6a241ebecb9f345582119fd9ab18125b818f7be4ba0c55805a71cae5`。对完整 first05/build14，累计变化必须精确为9个输入：原publication四源、选择Data/Provider两源，以及controls、layout和Demo；输出必须仅有SCUI.SCL与SCUIDEMO.SCX。具体前后SHA及长度是producer的固定 `QUALITY_SOURCES` 表，不接受其它变化。

本模式保留原真实G1/SC32/SCCLIB/PLATFORMPROBE/SCUITEXT/DEFAULTAPI/SWDEFAULT生成来源；只由同一原G1生成当前SCUI与当前Demo。两份新产物各有真实compile/output/format/log/map、独立owner账、独立FRESH generation_origin和实际include/manifest闭包；旧Demo编译记录另存 `superseded_demo_cases`，不能继续声称旧Demo字节包含Resources的WRAP描述。共享清单仍为SCUI 158exports/137imports、Demo133imports。

```powershell
python tools/refresh_m10a2_ui_native.py --out build/m10a2-ui-native-refresh-04 --build-report build/m10a2-ui-bootstrap/ui-build-18.json --previous-report build/m10a2-ui-platform-guest-05/ui-platform.json --dpi-overflow-wrap-repair
```

pure validator逐份核两份FRESH实际产物、原G1 SHA、源快照、闭包、未安装/未执行状态和双provenance；GUI reader显式接受此模式并装入新Demo，caller记录带它自己的generation_origin。PLATFORM/DEFAULT仍只按其未加载变化SCUI/Demo的真实闭包复用07，SCUITEXT在新库下同一次GUI VM执行一次既有语料，其余只有完整GUI收尾；不自举、不重复ABI或扩其它组件矩阵。

修订：2026-10-15，新增固定9输入/2输出高DPI修复模式，记录Toolbar/MenuBar原菜单范围overflow、宽度依赖的两遍Label候选布局和Demo单行WRAP；build18通过，两个新客体产物和最终视觉/恢复/退出证据尚待执行。

2026-10-16：build23/refresh09实际补齐node0上下文CAPTURE/BUBBLE，并由原G1生成SCUI及Demo；GUI21真实Drop/ACK已成功但主控行首边界误超时，原失败保留。只借紧邻start前的一个换行修正主控，实际原日志回归及旧标记拒绝通过。GUI22最终DECLARED_NATIVE_FIVE_LIBRARY_ORDINARY_GUI_SECOND_ROUND_PASS，共91项，GUI_behavior_verified/source_disk_unchanged/current_inputs_unchanged均true；两主题、100/150/200%实际字宽/换行、溢出菜单、文件保存/错误、Drop提交及Complete、静止/隐藏零提交、窗口恢复、正常return与最终owner/PF归账通过。主代理已查看Classic与200%原尺寸截图。公开158入口/137导入未变，SCAPI原SHA保持。静止200PIT实际2.032秒、QEMU一核CPU0.171875秒；checkbox观察127PIT包含F12/串口，不冒充纯输入延迟。证据evidence/m10a2-p4-context-and-p5-generation/index.json，不代表整版完成。
