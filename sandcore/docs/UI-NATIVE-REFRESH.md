# UI 原生SCUI定向刷新主控

2026-10-15，**SandCore M10a2 — The Application Platform Era**。新工具为 [refresh_m10a2_ui_native.py](../tools/refresh_m10a2_ui_native.py)，**SOURCE_ONLY：本文件只说明已收齐的工具与来源合同，没有自行构建、执行工具或启动VM。** 主代理统一运行；旧报告原字节与原FAIL/PASS保留。

## 为什么只刷新一个库

原guest04首轮已真实生成并执行G1，两个方向aggregate各55checks及owner账通过；G1生成SC32/SCUI和三个app，HOST/G1平台与官方3013文本工作流均完成。GUI01随后安装五份实际native库，平台与文本通过，但SCUIDEMO startup在ScIpcCreate因endpoint的ACL_SESSION未填allowed_session失败。

controls owner只修 `user/scui/ui_data.c`：同SID/subject ACL和cleanup cause，110016B/SHA `d73dc2c8ef3ffafb2bcc493aace143bc7fd59c65fb0872d9dc28e4e3b252e1bd`。主代理实比build11/12，input唯一差该源，output唯一差HOST `SCUI.SCL`（330816B/158exports/137imports）；CORE/ELF/SYM/boot/SCCC0、ROOT/SC32/STD/UTIL、所有头/manifest/Apps/AGG/Unicode全原字节。

因此本轮只用原实际G1在一个新Windows VM编当前SCUI，get回真字节。它不重新自举、不运行aggregate/platform/text、不跑GUI、不安装或执行新native SCUI；旧GUI01startup FAIL也不会被这个编译结果改成PASS。

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
