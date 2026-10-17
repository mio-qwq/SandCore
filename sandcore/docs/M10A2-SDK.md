# M10a2 公开 SDK 与来源合同

SandCore M10a2 — The Application Platform Era。最终 SDK staging 增量已实现；普通系统库、G1/SCLD、codec 例外与独立 ResearchDesk 的来源分别记录。2026-10-17的final11已经用HOST20/native10真实来源完成严格staging：496项/17503689B，八普通原生库、实际G1/SCLD及唯一SCSOUND宿主codec例外均逐项绑定。普通身份ResearchDesk已在GUI04经Studio的SCW工作区实际编译独立RDSTATS.SCL和四个TU的RDESK.SCX，运行与Debug2模块/符号/断点有独立证据；不将此扩成每个示例功能或整版完成。最新拖放/UI增量的普通交互仍由GUI12收束。2026-10-15五库preview和双示例宿主结果保留原时态。

## 内容与边界

`tools/stage_m10a2_sdk.py` 只核验和复制，输出目录必须不存在。它不构建、不启动 QEMU、不修改镜像或当前 /SYS、不建立 ZIP/Release。检查失败保留失败记录，不覆盖或删除旧 SDK。每次读取固定 SHA，开始复制前、复制后与 metadata 提交后再核输入守恒；SHA256SUMS 覆盖实际 payload、MANIFEST、PROVENANCE 和 HASHES，HASHES 明确不自引用。

| 路径 | 实际内容 |
|---|---|
| SYS/INC | 官方公共头原字节，11 个标准小写头与明确大写公共白名单；RUNTIME/SCLDR.H 保留相对 include 布局 |
| SYS/LIB | 8 个普通原生系统库、明确标记的 SCSOUND host codec，以及显式声明的普通原生插件 |
| SYS/SDK/LINK | 与所选实际二进制动态表逐项相等的原生 manifest；存在时选同源 `.NATIVE.JSON` |
| SYS/SDK/HOSTLINK | 原 `.json` 宿主 manifest，明确不作为原生编译清单 |
| SYS/BIN | 实际 guest G1 SCCC.SCX、独立实际 guest SCLD.SCX；host-preview 全程另标宿主来源 |
| SYS/SDK/SRC | 对应实现、公共/私有布局、工具链、现代应用及 codec 的固定源码摘要；私有内容只作源码归档 |
| SYS/SDK/EXAMPLES | EDIT2、UTILITY 和 RDESK 的独立 TU、插件、SCW工作区、manifest、响应文件、普通用户 BUILD.SH 与说明 |
| SYS/SDK/DOC | 同批公开规范、SDK 和示例使用说明 |
| SYS/UI、SYS/FONT | 真实 Unicode 数据/oracle 与原始凤凰 12/16px TTF |
| SYS/LICENSE | 所纳入第三方的固定完整源码、嵌套实现、完整许可/版权/来源与项目许可 |

普通原生库为 SCLDR、SC32API、SCCLIB、SCLIB、SCUI、SCGFX、SCSHELL、SCTERM，均以 `.SCL` 安装。SCSOUND 是单列的固定用户已授权宿主 codec 例外。实际 ABI major/minor/library_version 来自所选 SCL；本规范不把库版本固定写死在过时计数中。当前实际SC32API为ABI1.5/v7（194导出），SCUI为ABI1.4/v6（174导出）；独立SC32DROP2.H声明新释放凭证入口最低版本，基础SC32API.H旧声明原字节保留。所有版本按实际provider manifest/二进制共同核验。

公共头包括 SC32API 及 GUI/Font/Palette/Popup/Broker/Drop/Debug/Path/Lease/Replace/Host/Wall/Audio/SIMD/Config/Capture/Image/Auth/Terminal 独立头；SCUI/SCUITABS/SCUIARGB/SCUICANVAS/SCUISECRET、SCGFX/SCGFXDRAW、SCLIB、SCSHELL/SCSHELLABSENT、SCSOUND、SCUITERM、SCOFMT、SCDBGFILE、SCTYPES 和 RUNTIME/SCLDR。最终白名单由实际同批工具读取，所有 literal include 必须在 INC 闭合。源码新增实现目录时 SC32 聚合的真实 C include 根会纳入来源集合；audioapi/configapi/PTY 均不能遗漏。

标准头正好为 stddef.h、stdint.h、stdbool.h、stdarg.h、limits.h、errno.h、assert.h、stdlib.h、string.h、ctype.h、stdio.h。标准 C 运行库只存在于 Ring3，实际支持/限制见 SCCLIB。标准32位整数/指针与真实结构返回 ABI 见 SCCC-AGGREGATE-ABI。

PRIVATE、WIRE、内核头、旧 SCAPI/SCIO/SCMEM、私有动态格式和 codec 的宿主兼容头不得进入 INC。原字节源码可归档到 SRC，不能借源码归档把私有桥宣称公开接口。SCAPI.H 原字节和旧 ABI、旧 SCX 262108B load 限制保持。

## 普通原生来源与复用

native-preview 逐名要求 8 个普通系统库的 `--native-library NAME=PROVENANCE_JSON`，外加 `--compiler-provenance`、`--linker-provenance` 和 SCSOUND 的 `--host-codec-proof`。源报告须是实际成功、输入守恒的统一构建；每个库须匹配真实客体 SCCC compile case、退出值、精确产物 bytes/SHA 和原 stdout。G1 还须有生成文件曾执行证明。SCLD 是有独立 generation case 的真实客体 CLI，不用宿主 ld 或 Python 生成器替代。

普通库可复用未变字节，但须保留该产物原 generation 报告/源码快照、编译器 case 和原 stdout。refresh 当前 snapshot 仅作准备态，不能冒充被复用产物的生成源。自身代码、私有布局和实际公共声明必须一致；当前 SDK provider 的 ABI、符号 kind/宽度和依赖闭包也必须满足。无关 CORE 总摘要或独立应用布局变化不迫使未变库重编。SCSHELL 的独立 desktop 应用不进入 SCSHELL 库的代码闭包；SRC 仍完整归档它。

SCCC 会保留部分未调用 static helper，GCC 可能删除它们，二者实际 imports 可不同。因此 native-preview 优先选同目录 `<stem>.NATIVE.JSON`，HOSTLINK 保留原 `.json`。JSON metadata 不作为代码复用失效条件；每份当前所选 manifest 的 SHA、是否原 host build 成员、实际二进制表相等校验均另行记录。新生成 NATIVE metadata 不能替代真实 generation 源码或二进制身份。SCLD、mkscl 和 mkdscx 的精确导入规则不放宽：多余/缺失 import、导出或依赖不相等均拒绝。

库名、ABI、imports、exports、依赖、init/fini 存在性均从实际 SCL 检查；闭包按真实 provider 导出 kind 与最小数据宽度解析，环/缺依赖/版本不足失败。扩展插件继续使用精确七字段 SDK 描述：schema、library_manifest、native_provenance、source_roots、public_headers、resources、license_manifests；不能覆盖核心名字、不能借 codec 豁免。

## 唯一 SCSOUND 宿主 codec 例外

`tools/make_m10a2_codec_proof.py` 只从真实成功构建报告提取证明，不编译库、不生成 native 结果。证明精确绑定 SCSOUND.SCL 五个冻结导出、实际 host report 与 artifact bytes/SHA、user/scsound 全部实现/清单、实际 codec/include 私有兼容头、使用的公共布局和标准声明、dr_mp3/dr_flac 的固定 vendor 源。source_input_sha256 必须正好等于固定闭包，重复路径拒绝。该例外不能用于 SCUI、普通第三方库或任何借同名的其它源根。

许可沿第三方 M9 固定 dr_libs 提交 `dfe8377631000664666519fdb83da193fd8037f4` 的 MIT-0 选择，完整复制实际 dr_mp3、dr_flac、LICENSE、PROVENANCE 和本地 notice；dr_mp3 内嵌 minimp3 的 CC0 原文与 notice 同时保留。SCSOUND 的工程适配记录与实际实现均归档。宿主 codec 的来源始终为 `USER_APPROVED_HOST_CODEC_EXCEPTION_NOT_GUEST_NATIVE`，不将整套 SDK 或其它库改称宿主例外。实际解码/音频执行由对应客体验收另证。

host-preview 从显式 host report 的实际输出取所有库、SCCC0/SCLD，并全程标记 HOST；禁止混入 native provenance 参数，不自动降级。它只能用于宿主来源查阅，不能作为正式 native-preview 的替代。

## 独立多源开发

```text
sccc A.C A.SCO --object
sccc B.C B.SCO --object
scld APP.SCX --dynamic APP.JSON @OBJECTS.RSP
scld PLUGIN.SCL --scl PLUGIN.JSON @PLUGIN.RSP
```

SCO v1 保留各 TU 的段、section-relative 定义/重定位、未决函数/数据、private/static 身份、对齐/BSS 和真实源行。链接器按对象解析 duplicate/missing、落实各对象记录的对齐，最终输出 SCX/SCL、map 与 SCDB1MIO v1 `.sdbg`。副文件绑定完整映像 SHA，不冒充内核验证的实际装载身份。响应文件只有对象路径，不能包含源码、嵌套 @ 或隐式 include；具体限制见 SCO-AND-LINKER。

ResearchDesk 的 MAIN/MODEL/VIEW/IO 四独立 TU 与 RDSTATS 独立插件展示公开的多文档、共享 UndoRedo、树/表 provider revision、排序与源行、可调布局/主题、图表、文件对话框、剪贴板、未知长度读取和条件保存。数据/结果绑定正文 revision 与工作代数，失败候选不破坏原数据。已有目标使用 Preserving 精确发布；只有 durable COMMITTED 且正文修订相同才清 dirty。RDSTATS.SCW和RDESK.SCW分别描述插件与四TU应用，SCW即SandCore Workspace（工程工作区）。详细合同见SCW-WORKSPACE；命令与普通用户结束条件见 SDK-RESEARCHDESK 和示例 README。

```text
/BIN/SH.SCX /SYS/SDK/EXAMPLES/RDESK/BUILD.SH
/HOME/RDESK/RDESK.SCX
```

路径沿当前 rootless 63B/绝对 64B、无空白的公开边界。ResearchDesk 文档4MiB、标签64KiB 上限明确拒绝；不是静默截断或声明无限。插件从实际应用目录装入，已取符号固定 provider 至任务退出；没有声称可热卸载函数地址。

EDIT2 和 UTILITY 原例及历史证据保留。它们原先的直接 FILE 保存仍可能留下已写前缀；不会因新 ResearchDesk 实现条件保存就把旧示例改称原子保存。当前完整验收优先使用 ResearchDesk 的精确保存路径。

## 实际证据与后续出口

2026-10-15 的 `build/sdk-native-preview-01` 为五库旧 preview：79 payload、10865491B。公开 INC 下 HOST GCC 的 EDIT2/UTILITY 为17348/4999B，真实 SCCC 宿主后端为25613/7951B，65/36 imports 与当时清单相等。原报告/原目录不重写，它们没有执行 i386 或客体 G1，不证明本批新增库/示例。

2026-10-16 新增实现与宿主报告分列：ResearchDesk 真实 SCCC→五个独立 SCO→SCLD 生成应用和插件；真实 SCUI/SCLIB 的分离应用 TU 模型检查（窗口/字体/文件 gate 显式替身）；公共 include 闭包与拒假 native/借名 codec、实际动态表不匹配拒绝。精确源码/产物摘要和大小见同批 DELIVERY 与三个报告。

完整新 SDK staging 须由最终真实 guest 生成来源驱动；ResearchDesk 须在普通用户执行 BUILD.SH、实际 GUI/剪贴板/保存冲突/插件失败恢复/隐藏后台推进与退出回收。达到例示结束条件即收束，未改变库证据直接复用，不扩排列组合。staging 成功、宿主模型 PASS、guest 编译和实际运行是四种不同证据，均不单独等于 Phase7 或完整 M10a2 完成。

## 修订记录

原生来源检查以各库固定聚合入口的完整literal include闭包及公开布局头为准，逐文件与真实generation摘要核对；目录中未接线的历史草稿不冒充参与过客体编译。完整源码归档仍按当前HOST/archive报告固定全部声明目录的文件，因此“保留源码”与“实际编译来源”分别记录，不以删除草稿或补造guest摘要放行。

| 日期 | 修订 |
|---|---|
| 2026-10-15 | SDK 五库 preview 与双示例宿主后端检查；客体执行待验，原证据保留。 |
| 2026-10-16 | 完整公共头/八普通原生库及固定 codec 白名单、独立 SCLD、SRC/许可/来源、NATIVE/HOSTLINK 和 ResearchDesk 多源开发合同；本批整套客体 staging/运行待主代理统一验证。 |
| 2026-10-16 | 原生来源核真实聚合include闭包；未接线历史草稿仍完整归档、按HOST固定，不伪称guest编译。 |

2026-10-16：公开SCW工作区合同、示例路径和SDK归档接线。

修订：2026-10-17，更新final11真实原生来源SDK与GUI04普通SCW/独立SCL/四TU运行调试事实，明确新释放凭证接口版本及GUI12尚未验证范围。
