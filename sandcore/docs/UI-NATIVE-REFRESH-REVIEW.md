# SCUI 单项原生刷新审查

2026-10-15。所属版本 **SandCore M10a2 — The Application Platform Era**。本次仅只读审查Root批准的SCUI拖放端点修复及其原生刷新证明，不改compiler/kernel/user/tools/旧文档，不构建或启动VM。已读落盘的刷新工具与Root窄接reader；下文是静态来源审查，不是新VM或生成库执行PASS。

## 1. 真实差异与复用边界

两个统一构建报告均为`M10A2_SCUI_PLATFORM_COMPILER_UNIFIED_HOST_BUILD`、exit0、显式`source_files_modified_during_build=[]`。完整input/output路径集合一致，不只是比较交集。

| 记录 | 旧build11 | 新build12 |
|---|---|---|
| build report SHA | `fa45bfa50914afdee3ad843d44506391426a9fb6daa976bb8242ea02f989f7fb` | `a4ec1f6c19b0e77316f7a864fb8fa99aefb88f897523ef3e3d0c2506f7b37d4e` |
| 唯一input变化 `user/scui/ui_data.c` | `a6969d13f2a31dd111ce059ac43f2bdaf685bca774a16d9ab853ab2857f8f609` | `d73dc2c8ef3ffafb2bcc493aace143bc7fd59c65fb0872d9dc28e4e3b252e1bd` |
| 唯一output变化 `SCUI.SCL` | 330476B / `3c81b0cb8155115d7e4ec851af195ecac55d6571da36387878460af5630a1ecc` | 330816B / `dacea0cde8301c43b6a4c9ddbcbc73365583433723a0216bb0fb62b11df59ca6` |

源码差异集中于ui_drag_root端点ACL默认主体/会话绑定，释放时只Close真实endpoint，失败cause先保存再清候选，避免cleanup覆盖SC32 LastError。公共头、manifest、compiler、SC32、ROOT/STD/SCLIB、Boundary算法及三App源均未变。

`SCUI.C`实际包含`ui_data.c`，必须生成新SCUI原生字节。其余产物可以在完整依赖证明下复用，不能把“代码接口没改”直接当成库已执行的新证据。旧G1为242574B，SHA`50d152928d2eba50b46890d77ebe3295067bb372785a0db57708e26f752413a5`；已有真实执行/生成记录，可用这个固定G1只重编新SCUI。

主核/符号/启动盘应继续核全字节，当前两个报告相同：CORE`fbd014ba80543deaf052faf63c861a1e46e31ca2c58771bfb1a22beb5fdbc091`，core.sym`fafbac34a8eaa6bbadc62e0c908b3a9305f9fe46ae4604dfda0f7b715d099f0f`，core.elf`e0dc412efc07c747c8dbf6f1f49ee31517ad7f5f92d8c614f9c982004c71bb7f`，sandcore.img`47921cc2f9f0a23ea404cbd041a216931844023eedbcdbb4c824e9a55a8cfb3f`。原M10a1盘仍须核`3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64`，不能只相信旧报告中的true字段。

## 2. 必须先完整校验再复制

旧guest04完整报告应保持原SHA`dbad5d190a92bdb98f7518881412f5a977640644ecdd71d8debe868fa27dc672`及原状态、原input snapshot、原compile/execution记录。不能在旧目录原地换SCUI、重写provenance或补新current_inputs之后仍称原文件不变。

校验应覆盖：

- 旧报告完整PASS、源/原盘守恒、旧build11原报告绑定及完整路径清单；所有旧native blob的长度/SHA/格式/manifest合同、compile_case和G1来源相符。
- schema1两个provenance的原report相对引用、compiler SHA、完整libraries/applications/source snapshot匹配旧原报告；所有路径resolve后在旧out内，拒越界/别名目录与重复case。
- build12 current全部input/output长度/SHA、CORE/ELF/SYM相符；旧/新集合完全相等且只批准一个input/output变化，不能放宽为“任意SCUI目录变化”。
- 旧G1/SC32/三App/AGG字节仍与原generation及实际执行证明绑定；公共头/清单/编译器及相关标准库依赖闭合保持。最后校验脚本和原盘，再开始新out的候选复制。

失败只允许新输出目录报告FAIL_OR_INTERRUPTED/PREFLIGHT_FAIL及真实日志，不改变旧证据；不能提前生成可供GUI reader误认为PASS的provenance。生成中的对象保持候选状态，只有真实guest编译及get回字节、format检查、原盘/输入保持都成功后提交fresh元数据。

## 3. fresh / reused 来源合同

至少分三类事实，不把它们压成同一current snapshot：

| 事实 | 需要保留的来源 | 可以声明的结论 |
|---|---|---|
| 旧generation | 原report/path/SHA、旧build/G1、实际原byte、旧generation source snapshot | REUSED字节，当前未重编 |
| 新SCUI generation | 新current build12/source snapshot、实际本轮G1 compile_case、固定旧G1 SHA、get回的新byte及format contract | fresh SCUI已生成，不自动是已执行 |
| 当前兼容性证明 | 完整build11→12白名单diff、相关公共H/manifest/compiler/依赖SHA | 旧byte可与新SCUI组合候选，不能改写旧生成时间或源快照 |

G1/SC32/3Apps的原generation source不能由包含新ui_data的整个input字典替代。每项`origin_report_sha256`、原item/compile_case、原source snapshot或可核索引必须明确。复制byte的path可指向新out，但原path/report和SHA仍单列；schema1旧目录仍可按原规则读取。

新工具可以输出新的refresh scope/status，或在新的兼容report中保schema1主字段并加真实refresh来源层；两种都必须让reader区分旧PASS与fresh generation。不能只deepcopy旧report、换SCUI和当前input后继续叫一次全新FIRST_ROUND_PASS。

## 4. execution 依赖闭合

旧guest04的HOST与G1 PLATFORMPROBE/SCUITEXT真实运行，caller记录的库是`SYS_LIB_HOST_GCC_FIVE`。旧G1只是caller来源，不能把其执行库改为新native五库。

PLATFORMPROBE manifest直接依赖SC32，后端/kernel及SC32全部字节未变，可复用其旧真实87项的平台范围。SCUITEXT和SCUIDEMO的compile输入包括公开SCUI.H与manifest等，其全部字节未变，因此App编译产物可以复用；SCUITEXT旧执行的完整动态closure仍指**旧HOST SCUI**。Boundary算法未变是限定复用依据，不能抹掉那个旧库SHA并声称fresh native SCUI已执行。SCUIDEMO原记录只有生成，尚未验GUI，继续保持。

AGG两方向55项的custom library/caller/GCC counterpart、compiler/std/ROOT/kernel原byte及对应stdout仍须绑定实际origin。REUSED含callback与精确stack余额，不是本刷新VM再次运行。新工具只需真正执行固定G1重编SCUI；不存在的本轮platform/text/ABI执行不能以PASS case伪装。

fresh SCUI和复用其它byte组合后的实际五库普通平台/文本及GUI行为，由Root第二GUI VM取得新执行报告。`generated_system_libraries_execution=false`应保持直到那个运行证据；不能因为custom AGG库旧执行true就混淆系统库状态。

## 5. Root GUI reader 的必要窄接

原`native_first_round()`接受旧schema1完整第一轮，要求本report build hash等于当前build、全input等于当前路径、provenance source字典等于它的inputs，且每项compile_case为当前PASS。因此它会正确拒build11旧report配build12；Root现已加独立refresh验证分支，保旧入口的严格条件。

必要窄接应在识别独立refresh scope及完整校验origin/diff后：

- fresh SCUI走本轮真实compile_case/新source分支，验证当前manifest/G1 SHA/格式；其余REUSED按原generation+origin报告核对，不把case改名PASS冒充重跑。
- 分别记录本VM是否生成/安装/执行、prior实际执行/未执行；当前新out内路径全部受同一边界检查。
- 源保持检查依据当前兼容input；生成来源检查依据每项真实旧/新snapshot。旧schema1无refresh层继续原严格路径，不能为了refresh放松所有报告。

这次审查不修改reader，也不增加新suite。来源元数据确定后由Root独占窄接。

## 6. 当前审查状态

实际跨build差异、旧execution库来源和reader拒绝点已只读核对并发给Root/工具owner。刷新工具`tools/refresh_m10a2_ui_native.py`现已完整读，实际只安排旧G1重编SCUI；旧case为REUSED_PASS/original_status PASS，原runtime_dependencies_origin保存旧HOST五库，native每项generation_origin分开旧/新source。当前schema1 provenance的整体source明确标为CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN，另有artifact_generation逐项来源。这个表示方向符合合同。

| 实际审查项 | 源码结论 |
|---|---|
| 旧输入没有bytes | Root首先发现初版recorded_build/changed_entries误要求所有input长度。当前工具已只比较input path/SHA，output仍核bytes/SHA；新ui_data长度从current stat110016核，未伪造old长度，静态闭合 |
| QemuSession同out必须全新 | 初版先mkdir/persist/copy再交同out constructor，必FileExistsError。已反馈后当前工具全部preflight保持内存，由QemuSession首次建out，with后复制native候选；Harness local也在同vm.out内，静态闭合 |
| 全校验后复制 | 旧report/provenance/native bytes/compile contracts和新完整build/delta先核；HOST安装库格式也在with前核完，复制不修改origin，当前顺序合理 |
| G1 literal all-branch闭包 | 初版会从s3c.c的inactive COMPILEUI走到NUI/SCMEM/GLYPHS而拒合法G1复用。当前include_closure(compiler_cli=True)先核sccc.c明确SCCC_CLI1及s3c.c唯一CLI/GUI include段，精确只选CLI；其余literal分支仍保守，obtain_artifacts传该模式。未合成旧GUI snapshot，静态闭合 |
| Root reader generation | validate_refresh_origin重读origin/build/delta/byte/closure，valid_compile只对确切原case允许REUSED_PASS；旧schema1没有refresh层仍严格PASS，方向正确 |
| Root reader execution | 当前逐caller原字段、REUSED/本VM未执行/原report SHA和旧core+HOST安装库runtime闭包精确相等；aggregate原artifacts/要求不变并逐方向核原字段/REUSED/未本VM执行。与tool producer字段匹配，不要求重跑，静态闭合 |

以上仅围绕实际端点修复及同一schema，未新增suite。当前已见的实际阻塞均由所有者修复，并再次只读核对；未再发现具体P0阻塞。刷新工具新增validated_refresh为纯文件来源校验helper，不创建文件/VM；Root reader当前独立窄分支继续对生产字段核验。文档与工具最终冻结摘要由所有者交Root，真正旧G1重编和第二GUI仍由实际执行代理负责。

## 修订

2026-10-15：核真实build11→12单项差异与落盘工具，input长度/newout/G1条件分支三个实际阻塞及reader旧execution来源字段已由所有者修复并静态核对；无新增suite，未build/VM或改任何已有源/工具。
