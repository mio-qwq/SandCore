# M10a2 SDK Preview staging

SandCore M10a2 — The Application Platform Era 的公开 SDK 暂存入口是
`tools/stage_m10a2_sdk.py`。本批提供工具源码、双窗编辑器和 CLI 示例源码；
**native-preview staging已实际完成，两个示例已通过只用公开INC的HOST GCC和实际SCCC宿主后端编译；尚未在SandCore中编译或运行。**
成功目录证明来源检查和复制完成，编译证据见 [检查点](../evidence/m10a2-sdk-public-compile/README.md)，不是 SDK 客体验收、Phase 7
完成或完整 M10a2 发布。当前 GUI、桌面、主要组件重制和最终统一验收继续按原合同推进。

## 1. 暂存内容与边界

工具只收集指定来源，不构建、不启动 QEMU、不修改镜像、不安装当前 `/SYS`、不创建 ZIP、
不创建 Release。目标目录必须不存在；检查失败不覆盖已有目录，也不删除失败副本。

| 暂存路径 | 内容 |
|---|---|
| `SYS/INC/` | 官方公开头原字节；标准头保持小写，自有头保持大写 |
| `SYS/INC/RUNTIME/SCLDR.H` | ROOT 的正式低层公开头，保留相对 include 布局 |
| `SYS/LIB/` | 显式提供并核验的五个系统 SCL，及可选实际插件 |
| `SYS/SDK/LINK/` | 对应实际 SCL 的原始 JSON manifest |
| `SYS/BIN/SCCC.SCX` | 实际客体生成并曾执行的 G1 CLI，或明确标记的 HOST profile CLI |
| `SYS/UI/`、`SYS/UI/TEST/` | Unicode 15.1 属性资源与两个官方分段 oracle 的二进制资源 |
| `SYS/FONT/` | 原始凤凰 12px/16px TTF，供当前正式 Font 服务按需使用 |
| `SYS/LICENSE/` | 所纳入第三方的固定原件、完整许可、版权和来源记录；项目许可原文 |
| `SYS/SDK/EXAMPLES/` | `EDIT2.C/JSON`、`UTILITY.C/JSON` 和示例说明，源码状态单列 |
| `SYS/SDK/DOC/` | 对应库、ABI、编译与本 SDK 使用规范 |
| 目录根 | `MANIFEST.json`、`PROVENANCE.json`、`HASHES.json`、`SHA256SUMS`、客体命令 |

五库为 `SCLDR.SCL`、`SC32API.SCL`、`SCCLIB.SCL`、`SCLIB.SCL`、`SCUI.SCL`。
库以实际 SCL 动态表核对清单名字、版本、导入、导出、依赖和 init/fini 存在性；
导入的 kind、最小宽度及依赖版本必须由目录内真实 provider 满足，依赖环明确拒绝。
导出函数的正文宽度、数据对象宽度和 RVA 直接记录真实 SCL 表，不从 C 声明猜测。

普通应用包含 `SC32API.H`、`SCUI.H`、`SCLIB.H` 和所需标准头即可。
公开 INC 共收齐 `SCTYPES.H`、`SC32API.H`、`SC32GUI.H`、`SC32FONT.H`、
`SC32PALETTE.H`、`SC32POPUP.H`、`SC32BROKER.H`、`SC32DROP.H`、`SCUI.H`、
`SCLIB.H`，以及上述 `RUNTIME/SCLDR.H`。ROOT 原头中的 `../SCTYPES.H` 因而在 INC
内正确解析；工具不重写头来迁就平铺目录。普通现代程序无需调用 ROOT 引导接口。

标准头正好为 `stddef.h`、`stdint.h`、`stdbool.h`、`stdarg.h`、`limits.h`、
`errno.h`、`assert.h`、`stdlib.h`、`string.h`、`ctype.h`、`stdio.h`。
实际标准兼容范围、整数宽度、FILE/端点语义和格式限制见 `SCCLIB.md`；SDK 不缩减这些语义，
也不因有头文件就承诺尚未实现的标准库族。

`PRIVATE`、`WIRE`、`NUI`、旧 `SCAPI/SCIO/SCMEM`、内核头及私有格式头不能进入 INC。
所有公开头的 literal include 必须在暂存 INC 内闭合，不允许逃出 INC 或不可枚举的宏路径。
编译器可执行文件自身保留已有引导实现，不意味着现代应用可以复制旧 syscall 桥。

## 2. 真实来源校验

`--build-report` 指向一次真实成功的 M10a2 统一构建 JSON：`exit_code=0` 且
`source_files_modified_during_build=[]`。工具按报告的精确路径逐一核当前 input SHA；
报告 inputs 本来没有 bytes 字段，不伪造旧长度；outputs 则同时核 bytes 和 SHA。
包括 CORE 在内的报告输出必须仍存在且相等。SDK 不携带或替换 CORE，只记录该构建来源；
实际安装和运行必须由后续统一验证使用具备对应 Font/Broker/Drop/Popup 能力的 CORE。

`native-preview` 需要逐名显式指定五库的 `PROVENANCE.json`，以及
`--compiler-provenance`。不从最新文件时间、目录里某个同名文件或 HOST 输出猜来源。
来源 JSON、完整原报告、实际 SCL/SCX、成功 compiler case 和对应 stdout 都固定摘要。
原报告必须记录成功及源码、原盘守恒；provenance 的库表与原报告须精确一致。

P3 原报告的 `generated_libraries` 与本轮平台报告的 `native_libraries` 分别按实际 schema
读取。P3 整体状态位不会被当库。小型证据归档若保留原报告的绝对 stdout 路径，工具仍只读
该原路径及其原 SHA，不自动换成归档内同名文件。缺来源文件时明确失败。
实际产物必须在所选 provenance 所属 out 内；不能把指向别处的归档元数据当作自包含来源。

对于 refresh，整体 `source_input_sha256` 只是本次准备快照；每个产物必须保留自己的
`generation_origin`。`REUSED` 回到带原 SHA 的完整旧报告核生成 case、原 byte 和原源码；
`FRESH` 必须有本 VM 实际生成记录。工具核每库自身实现/manifest、正式公开头以及
`SCTYPES.H`、原 `SC32GUI.H` 布局仍与其生成来源相等。P3 ROOT/SCCLIB/SCLIB 不因 SC32
新增公开入口而被虚称重新生成：其自身源和共同布局保持，新的 provider 用实际 ABI/导入闭包
核兼容。SC32 和 SCUI 自身正式头必须匹配自己的真实 generation。

CLI 必须是真实 `SCCC1.SCX`，保留旧 `SCX1MIO` 容器、32 位 i386 映像/栈/作者与容量检查；
其当前 CLI 编译器源码须与指定构建一致。SDK 中改名为 `SCCC.SCX` 只提供稳定入口，来源记录
仍写明原 `SCCC1.SCX`、原报告和 SHA，不称作新一代编译器。

每库来源区分 `ACTUAL_SCCC_NATIVE` 和 `HOST_GCC_BUILD_ONLY_NOT_NATIVE`。
“实际生成”“该报告曾执行此产物”“staging 本身没有执行证明”分别记录；
原库后续执行证据仍看相应原验收报告，不能由一次文件复制补出来。

## 3. 调用方式

从 `sandcore` 目录调用。下列来源接法已用于本批实际staging（build13），没有客体执行或发布。
若当前库或编译器源码已经变化，应使用对应新成功构建/真实 generation；不改旧报告来让检查通过。

```powershell
python tools/stage_m10a2_sdk.py `
  --out build/sdk-native-preview-01 `
  --build-report build/m10a2-ui-bootstrap/ui-build-13.json `
  --profile native-preview `
  --native-library SCLDR.SCL=evidence/m10a2-stdlib/native-libraries/PROVENANCE.json `
  --native-library SCCLIB.SCL=evidence/m10a2-stdlib/native-libraries/PROVENANCE.json `
  --native-library SCLIB.SCL=evidence/m10a2-stdlib/native-libraries/PROVENANCE.json `
  --native-library SC32API.SCL=build/m10a2-ui-native-refresh-01/native-libraries/provenance.json `
  --native-library SCUI.SCL=build/m10a2-ui-native-refresh-01/native-libraries/provenance.json `
  --compiler-provenance build/m10a2-ui-native-refresh-01/native-artifacts/provenance.json
```

仅需查看 HOST 构建产物时可显式选择 `--profile host-preview`，不传 native 来源参数。
该 profile 从所选构建的实际输出读取五库和 HOST SCCC0，并全程标记 HOST；
它不是原生 SDK 验收包。两种 profile 都核实际格式/动态表，也都不运行可执行文件。

当前工具只接纳 native profile 的可选插件，使用重复的 `--plugin-manifest` 参数。
每个 SDK 插件描述 JSON 精确包含七个字段：

| 字段 | 合同 |
|---|---|
| `schema` | 整数 1 |
| `library_manifest` | `sandcore` 内现有库 manifest 相对路径，且在指定构建 inputs 中 |
| `native_provenance` | 现有真实客体生成来源的相对路径 |
| `source_roots` | 非空的 `user/` 下实现目录列表，每项以 `/` 结束；完整实现必须与 generation 相等 |
| `public_headers` | 本次构建固定的实际大写公开头路径列表 |
| `resources` | 实际 `{source,target}` 记录列表，source 在构建中，target 不能覆盖 INC/LIB/BIN |
| `license_manifests` | 固定第三方清单路径列表；项目自写且无第三方材料可为空 |

插件清单的文件路径均相对 `sandcore`，禁止 `..`、绝对路径和反斜线别名。
扩展库不能重名或覆盖核心五库，所有导入必须在当前 SDK 内真正满足；公开头同样受 INC 规则约束。
许可清单沿用 `third_party/M10A2-SOURCES.json` 的 version/components/source/guest/files
格式，完整复制固定文件及 local_files。没有真实库、公共头、来源和依赖闭包时不会生成插件入口。
以后 SCSHELL、SCGFX、SCRES 等只能用此显式清单加入，不提前伪造文件或能力。

## 4. 摘要、失败与许可

复制前完成来源、格式、依赖、目标名冲突、公开 include 和资源清单预验。
全量输入在复制前、复制后和 metadata 提交后复验 SHA；复制品同样逐文件核 SHA/长度。
目标名按 SandFS 不区分 ASCII 大小写检查冲突，并限制 ROOTLESS 路径不超过 63B。
`MANIFEST.json` 记录布局/表/版本/依赖初始化序和每个文件来源；`PROVENANCE.json`
保留每产物原生成来源和固定输入；`HASHES.json` 覆盖 payload、MANIFEST、来源与命令。
`SHA256SUMS` 再覆盖 HASHES 本身；摘要文件不自引用。

预验失败不创建目标。复制中失败留下 `STAGING-FAILED.json` 和原有副本，目录不可使用；
修复输入后换一个新目标，不自动清除或覆盖失败证据。成功目录仍写明
`SDK_runtime_verified=false`、`full_M10a2=false`、`Phase7_complete=false`。

Unicode 15.1 数据、官方 Grapheme/Word 测试及完整 Unicode-3.0 许可按固定第三方清单复制；
凤凰 TTF 与 CC0 正文、作者原许可及来源说明一并复制。全项目许可原文单独保留。
此工具不将项目现有有限源代码许可变成新的发行授权，也不替用户作公开发布决定。
SDK 未捆绑 GCC、QEMU 或 Python 本体；使用宿主工具不等于移植这些工具实现。

## 5. 客体开发工作流与示例

暂存目录是部署材料，不会自动进入正在运行的系统。后续由系统维护者在授权安装中放入
对应 `/SYS/INC`、受保护 `/SYS/LIB`、`/SYS/UI`、`/SYS/FONT`、许可和编译器；
普通开发者只向本人可写目录输出程序。保护路径和会话/文件权限没有 SDK 绕过选项。

安装与统一验收另行完成后，在普通用户 Shell 中运行：

```text
/SYS/BIN/SCCC.SCX /SYS/SDK/EXAMPLES/EDIT2.C /HOME/EDIT2.SCX --dynamic /SYS/SDK/EXAMPLES/EDIT2.JSON
/SYS/BIN/SCCC.SCX /SYS/SDK/EXAMPLES/UTILITY.C /HOME/UTILITY.SCX --dynamic /SYS/SDK/EXAMPLES/UTILITY.JSON
/HOME/EDIT2.SCX
/HOME/UTILITY.SCX
```

应用清单只列正文实际使用的 imports，依赖由动态库装载器解析；不把库实现静态复制进示例。
FILE/errno/stdout 宏所需的 SCCLIB 辅助导入同样明列。编译器按正式 `SYS/INC/` 搜索公开头。
编译失败/正文提交/`.map` 和 `.log` 的实际规则见 `SCCC-SCL.md`，本工具不另造编译器行为。

`EDIT2` 有菜单栏、工具栏、查找输入、按钮、状态栏和两个独立窗口；共用一个 UTF-8 Document，
第二视图启用合法字素换行，选择与滚动各自独立，Undo/Redo 是共享正文历史。Open 在完整读取和
严格 UTF-8 验证后一次替换同一 Document；Save 在准备好正文快照/路径引用后写真实 FILE，
写成功并关闭后才 MarkSaved。保存失败保留正文/dirty，但目标文件可能已有部分字节，
不宣称跨文件原子保存。New/Open 遇未保存正文先保留文档并提示保存；最后视图关闭或 Exit
提供真实 Save/Discard/Cancel 对话框，取消或失败均保留窗口。Copy/Cut/Paste 由 SCUI 的系统
剪贴板入口执行，不维护应用假剪贴板。命令状态走正式 COMMAND_QUERY。

界面不含裸 RGB，不自画控件，不复制 Demo/NUI。主题、凤凰文字、DPI、焦点、Tab、caret、
局部损伤及隐藏停画均由 SCUI 的正常消息泵提供；应用没有绘图 timer 或忙轮询。
隐藏后后台文件工作并未被暂停。两个窗口是这个示例的产品形态，不是系统窗口上限。

`UTILITY` 用真实标准 FILE 分块读取、动态 ScBuffer 累积完整行、严格 UTF-8 校验，
每行建立 stable Model item，再经 ModelVisit 生成 typed Config 和 canonical serialize。
`SDK_INPUT` 指定输入文件；不设时使用三行内置示例数据。`SDK_OUTPUT` 指定输出配置文件；
不设时写 stdout。它检查读写/flush/close 与容量错误，释放所有对象；不是用固定行缓冲截断数据。
已完成的前序模型项与失败当前行的候选区分，最终错误退出释放整个模型；文件写失败仍可能留下前缀。

更详细操作、快捷键、资源归属及源码文件见暂存的 `SYS/SDK/EXAMPLES/README.MD`。
后续统一验证至少需要实际 SDK staging、仅公开 INC 的 HOST/客体编译、普通用户实际运行，
以及编辑/文件/剪贴板/双窗/隐藏与正常回收的一轮定向证据。已有库证据不能替代两个新示例的运行。

## 6. 本批实际结果

`build/sdk-native-preview-01`成功暂存79个payload、10865491B，五库/真G1来源、公开include、许可、复制与metadata摘要均通过。暂存本身未运行程序。`tools/check_m10a2_sdk_examples.py`在`build/sdk-public-compile/sdk-public-02`仅使用其公开INC：HOST GCC的EDIT2/UTILITY生成17348/4999B动态SCX，实际SCCC宿主适配器生成25613/7951B；65/36实际imports与清单一致，9.4045秒、exit0、输入守恒。没有执行i386或在客体调用G1。

初次驱动把HOST输出命名为带来源后缀的文件，被正式manifest文件名核对拒绝；只修驱动输出目录和正确i386入口栈编译选项，示例正文未改，失败报告保留。原staging目录/摘要不因后续文档更新被改写，当前新文档由独立检查点保存；最终SDK统一打包时再使用对应实际来源。

## 修订记录

| 日期 | 修订 |
|---|---|
| 2026-10-15 | 新增 SDK staging 源码和两个公开 API 示例；来源、原头字节、许可、失败保留与 preview 边界写齐；未运行 staging、未编译/执行示例，Phase 7 与完整 M10a2 继续。 |
| 2026-10-15 | 实际native-preview暂存与公开INC的GCC/SCCC宿主后端双示例编译通过；客体编译、运行与GUI/回收仍待，完整目标继续。 |
