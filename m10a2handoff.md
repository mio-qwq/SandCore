# SandCore M10a2 — The Application Platform Era：完整开发交接

## 2026-10-17 最终交付接手点

下面2026-10-15的详细记录是历史，不能按其“GUI未验/P5未集成”重新开工。唯一当前事实为HOST21/native11/final12、GUI12闭合及GUI13登录功能/资源边界，详见sandcore/docs/M10A2-ACCEPTANCE.md。本轮没有活QEMU/构建，也没有恢复任何子代理；用户已明确禁止subagent和computer-use。

### 固定输入与结果

HOST21报告SHA738e41d51bc4cd026cdc658c0b8d484614c59e7952b4908be389eaa919a76749；native11内报告SHA66396e2bb8eaf81eefffa02fb4fbaffb23c99a0fddf749ba81559b0d07dad9c0；CORE44d0b635f4f2597ef8009067de0da299bb36270c367589a8898711a64361f6b2。对应build/m10a2-main-session-return-01、m10a2-p6-target-refresh-11、m10a2-p6-final-12。最终SDK从同来源restage到build/m10a2-sdk-delivery-02；不再重编或扩展矩阵。

GUI12证据sandcore/evidence/m10a2-gui12，review SHA3b96f6369e85ca4d7ff98eb631dcbaa0a2b2cb6f5266a314e66dc2b9a9dc0680，快拖/取消/168B持久复制、实际客体DROPUP9项和Installer列宽、正常关闭/owner0。GUI13证据sandcore/evidence/m10a2-session-return，review SHA3fdfa7469a0f099ec78903431e6bbbc04cf7dd6483dca309e6b66f7aea456c0a，真实退出/注销/两次mio空输入登录/root拒绝，原资源FAIL/8页地址归属未直接捕获，驱动由Root CtrlC结束为exit1，QEMU已关闭。不要改旧FAIL或追求全绿重跑矩阵。

### 用户盘与最终包

test保持严格四文件；数据盘已更新SHA362e3ce3ec4df694561443b761270bcae4fd0e13382c066681e10ba91d55a983，保留1626个非更新对象含34个HOME/桌面对象；其他三文件不变，账户库不重置。原盘备份sandcore/build/m10a2-test-session-update-01/sanddata-before.img，update.json保留明细。首次prepare因持有Windows读句柄而os.replace失败，原目标未改；完整准备盘复核后释放句柄完成原子发布。helper现改为build内准备再关闭句柄提交，仅静态审阅此helper修正，没有再次迁移用户盘。

最终Release使用原M10a1只读基盘3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64新建一次干净迁移；不打包用户test盘。SCAPI.H必须保持81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1。SDK staging字段保持自身未运行语义，独立delivery凭证链接实际GUI，不改写旧报告。

### 用户最新授权与发布顺序

用户要求同步文档/SDK，检查完再做密钥验证的commit和Release。原“不自行Release”由此次明确授权覆盖；仍main、不强推、不读SKM私钥、不自动清理用户或旧代理目录。先完成文档/SDK/zip/摘要检查，再推送保留本地历史、用GitHub自动签名接口创建最终提交并实际核Verified，最后发布附件。没有可用用户GPG私钥，不伪造本地签名；GitHub自动签名不涉及用户SKM密钥。

WSL gh网络直连超时，但Windows现有127.0.0.1:7890代理可通GitHub；drafts/m10a2-session-return-01/github_api.py通过WSL gh凭据管道使用现有账户，令牌不写磁盘/日志。WSL→Windows临时TCP中继被拒绝且已停止，不改VPN/防火墙。远端main已读为01da6943c82364c059df8be9a7128e9006ea1c83，本地提交前HEAD c72923c；任何推送必须重新核远端祖先，不强制覆盖。

### 剩余交付动作

检查SDK当前规范与manifest/实际9库、final12发行表/SCAPI、独立干净盘和4文件zip、SDKzip及SHA256SUMS。保存evidence/m10a2-delivery-01的审计与发布凭证。只显式stage本轮拥有的文件；.zcodeignore及defaults-upgrade/image-tools/native-syntax-fix/source-capacity/sdk-final/session2/settings-installer/sound-cover/sound-review/studio2/terminal2/ui-layout-fix未跟踪目录属于旧代理/用户，不能批量加入。签名commit与Release必须以GitHub实际结果为准，未完成不得写成已发布。达成完整目标后再complete。

<details>
<summary>2026-10-15详细历史交接，原文保留</summary>

保存日期：2026-10-15，Asia/Shanghai。工作区：`C:/Users/Administrator/Desktop/projectos`。

**这是一份中断恢复文档，不是 M10a2 完成声明。** 用户本次要求“保存当前状态，写极其详细的 m10a2handoff.md，写完告诉我”，随后明确“别用 subagent，你自己快点写好”。因此本次只核实并保存状态，没有继续实现、构建、编译或启动 QEMU。三个子代理均已被中断；恢复开发时必须先遵守用户届时对代理使用的最新要求，不能自动恢复它们。

本文前半部分解释架构、事实、失败与接续方法；末尾的机器采集附录记录本次实际文件摘要、工作树和报告。若旧 `HANDOFF.md`、设计文档、过期 freeze 或聊天中“当前”“下一步”的文字相互矛盾，**以本文件的实际采集附录与对应原报告为准**，不要覆盖旧证据来制造一致。

## 1. 恢复时先看这一节

### 1.1 最短状态结论

- 正式代号始终为 **SandCore M10a2 — The Application Platform Era**，完整目标没有缩减。
- 已有真正 Ring3 SCL/动态 SCX、自举、SC32、SCCLIB、SCLIB、SCUI 平台/文本/结构体 ABI 的实际证据。
- **SCUI 完整 GUI 验收尚未通过。** 最后一次 GUI12 的原报告仍为 `FAIL_OR_INTERRUPTED`；它不是系统崩溃，而是在隐藏样本解析处失败，且另有真实的 200% 布局画质问题。
- 200% Toolbar overflow、Label 按实际宽度测高与 Demo WRAP 修复已经收齐并通过 **build18 + 两个输入的 SCCC 宿主后端生成**。这两个新产物还没有客体生成或运行证明。
- `refresh04` 与 `GUI13` 尚未启动：本次核实没有对应目录，没有 Windows QEMU 进程。
- Phase5 已有大量真正隔离源码：FS lease、host/WM、SCSHELL 库、SCFILEWORK worker、SCSHELL.SCX 桌面。**尚未正式集成、构建或运行。** 不是只有设计，但也不能称已验收。
- Files2、Studio2、Debug2 和其它主要组件的完整重制还没有完成；SDK 仅有现存公开头示例的宿主编译证据，未完成最终 SDK 客体编译运行闭环。

### 1.2 Git 的真实位置

本次核实时：

| 项目 | 值 |
|---|---|
| 当前分支 | `main` |
| HEAD | `0cd573bb13a13d1b675e4ee8c8d48a3830c1f924` |
| 本地 `origin/main` | 同一提交 |
| 最近已成功推送 | `0cd573b`，本轮中断前已从 `71a98eb` 推至 GitHub |
| 远端仓库 | `https://github.com/mio-qwq/SandCore.git` |
| 当前未提交 | 新 SCUI 修复、活动 GUI/refresh 工具、Phase5 drafts 与部分文档，详见附录 |
| 用户文件 | `.zcodeignore` 仍未跟踪，未改、未加入提交 |

重要的上一批提交：

1. `0cd573bb13a13d1b675e4ee8c8d48a3830c1f924`：修 SCCC `default:`、控件/布局与 publication 已验源码，保存 first05/refresh02/GUI07/09/10 的精确小证据；**没有宣称 GUI 通过**。
2. `71a98eb095e05fd73fa11f49fc3e189194c6f1a1`：现代 F12 路由与公开 SDK staging/examples/宿主编译。
3. `0d78cd4de7816859bb5f50e80f87b37f13cb47d8`：原生 SCUI 平台、G1/结构体/官方文本及早期 refresh 证据。
4. 更早 P3 检查点 `f632708335e358ac1a48424ec161ece6a861a3b7`，清理/接续检查点 `d8e6ed0`。

本次“保存状态”没有擅自提交、推送这些仍在收尾的工作树文件；所有源码原样保存在工作区，逐文件摘要在末尾。恢复者应保留这些内容，不能把 `git reset --hard`、清空 drafts 或重新 checkout 当作恢复步骤。

### 1.3 运行状态

本次通过 Windows 进程表核实，没有运行中的 `qemu-system*`、`qemu-img*` 或本任务的 Python/WSL 主控进程。之前的句柄只作为历史：

| 历史操作 | 句柄 / PID | 已知终态 |
|---|---|---|
| build18 | exec session `65854` | exit0，已取回输出 |
| host-overflow-wrap-01 | exec session `17318` | 报告 exit0，两个 case 都生成成功 |
| refresh03 | exec `34854` / Windows QEMU `21652` | exit0，VM 已关闭 |
| GUI11 | exec `89568` / QEMU `15612` | FAIL，VM 已关闭 |
| GUI12 | exec `38050` / QEMU `14176` | FAIL，VM 已关闭 |
| refresh04 / GUI13 | 无 | 未启动 |

不要拿这些旧 PID 向新进程发命令；PID 可复用。恢复时重新查进程、报告和源摘要。

`get_goal` 本次返回 `null`。用户“直到完全完成 M10a2”的完整授权与范围仍在聊天中，但**不能谎称当前工具里还有 active Goal 记录**。本次只写交接，没有重新创建或标记 Goal 完成/暂停。后续若用户恢复完整开发，先检查工具状态，再按明确的完整目标恢复记录。

## 2. 必须继承的合同与边界

### 2.1 必读入口

- 根 `AGENTS.md`：长期工程、安全、兼容、美学、验证收敛和历史阶段规则。
- 根 `M10a2RULE.MD`：本版本完整目标；用户已明确要求阅读、严格遵守并开工。
- `sandcore/docs/M10A2-DESIGN.md`：总体设计。
- `sandcore/docs/M10A2-IMPLEMENTATION.md`：历史实现/验证来源，**顶部状态表较旧，不能直接当今日待办**。
- `sandcore/docs/M10A2-COMPONENT-AUDIT.md`：现存组件清单与迁移要求。
- 本文、根 `HANDOFF.md` 及各精确原报告。

### 2.2 不得变更的约束

1. **SCAPI.H 原字节冻结**。当前 SHA256：
   `81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1`。
   新能力通过 SC32API 及独立大写 `.H`，不增改旧功能号、寄存器含义或旧缓冲布局。
2. 内核禁浮点、禁 libc；中文注释解释原因。自有公开头全部大写，ISO C 标准头保持标准小写。
3. 主力为 WSL Ubuntu ELF 工具链；实际运行用 **Windows QEMU**。不要改用 WSL QEMU 偷换环境。
4. 原 M8 开发/游戏优化/影片渲染封存，M8a/M9/M10a1 正式基线与用户盘、历史字节保留。不得借 M10a2 默认 `make all` 重启游戏/影片任务。
5. 正式私钥只有用户持有。代理不得生成、读取、保存或代用用户正式私钥；公开公钥、用户已给签名与本机普通内容 SHA 是不同概念。
6. 只允许符合项目许可约定的第三方代码；真实引入源码、版权/许可证及固定来源必须归档 `/SYS/LICENSE`，不是只给一行作者。
7. 单一 `main`，不强推，不自动创建 Release，不删除用户提交。开发提交不得冒充整版完成。
8. 新 UI 必须有真实设计：Classic/Aurora 共同行为、Phoenix 原字形/真实测量、GFX 已登记色槽/Theme24、密集但有秩序。不能以截图数值 PASS 替代看图。
9. 第一批源码/资源/规范收齐再统一构建运行；不逐组件不断开 VM。Phase5 目前仍是隔离收齐阶段。
10. 验证适量。复用未变化且仍有效的来源，不因 CORE 总摘要改变把所有历史结果归零，不扩无价值矩阵。

### 2.3 当前对子代理的最新要求

早先用户指定子代理 **GPT-6.1 Sol + max**，已有三方向均按此执行。**本次更晚的明确指令是“别用 subagent，你自己快点写好”**，本次交接写作已停止并中断它们。不要把旧的可用代理清单视作自动重新开工授权。

## 3. 完整目标还包含什么

M10a2 不止四个 SCL 文件。最终仍须完整做到：

- 真正 Ring3 动态装载、依赖、导入/导出、重定位、生命周期、自动动态 SCX 与 SCCC 工具链。
- SC32API、SCCLIB、SCLIB、成熟完整 SCUI 的公开、稳定、实际可开发合同。
- 真正用户态桌面与 Shell：图标多选/拖动/布局持久化、开始/上下文菜单、任务栏、文件关联/快捷方式、跨应用剪贴板/拖放、标准文件事务与对话框。
- Files2、Studio2、Debug2；Notes、Canvas、Lens、Settings、Monitor、Calculator、Palette、音频、终端、登录/会话、安装器及其它主要组件逐项审查和实质改造，保留已正常工作的功能。
- 新旧程序可区分，历史源码/SCX 保留，旧 SCX 不需重新编译。
- 可公开使用的 SDK，实际系统内编译、动态链接、运行第三方复杂桌面程序。
- 有针对性的真实双盘 Windows QEMU：键鼠、权限、会话隔离、兼容、性能、回收、截图与可复现命令。
- 文档、阶段源码/证据提交和最终实际验收；不能未经用户同意缩减、封存或改名以制造完成。

## 4. 已经成立的平台与标准库基础

### 4.1 P1/P2：真实动态链接

此前已有独立 VA 模块区、页权限、SCL 格式 C/Python 校验、ELF 引导打包、真实 i386 Ring3 装载、函数/数据导入、ABS/PC relocation、依赖/环检测、引用与结束顺序、自动动态 SCX、正常/异常退出回收等来源。优先读：

- `sandcore/evidence/m10a2-bootstrap/`
- SC32 对应证据入口及 `M10A2-IMPLEMENTATION.md` 的历史报告链接。
- `sandcore/user/runtime/`，`sandcore/tools/scl_format.py`、`scd_format.py`、`mkscl.py`、`mkdscx.py`。

不要把宿主算法测试中的 mmap/mprotect/模拟回调称为执行了 i386；该边界在报告中有显式字段。

### 4.2 P3：标准库与通用库

`sandcore/evidence/m10a2-stdlib/` 是稳定证据入口；实际执行过九个标准库模式、七组通用功能共253检查、22B 文件与暖态精确回收。范围是有明确标准边界的 freestanding C32 profile，不能宣传完整 ISO C 或整机帧率。

| 库 | 大小 | SHA256 / 说明 |
|---|---:|---|
| 原生 SCLDR | 57040B | `658b163f910bbe77cb1acf7887d7a7e694342542ec73edd6bdb1c7fc85873305` |
| P3 旧 SCCLIB | 66892B | `9d1d7e308e48ce666343c5b0c922dcba3cddef9685bfa94b4858afe193095ea9`；被后来的 default 修复版本替代，不混用 |
| 原生 SCLIB | 146165B | `2aa76e9695e39c002fa65e9e8777f83f4645b802c906b2465cf5eb70a310fdd5` |

ROOT ABI1.1/v2、12 exports；SCCLIB94 exports/19 imports；SCLIB163 exports/30 imports。ROOT/SCLIB 本轮相关源码没有 C `default:`，因此没有跟着 default 修复重做无关全部套件。

原 build guest05 的旧 `native-libraries` 目录可能已空。P3 来源应读 **`evidence/m10a2-stdlib/native-libraries/PROVENANCE.json`**，不是猜不存在的 build 路径；文件名 `PROVENANCE.json` 大写。

## 5. SCCC 已修复的真实问题

### 5.1 容量与 i386 ABI

SCUI 的实际输入暴露了编译器容量与结构体调用缺口。现有源码已支持实际需要的 aggregate by-value、hidden sret、`RET 4`、函数指针/回调、varargs、SCLMAP 等；两个工具链方向实际共55项检查已通过。

工作区沿用实际收齐后的 heap 容量：SOURCE1MiB、TOKEN3MiB、POOL2MiB、SCL CODE1MiB、MAP128KiB；symbol 曾实测7659，6144不足后仅升为16384。不要再凭估计增宏。后续 P5 真正 CPP/符号/输出尺寸仍要实测，静态 include 字节不是编译通过。

### 5.2 `default:` 被误当普通 label

`user/s3c_parse.inc::statement` 原先在 switch case 逻辑前，先把 `T_ID + ':'` 当 goto label；词法器对 `default` 也用 T_ID，导致：

- switch 分派链没有 default。
- 嵌套两个合法 default 会误报 `duplicate function label`。
- Demo F7 默认命令返回0而未选中 Table。

最窄修复仅排除关键字 `default`，让其进入原 switch 处理。当前 parse SHA：
`1ca3c6ca14879a6d841ae1cffb5ab6417643ce1d8b1dc1fcddbaade931cb8d09`。
emitter 未因这个错误修改。

证据：`tests/m10a2/SWDEFAULT.C`、`DEFAULTAPI.C/.json`，以及 `tools/diagnose_m10a2_switch.py`。
旧 backend 实际失败，新 backend 真实生成 SCX main 机器码六项检查 exit0；这是 Linux i386 adapter 执行，不伪称客体。后续 first05 又由真实客体 G1 生成并运行 SWDEFAULT。

`DEFAULTAPI` 实际检查 `ScErrorName(12345)=="UNKNOWN"` 和 `strerror(12345)=="Unknown error"`，要用新的 SC32/SCCLIB；旧 SCCLIB 字节不能代替。

## 6. first05：完整原生生成与执行来源

原报告：`sandcore/build/m10a2-ui-platform-guest-05/ui-platform.json`。
状态：`DECLARED_PLATFORM_TEXT_G1_FIRST_ROUND_PASS`。
报告 SHA：`5535d2fdb7c6736aa7321d250bf521564055253b319199b6bf85d7f594c7e993`。
执行结束、source/current 守恒。该 VM 中先使用 HOST 五库完成执行，再用真正 G1 生成新库/Apps；**生成的新库并未在该 VM 安装运行**。

| 实际产物 | 大小 | SHA256 |
|---|---:|---|
| SCCC0→G1 | 242638 | `dd7ba9d45fbd1abbafa3f5468dc2d9ee5822625aca052df2e6655945aa302138` |
| SWDEFAULT | 732 | `5c2156ec32142b34e9827a4baba8766201f1e9c184d67c62923ad9c8b2ed0a7e` |
| SC32API | 103863 | `5249c73f40d98d42f992c1add13a113475050552e49a656c6112ad09aabeff46` |
| SCCLIB | 66892 | `2ce2cabb4a601716ccd3a1cf02f4b73fa1752982fb99ceb960ce83a8fb210d1d` |
| 旧 SCUI | 658368 | `0012c12984463fb9592b604f0c108a32d9f306423968bde62be863e7f8a5c619` |
| PLATFORMPROBE | 63265 | `217bba7cab07ea46b27aac7abd73d78f27af8955b8d017484fd4eb25055a31e4` |
| SCUITEXT | 5242 | `8487372def93b0183894586fcfc6278566f395f65b5ca971f4fe25a04adfa637` |
| 原 Demo | 76393 | `05c47e0262834f694e5ad55580057bda3b818ce54de7ce96a3e2fc9d21b334ab` |
| DEFAULTAPI | 785 | `b9379a96045faa53c22828b7b367edde123e9c6cb3646444634e9c2f23777a10` |

G1 load242602、BSS2602680、stack131072。其源码包含 default 修复，后续 refresh 均必须复用这份真实 G1，不重新自举。

first05 实际覆盖：平台87、官方 Unicode15.1 的3013条（Grapheme1187+Word1826）及12948双向边界、两方向55 ABI、实际 SWDEFAULT 与回收。first05 不能替代新 SCUI GUI、桌面或 SDK 的执行。

## 7. SCUI 当前正式工作源与基础合同

当前工作源的 SC32 仍是 ABI1.2/v3、130 exports；SCUI 仍是 ABI1.0/v1、158 exports/137 imports。**P5 拟新增版本只在 drafts，不能混写成已发布。**

现有 SCUI/SC32 包含：

- 控件树、布局、菜单、Document/Edit、虚拟 Provider/Selection、表格/树/列表、PropertySheet/FileDialog、真实字体/Palette、Popup/Cursor、PS2 wheel。
- perSID clipboard TEXT/FILES 与 CAS、Broker IPC/backpressure/EOF/wait、shared physical pages/grants/revocation、真实 Drop 票/sender/hit/可见性。
- `ScUiContextRootNext` 按本 context 的活 root 链枚举；不按裸 ID 扫描。
- Phoenix 原始 TTF、整数栅格和 mask；Unicode完整固定官方来源/许可/oracle。

当前原生 SCUI 各代来源如下：

| 代次 | 真正生成来源 | 大小 / SHA | GUI 结论 |
|---|---|---|---|
| refresh01 | 旧 G1，只刷新 ACL 修复 SCUI | 656796 / `5de54249…5950` | 历史，不作当前 |
| refresh02 publication | 同 first05 G1 | 658552 / `c8141f5d664f07fd48b34edc52a7924dd32ad8381445c4dbbcc3bc6c2be8cdea` | GUI09文本通过，GUI10全选失败 |
| refresh03 selection | 同 first05 G1 | 662584 / `1f38ab0b4510bbb644f4fb809cbae43b9443cf0647fbf3ba07382e798beb5153` | GUI11/12使用；12大量功能通过但整轮FAIL |
| build18 HOST | GCC HOST | 337256 / `14bc70f36ef6140544273831f0487c0fecc125d21e31711c523873516438e2c8` | 不是客体产物 |
| host-overflow-wrap-01 | SCCC实际宿主后端 | 670644 / `08c3186631e733b8e9670cdc1eeb0e7cc60e06a07bd694f2640a0d674edc050a` | 仅生成，不执行；下一次待真实 G1生成 |

refresh03 原报告实际文件名仍为 **`ui-platform.json`**，不是 `ui-native-refresh.json`。报告 SHA：`4b7fb41e2745ed53c48b78d801634305b32e9c55bcc1a8c7081d412e5833b13a`。

## 8. GUI 失败沿革：哪些是代码问题，哪些是主控问题

必须保留原报告的失败状态，不能因为后来解释清楚就把旧 JSON 改成 PASS。

| 轮次 | 实际发现 | 已做处理 / 边界 |
|---|---|---|
| GUI01 | Drop endpoint 指定 ACL_SESSION 却没有 allowed_session，Startup -301/cause-101 | `ui_data.c` 改默认严格 SID/subject ACL、保原 cause、不 Close0；不是放开公共 ACL |
| GUI02–04 | READY 前缀/正则、NODE kind 字段覆盖、UID 换行等主控解析 | 只修 parser，原失败保留；实际平台/文本子结果可按精确闭包复用 |
| GUI05 | 现代 F12 被旧登录入口截走 | `wm.c` 最窄 focused-modern F12 路由修复，已于 GUI06真实通过 |
| GUI06 | F7 不切页；150% Toolbar/副窗 Exit裁剪；Combo巨大空白 | 找到真实 SCCC default bug；控件popup高度与Demo布局同批修 |
| GUI07 | popup 创建后 UI 缓存仍不可见，早到按键误回主窗 | post-ACTIVATE Query出版当前surface；notified_visible与缓存可见分开，不能吞首次VIS |
| GUI08 | 新SCUI文本功能exit0，PF+1却总账FAIL | 精确归属冷 finite-wait heap：count0/cap0/page0→count0/cap32/page1，managed+allocated实测；不是盲豁免泄漏 |
| GUI09 | Popup已真展开，但F12没有snapshot BEGIN | Demo在外层loop迟登记新root，Step可批处理后无限等待；诊断键没有到观察者，不是publication再次坏 |
| GUI10 | 有限真实F12诊断修后，Combo选Aurora/收起通过；Ctrl+A仍1项 | 真缺陷：SelectionSet(0)非法；改真实首尾Range/展开树IDs一次提交 |
| GUI11 | 100000全选与编辑通过，等DROP marker超时 | 图片/原console证实际Drop成功；su stdout管道全缓冲，Demo该printf未fflush。只调整主控用真实F12正常flush |
| GUI12 | Drop/对话框/主题/DPI数值/resize/idle通过，隐藏样本解析失败；另200%真实画质裁剪 | 主控漏SYSTEM prompt和相邻换行；timer源没错。真正布局问题由build18修，尚待新客体证明 |

### 8.1 现代 F12 的内核修复

`wm.c` 当前 SHA：`f22357f64be1b7a1c1fab492981080ef58aa2d25c329be5b18cc4590c7c72bca`。
旧 SHA：`cee84f57f5ba68b89c193ef40bff909e3436f627eaf5f064be24948a2ef874ce`。
只移除 focused eligible modern 分支中的无条件 F12 legacy fallback；没有现代焦点的旧登录 F12 继续保留。

build13 后 CORE 一直未被后续 UI-only build 改变：

- 完整 CORE507056B，payload506928，BSS374600。
- CORE：`a6cabc47fc3d91bda157c19274ad2a999b54af0990484e90bfb529f55d2d8c02`。
- ELF：`b76308a78c073bfe1d037d5fcc2a3dc25c28323f2e677a50d14d24163e61a02f`。
- SYM：`7af2e58a6e642787b0a5475daa2ba982d12a9f989dba385a73853dd41a0ef294`。
- BIN：`4207859b8cd9a456d0c1f1acd0afa138fa632635c4e167d12f3dfad8db452a2e`。

### 8.2 publication 的四文件修复

`ui_tree.c` WindowCreate和`ui_menu.c` PopupCreate成功返回前重新查询 ACTIVATE 后真实surface；private UiRoot 新 `notified_visible` 区分“缓存已见当前值”与“业务已收到VIS”；`ui_dispatch.c` 依据后者发通知。失败仍走原rollback，不把创建中的隐藏候选假标可见。

该修复 build16/refresh02实际通过；build15因缺前置声明失败的原报告保留。

### 8.3 全选修复

工作源：`ui_provider.c` 与 `ui_data.c`，build17只有这两输入和 HOST SCUI 变化。

- 空模型 Clear。
- 平表/List/Table取真实第一/最后 stable ID，调用现有一次提交的 SelectionRange REPLACE。
- Tree动态收集当前展开段的真实ID，保拥有旧段快照，逐callback核provider/selection/view修订，最后一次提交；没有固定rowcap。
- Range及内部IDs提交前检查 selection revision，provider回调合法Clear之后不能被旧候选覆盖。

build17报告 SHA `a05e3e76a478fda829a17b677cec4be134e0ee58d07a110cd3a4694e6266af6a`；106.027774401秒、exit0、changed=[]。
HOST SCUI333068B / `3af7147a79cb476afb862f919cb2995598d65f0e8a304d7739e5554942f68910`。
`host-select-all-01 --scui-only` 成功生成662584B，后来的真实 G1 refresh03生成字节与该后端一致。

### 8.4 诊断日志不是业务输入

`scui_input.py` 只通过 HMP sendkey、QMP真实相对鼠标/按键；读取内存只观测物理鼠标位置等事实，不改窗口/任务内存。

snapshot 总15秒、最多3次真实F12，只接受同count完整BEGIN/END。`wait_logged` 在原20/30秒deadline内用真实snapshot触发Demo正常fflush，仍从原业务输入前console绝对offset读取真实COMMAND/DROP/COMPLETE/SAVED/APPLY。

**不能重发业务 DOWN/UP/菜单动作来掩盖失败。** HIDDEN每次样本本就fflush，隐藏会话不能用F12强行唤醒替代后台timer。

GUI12 HIDDEN真实原文已有两条：round1，tick27323与27523，visible0，paint320、submitted450029368均完全不变。第二条带 `SYSTEM /SYS [SYSTEM] # ` 前缀。旧pattern既拒该前缀，又前后消费换行导致相邻行漏匹配；新pattern用 `(?m)^` 和可选实际prompt。**因此不改timer源码。** 原GUI12整体仍FAIL，最终EXIT/owner收束尚未得到该轮证明。

## 9. build18：最后已构建批次

文件：`sandcore/build/m10a2-ui-bootstrap/ui-build-18.json`。
SHA：`9be0ddfa6a241ebecb9f345582119fd9ab18125b818f7be4ba0c55805a71cae5`。
exit0，48.311311191秒，`source_files_modified_during_build=[]`。

相对17只有下列5个输入变化：

| 文件 | 大小 / SHA256 |
|---|---|
| `user/scui/SCUIPRIVATE.H` | 12466 / `93665c973868ae816391021febf2ee02c93d47884513cfeb5ac8fa1d5e7089ed` |
| `user/scui/ui_menu.c` | 60727 / `6dff97d80f8d39df51d8640cd7a330e10bd12236e5da7e953d7ee5ece0248eae` |
| `user/scui/ui_controls.c` | 56677 / `853f4555af72a086420f7d8ebc39e534dd09894da590b0b6aab222d18595d820` |
| `user/scui/ui_layout.c` | 24348 / `2719565e49b4d76a6f9ec338a06f67f2630f6852bc621209deac98fd13cc6bbc` |
| `tests/m10a2/SCUIDEMO.C` | 68940 / `dc666a7acbb373d0504ac67c1cea5c39f4eedb40902b7cf309aa3ec2dab6f259` |

只有两个产物变化：

- HOST SCUI337256B，`14bc70f36ef6140544273831f0487c0fecc125d21e31711c523873516438e2c8`。
- HOST Demo45060B，`e8e19c000ff931acc6870d0d60c3bedfa9d2af8ce68ba61f7376607722aa6b8a`。

### 9.1 overflow与Label修复究竟是什么

- Toolbar/MenuBar按当前字形/scale量可见前缀，保一枚chevron，不再把最后命令画到客户区外。
- Overflow借原 ScUiMenu 的后缀范围；private `UiPopup.range_first` 是固定下界，`first_row`仍供滚动，二者不能混为一项。
- 不复制 menu entry 的 `context_drop`；原menu保强引用，避免双free/悬垂context。
- 鼠标/键盘都可打开、导航、执行隐藏项，保持disabled/icon/shortcut/原Command与一次UP语义。
- Label自然宽度保留。第一遍候选给实际父布局宽度，第二遍自底向上测换行高度，再重算VBox/flex；中间帧不发布，custom arrange只最终一次。
- Demo只加私有wrappedLabel helper和Resources那一行WRAP，不放大窗口、不删按钮。
- status合理省略可保留；不能把右侧被截断的实际按钮误当合理省略。

### 9.2 两输入 SCCC 宿主预检

`sandcore/build/m10a2-ui-native-host/host-overflow-wrap-01/report.json`：

- SHA `fc3bfa7aa02e30e4038df3b680f9800ea408e258692744547457b12200a71ec8`。
- exit0，总13.269292601秒，仅SCUI+SCUIDEMO；没有再跑ABI/其它Apps。
- `executes_i386=false`、`executes_generated_compiler=false`、`guest_compilation_or_execution=false`。
- SCUI670644B / `08c3186631e733b8e9670cdc1eeb0e7cc60e06a07bd694f2640a0d674edc050a`。
- Demo76741B / `80c0a571284c6a669b80d6cb7365002fac46578acc063b5fe19d79d9640cec94`。
- SCUI实际frontend：types2818、symbols7757、params3028、fixes4575。

这两份是**实际SCCC后端生成**，尚不是实际客体 G1 的新产物。

## 10. 下一步 P4 的准确恢复方式

当前工具已写入 `DPI_OVERFLOW_WRAP_REPAIR` 模式，但因本次保存状态而未运行。先静态核它与 build18、first05、既有原生产物的来源：

- first05仍是唯一完整原G1来源。
- 14→18明确允许9个变化输入；只 fresh SCUI+Demo。
- SC32、SCCLIB、PLATFORM、DEFAULTAPI、SCUITEXT、SWDEFAULT及ABI旧产物按真实闭包复用，不把旧Demo冒充当前。
- 当前public SCUI头/manifest仍158/137；private范围字段没有发布新ABI。
- 新GUI主控应按真实Window PAINT128已清后截图，而非固定等0.3秒。
- HIDDEN解析修正必须按原console实证，不改timer。

构建命令是历史已完成步骤，**源未变不要再重跑**：

```powershell
wsl.exe -e python3 /mnt/c/Users/Administrator/Desktop/projectos/sandcore/tools/build_m10a2_ui.py --tag ui-build-18
wsl.exe -e python3 /mnt/c/Users/Administrator/Desktop/projectos/sandcore/tools/check_m10a2_ui_native.py --build-report build/m10a2-ui-bootstrap/ui-build-18.json --tag host-overflow-wrap-01 --case SCUI --case SCUIDEMO
```

这些tag拒绝覆盖，直接重跑会失败。后续代码若变化才给新tag。

下一次真正运行应在 **Windows Python**，不是 `wsl python`。先检查本机可用Python和当前tool参数，以下为预定新输出路径，只有确认目录仍不存在才执行：

```powershell
python sandcore/tools/refresh_m10a2_ui_native.py --out sandcore/build/m10a2-ui-native-refresh-04 --build-report sandcore/build/m10a2-ui-bootstrap/ui-build-18.json --previous-report sandcore/build/m10a2-ui-platform-guest-05/ui-platform.json --dpi-overflow-wrap-repair
```

工具的相对路径按当前cwd解释；如从 `sandcore` 目录执行，去掉各参数前的 `sandcore/`。它只允许全新build子目录，不能覆盖历史证据。终态报告应为 `.../ui-platform.json`。

fresh编译、get回读、maps、owner与source/原盘守恒都通过后，再调用现有pure `validated_refresh` 检查，不凭进程exit0直接开GUI。随后预定：

```powershell
python sandcore/tools/verify_m10a2_ui_gui.py --out sandcore/build/m10a2-ui-gui-guest-13 --build-report sandcore/build/m10a2-ui-bootstrap/ui-build-18.json --platform-report sandcore/build/m10a2-ui-native-refresh-04/ui-platform.json
```

`--reuse-native-checks`只在现有reader明确验证给定历史报告资格时追加，不能猜GUI12这个整轮FAIL可无条件复用；当前工具只认固定来源/源码闭包。PLATFORM/DEFAULT的真实运行来源为07，它们没有装载SCUI；文本若reader认定当前新模块需新跑，可在同次启动做一次3013+owner，不重复自举/ABI/网络等矩阵。

实际结束条件是：overflow可用、200%文字/按钮不裁、真实已绘菜单/属性图、隐藏同round两样本、正常EXIT与最终owner账、source/原盘守恒。通过后保存最小新证据并提交，再继续P5，不自行扩大排列组合。

## 11. 已归档证据与如何避免膨胀

| 目录 | 已保存的事实 |
|---|---|
| `evidence/m10a2-ui-platform/` | 原first04的G1/ABI/平台/文本、必要失败/产物/截图；76文件约5.46MB |
| `evidence/m10a2-ui-refresh/` | refresh01、GUI02实际native-five子结果、F12前旧CORE/ELF/SYM与来源 |
| `evidence/m10a2-ui-default-publication/` | first05、真实G1/新SC32/STD/Apps/SW、旧与publication SCUI分列、07/09/10原报告/console/PNG |
| `evidence/m10a2-sdk-public-compile/` | SDK公开头双示例GCC+SCCC宿主生成及前次驱动失败，未客体执行 |

最新已提交的default/publication归档为118文件、23,524,578B，160逻辑引用去重到116份正文+README+索引；索引SHA `5a5b3a42d067bc224bc78777acee1039de6621ac6f4dc05bb29e86c11baa7794`。
脚本 `tools/archive_m10a2_ui_proof.py` 逐份copy后核SHA，不复制磁盘，不改原报告。

GUI11/12、refresh03、build17/18与最新host预检还主要在build，后续收尾应追加紧凑归档，不复制整盘/整VFS。

曾尝试清理 GUI01..05 十份已核派生失败盘，总1,349,550,080B，解析绝对路径与范围均已核；**自动审批以“blocked by policy”拒绝 Remove-Item，未执行删除**。不要换删除机制绕过。原盘、当前可用产物、用户文件和必要证据保留；这次拒绝不阻挡开发或写交接。

`git diff --check` 对原始CRLF JSON/console会产生巨大且无意义输出。源码检查用：

```powershell
git -c core.whitespace=blank-at-eol,blank-at-eof,space-before-tab,cr-at-eol diff --check -- . ':(exclude)sandcore/evidence/**'
```

不要为消除lint修改原始日志、Password提示空格或报告换行，破坏逐字节来源。

## 12. SDK 当前实际边界

- `tools/stage_m10a2_sdk.py` 已有真实native-preview01 staging：79 payload、10,865,491B。
- 原生库/G1、公开头、Unicode/Phoenix完整许可、摘要闭包已核。`SCLDR.H`应在 `SYS/INC/RUNTIME/SCLDR.H`，保原 `../SCTYPES.H`；不伪造扁平头。
- 不把 PRIVATE/WIRE/NUI/SCAPI 放进公开SDK INC。
- `examples/m10a2-sdk/EDIT2.C`：双共享Document窗口、menu/toolbar/find/status/FileDialog/UndoRedo/系统clipboard/dirty-close/隐藏停画，65 imports。
- `UTILITY.C`：FILE→动态Buffer→UTF8/Model/typedConfig→序列化stdout，36 imports。
- `check_m10a2_sdk_examples.py` 的sdk-public02用公开INC、GCC `-nostdinc -Werror`和实际SCCC后端，六case exit0，9.4045秒，source unchanged。
- GCC EDIT217348B、UTILITY4999B；SCCC后端25613B/7951B。**未执行这些i386 Apps，未实际SDK客体编译运行。**
- preview使用较早原生来源，不能因为staging目录存在就当当前最新SDK；最终须纳入真实P5公开头/库/工具链，重新按实际版本生成并客体使用。

## 13. Phase5 总体：五个隔离目录

本次实际盘点：

| 目录 | 文件数 | 总字节 | 当前性质 |
|---|---:|---:|---|
| `sandcore/drafts/m10a2-shell-fs/` | 23 | 185255 | freeze2实际FS后端与桥/补丁，未应用/构建/运行 |
| `sandcore/drafts/m10a2-shell-host/` | 18 | 232827 | Root host/WM/可信发行/painter接线候选，未应用/构建/运行 |
| `sandcore/drafts/m10a2-shell-r3/` | 21 | 310453 | 真SCSHELL库正文，尚未最终manifest/doc/freeze |
| `sandcore/drafts/m10a2-shell-worker/` | 13 | 139490 | 真worker正文、manifest与静态来源，未编译/运行；freeze仍需确认 |
| `sandcore/drafts/m10a2-shell-desktop/` | 11 | 131764 | Root桌面/任务栏/service实际正文、候选manifest/Make片段；最近源码变动后静态摘要需重生成 |

这些数是整目录，包含文档/patch/JSON，不能冒充预处理字节或SCX大小。任何 `source-manifest.json`、`static-source.json`、`integration.patch` 都可能比最后源码旧；后续先比较SHA再使用。

正式目标分工：

- `SCSHELL.SCL`：调用者地址空间的公开Shell框架、client/catalog/assoc/menu/dialog/clipboard/drag/job。
- `SCSHELL.SCX`：每普通SID的真实桌面/任务栏host与服务，普通真实凭据，无SYSTEM代办文件权限。
- `SCFILEWORK.SCX`：caller-owned managed worker；独立长IO/计划/提交/结果/恢复，不拖住桌面消息泵。

## 14. FS freeze2 的精确合同

入口：`drafts/m10a2-shell-fs/ABI.md`、`FREEZE2-DELTA.md`、`freeze.json`、`integration.patch`、`integration-inputs.json`。

- freeze2 SHA：`e43a2999dc877c21251a0f7baa9827bf016104e6690339e4c7e0ded7da3bb836`。
- 保留freeze-v1 SHA：`29ce62c76ad69c9817ef9b593471e1a02afaee4bd3098ed9a60515a4129daa04`。
- 公共SC32LEASE.H6950B / `e724f522bdc883df656d4851e7dbe45d1cb40282de05c7fee66ad09c7665f943`。
- WIRE1991B / `60adac3541e003cdd10c6d40e0b1e0f17d1b5d2e188afc5737ea6ca2ec2cdc4c`。
- patch35402B / `589dc116782442be95b642c5d86d1ed98003b1fd4a7bbe7c2479ebbbe3749af9`。

SYS gate0x300、selectors700..70b，error-operationf00..f0b。不要用d00，那里是Shared；e00是Drop；host另用1000族。

12实际入口：Acquire/OpenPinned/CreatePinned/Query/Snapshot/Publish/Rename/Remove/Release/Cancel/Advance/SetMode。

核心记录：Acquire/Open64、Publish/Rename96、Remove48、Control32、Snapshot40/Page32/Row128/Info160；ModeDesc48。最后以头与sizeof静态断言为准。

实现包括动态owner/path/object索引、READ_STABLE/WRITE_EXCLUSIVE/NAMESPACE、真实strong pin、private Create mode3、条件absent/exact目标、object/revision/snapshot_epoch、单调operation_sequence、防重放、v5目录双bank/CRC/superblock提交、uncertain隔离、16sector metadata短步、128sector transfer GC与16page RAM GC。

### 14.1 不可重新引入的缺陷

- Release flags0是真正最终释放，成功后Query/Cancel/Release均STALE；不能留无界tombstone/FORGET。
- 文件Close逻辑解绑及时，strong pin/extent由lease按owner阶段回收，不借复用task槽。
- 目录record在无关条目压缩后必须按实际名字刷新index；NULL/撤销结果不能误认同名重建目录。
- scope READ_ONLY仅允许读；不能借父namespace把READ_ONLY升级为写权限。
- NAMESPACE根R负责遍历，正常snapshot以META过滤可见成员；不可读文件能列出并逐项Skip，不泄不可读祖先目录下的名字。
- 每页发布前复验根R与本页META，撤权不部分发布。

### 14.2 METADATA_ONLY

OpenPinned.flags2、Info.flags256，existing-only：FILE READ_STABLE/open_flags0或DIR NAMESPACE/open_flags0。只Query/Close；Read/Write/seek/flush/DirNext/Snapshot ACCESS，Release后限制保留到Close，不能重Acquire恢复IO，不能作为parent grant。
未枚举目录epoch0/SNAPSHOT清零不是“目录空”。它允许在真实父RW/META/exact条件下改名/删除不可读对象，不凭对象自身R/W制造越权。

### 14.3 SetMode

70b/f0b，Info.operation6。48B：size/version/lease/gen/expected_object/revision/snapshot_epoch/final_mode/flags/reserved3。
只改rw（<=63），不改UID/GID/path/extent。真实owner或既有manage权限，SYS/MOD继续硬件授权；不额外要求fileW/parentW。
目录COPY采用私有3→发布3→children→后序恢复最终mode，不能把preserve-mode留NOT_SUPPORTED。

### 14.4 后台推进

`fs_lease_poll`确实从真实待办链推进`lease_advance_owned`并wake；各批检查保存的真实owner/subject及权限，commit结果通知owner generation的STREAM。不能因为在task0执行就借task0权限。
R3 Store/worker也可用同sequence显式Advance。等待语义必须与真实待办/唤醒一致，不能每16sector无故睡100PIT。

## 15. host/WM 与可信发行隔离稿

### 15.1 新SC32计划版本

SC32拟ABI1.3/v4。FS12出口+host当前11出口需与原130统一合并，预期153；**这只是当前源合同计数，正式manifest/符号提取后再确认**。

hostselectors800..80a，error1000..100a，capabilities_hi bit1（值2）；leasecap hi bit0（值1）只在真实v5可用/未fault时置。

| selector | 入口 |
|---|---|
|800|ScProcessLaunch|
|801|HostQuery|
|802|HostPrepare|
|803|HostActivate|
|804|HostConnect|
|805|HostWorkspace|
|806|HostWindows|
|807|HostWindowAction|
|808|HostRestart|
|809|HostDeactivate|
|80a|HostWindowAt|

LaunchDesc32/Info64、HostInfo96、HostDesc48、Connection160、WindowRow320、Page32、Action64、Point32→WindowInfo96。桥使用SC32私有gate，不扩SCAPI。

### 15.2 启动与权限

- `ScProcessLaunch` path/arguments分离，1023B现代参数，真实普通SID/UID/GID/normal realm，环境/CWD继承；不创建job、不继承隐藏stdio writer，子进程活到自身退出或SID注销。
- 旧SPAWN仍受管，旧`task_exec_scx`保原空格切路径、127B参数及原auth顺序；真正装载抽为parts，旧168B task.args仍127B前缀，新ARGUMENTS2完整值。
- task0只可从固定 `/SYS/SHELL/SCSHELL.SCX` 以该普通会话真实身份启动host，在子任务尚未调度前绑定PID/gen。UID、文件名、IPC名字都不能自封角色。
- `/SYS/SHELL`拟加入保护，不能只保护LIB而让普通用户换worker/host。
- 普通root/SYSTEM/Ring3均不能借角色取得外部硬件SYSTEM管理权。

### 15.3 首帧与生命周期

HostPrepare绑定本人两个不同SC32 surface和本人LISTENING endpoint。PREPARING位8192，保原Popup1024/NoActivate2048/Transparent4096，不复用冲突值。
不可见准备surface仅可完成有限真实首帧：同geometry完整Submit+匹配PaintEnd，才允许原子Activate；不能只改visible位或包围框冒充完整帧。

角色state：OFFLINE0/STARTING1/PREPARED2/ACTIVE3/RETRY_EXHAUSTED4/STOPPING5。
正常清理先809 Deactivate撤角色/endpoint、保PID/gen到真实EXIT，再关service/Context/return；Restart在本人尚未EXIT时BUSY，不提前task_stop打断fini。异常丢surface/OOM detach→stop→有限重启；注销不重启。最多4次连续短命失败，稳定60秒后恢复预算；不能每tick扫所有SID或无限重启。

WM不改旧80B win_t；动态私有尾部承载role/祖先/帧等。desktop底层、taskbar专层、普通窗与popup层级真实参与合成/命中；隐藏不画，普通后台业务继续。

### 15.4 窗口模型

- 分页只同SID获准窗口的值，无canvas/procedure/内核指针。
- 整次调用64检查预算，不是每行各64；一个320B内核局部row，空过滤页推进cursor。
- 变化通知只wake绑定host的TASK_EVENT_VISIBILITY。源码最后静读确认`sc32_gui_stamp(w->owner,m)...`所插通知位于 **gui_close_request**，不是每次鼠标输入；不要误判后删掉必要close通知。
- WindowAction用window/gen+PID/gen核对，focus/minimize/maximize/restore/place/close-request，尊重模态、disabled、CLOSE veto；close不是kill。
- 80a只给真实ACTIVE可见同SID host查询实际WM hit/z序，同SIDnormal值，不铸Drop票；用于把桌面layout手势转为跨窗FILES拖放。SYSTEM/外SID/隐藏SID拒绝。

### 15.5 可信映像摘要不是私钥

`tools/mk_shell_issue.py`消费真正动态、依赖SCSHELL.SCL、无尾icon且<=262144B的SCSHELL.SCX，生成CORE内精确非零SHA/bytes。
固定受保护路径不足以证明旧盘上同名文件是正式发行；loader在唯一scx_stage已读完整payload后、pd/spawn前核同一内存映像，再映射相同字节，不hash后重读另一个版本，不设全零通配。

最终若用客体G1的SCSHELL.SCX，必须显式`SHELL_HOST_IMAGE=<已核原生文件>`重生成header并重建必要CORE；HOST与G1机器码可能不同。安装盘必须放同一原生字节。切换输入时显式刷新header，不能只赌mtime。该流程目前未执行。

### 15.6 P5 SCUI候选能力

仅draft patch，拟SCUI ABI1.1/v2、162 exports（原158+4）：

1. PainterClear：当前clip的ARGB透明，没有新RGB色号，复用内核唯一壁纸缓存。
2. PainterTextColor：临时选择已有24 Theme角色，调用后恢复，不污染后续控件。
3. PainterBounds：返回当前damage本地clip，自绘只查询相交cell。
4. ContextWaitTimeout：真实timer/drag下一期限与caller最大等待的交集，混合GUI/IPC泵不猜固定高频poll。

公开SC_UI_TEXT_*名字沿原实际flags值。当前P4工作库还没有这些入口，不能加载P5桌面并期待成功。

### 15.7 接线脚本仍需重生成

`make_patch.py`、`make_wm_patch.py`只生成review diff，不改工作源。Root后来改了WM预算、80a与新UI接口，当前P4 private头又已变到build18：**旧integration.patch/source-inputs/wm-source-inputs不能盲应用**。
先按当前文件SHA核基线，重生成两个候选diff，人工合并FS/host/WM共同改动，再统一更新caps/version/exports/Make/包/SDK。不能依次apply覆盖同一task/fs/SC32字段。

## 16. SCSHELL.SCL 库正文

位置：`drafts/m10a2-shell-r3/`。正式拟 `user/SCSHELL.H` 与 `user/scshell/`；WIRE/PRIVATE不入SDK。
聚合`SCSHELL.C` include职责`.c`，不能再分别编译成重复TU。

已有真实正文：base/catalog/store/protocol/client/service、legacy、launch/shortcut/icon、job/clipboard/drag、menu/provider/dialog、finalize、分步store。没有正式SCSHELL.json或最终freeze，尚需收尾。当前目录21文件310453B；此前静态literal include闭包39文件、295宏、原始336630B，对当时真实SCCC FILE64/MACRO512/SOURCE1048576够用，但最终编译仍待。

### 16.1 Catalog/持久层

SYSTEM0/ACCOUNT1/SESSION2；app/verb/assoc/layout均动态，严格expected_epoch CAS，0不是忽略，空catalog epoch1。
SYSTEM种子只能本地trusted host显式装入，远程不能写SYSTEM。ACCOUNT保存只输出ACCOUNT+layout，Load严格拒绝磁盘SYSTEM/SID冒充，同时保当前SYSTEM/SID并合并账户候选。SID项由真实sender PID/gen绑定，EOF不伪装进程退出。
`ScShellCatalogStoreRevision`给最后真实Load/Save确认revision，不盲读META接受外部新版本再覆盖。

LayoutCreate的item/id去重已改双Hash，file item去重也用Hash；不保留每次拖动保存平方扫描。Start按category '/'动态分类、shadow/hash/排序，不用旧32/24数组。

### 16.2 协议

唯一`SCSHELLWIRE.H`。48B header只放值/offset/bytes，不传foreign pointer/FILE/lease。
服务0x100族：HELLO100/ACK101、QUERY110、REGISTER120、UNREGISTER121、ASSOC122、LAYOUT123、CHANGED131。Worker0x200族另列下一节。
HELLO seq0，后续双字单调不复用；真实Packet.sender/Peer匹配PID/gen/SID/UID/GID/subject/normal realm与flags0，payload自报无效。
每帧含头<=65536，大body连续offset分片，不重叠/跳跃/重放；字符串唯一NUL+4B零padding；背压保拥有frame重试，Receive先Peek/完整分配，不因容量失败消费。
SCAT文件64B头+CRC32+版本/epoch/count/layout，App112/Verb64/Assoc48/Layout48。CRC不授身份或持久保证。

### 16.3 服务与消息泵

ServiceDesc48接真实catalog、借用listener、host_epoch、budget、account_store与callback。Create核真实HostQuery PID/gen/SID及LISTENING endpoint，不能任命角色。Close只关owned接受channel，不替Root Deactivate/关listener。
最多64帧/轮是公平预算，不是clients/注册总量上限。ServiceInfo.flags READY1只在本次尚未检查完或可推进store时置；空服务扫完清0，避免消费STREAM后漏已有第65帧。
远程ACCOUNT/Layout commit已改私有分步store，不能同步CatalogSave把整个配置写完才还UI；候选真正Publish后才ACK/替活动catalog。

### 16.4 分步Store

四入口：StoreBegin(catalog,path,expected_revision,**)、StoreStep(store,timeout)、StoreQuery、StoreClose。
Info48：size/version/state/status/expected_revision/new_revision/catalog_epoch/bytes_written/total_bytes/stage/flags/reserved。
state WRITING1/PUBLISHING2/COMPLETE3/FAILED4/CANCELLED5/UNKNOWN6；flags DURABLE1/READY2/WAITING4。
Begin拥有完整快照，保存catalog_epoch；写16KiB或推进一个阶段，旧快照在发布前遇epoch变更须STALE，最终commit期间catalog变更BUSY。只真实Publish更新revision；uncertain不能ACK，Close不回滚已发布数据。
元数据PENDING兼READY|WAITING避免无故长睡；确切权限/后台推进合同见FS节。Release/Advance/Close也拆步，最后成功不能再隐藏while清理。

### 16.5 READY3与对象寿命

公开正状态END1/PENDING2/**READY3**。
JobStep/MenuStep/ClientStep在budget后只对本人真实READ/ACCEPT/closed或足够下一完整frame的WRITE返回READY。写minimum按48+下一片长度，不能只余1B就忙醒。等worker/用户结果窗仍PENDING，Menu无async/终态END。
菜单popup消失后Root把ShellMenu放动态work链，继续Step，END后Release；Release是明确取消未完成owned工作，不能因点开新菜单杀正在复制的文件。

### 16.6 shortcut/图标/legacy

SHORTCUT：LEGACY1/APPLICATION2/EXECUTABLE3/PATH4；PATH要求profile0/空args，Launch走CatalogOpen文档/目录关联。
ScShellOpen对PATH→LNK链用动态visited Hash防环，没有固定32层。
旧LNK三行/SMENU保原CR忽略/尾空行与根路径命令语义；rootless历史exe补绝对根路径，参数保原；固定worker Spawn必须 `/SYS/SHELL/SCFILEWORK.SCX`，不能受Files CWD影响。
真实图标读取SCX36B头及合法尾icon，不搬完整代码；@走ThemePath，literal沿旧root spec。资源cache有预算（当时默认512KiB），theme/display/path revision失效。Root桌面不能强持所有隐藏项把cache预算架空。

### 16.7 剩余收尾重点

- 最终manifest/imports/exports/Make/SDK、source/freeze与文档尚未最终落齐。
- 复核Store异步与service candidate/base epoch锁、Close/fini两阶段与共享对象引用，不因最后清理出现UAF。
- 保最终通知错误与真实文件job态分开，observer失败不能改已Durable结果。
- 进度主文案改真实状态名和可读字节量，不能只给State整数/高:低字对；原因码可次级展开。
- 当前源与旧静态摘要/合同末行可能不一致，先用附录SHA定位，再做完整源收齐与编译。

## 17. SCFILEWORK.SCX worker

位置`drafts/m10a2-shell-worker/`，正式拟`user/shellworker/`。公共头在user，唯一WIRE从`user/scshell/SCSHELLWIRE.H`借用；private头不进SDK。

本次已见8个实际C职责文件+PRIVATE.H：主SCFILEWORK、lease、plan、protocol、recovery、retry、transfer；另manifest与INTEGRATION/RECOVERY/spec/static-source。
最新静态记录69 imports，无missing/extra、无缺public/private定义或literal include；23文件/178590B、宏保守251。**仅文本/静态来源，不是CPP/语法/链接/SCX或执行PASS。**

真实已写：

- caller创建SESSION listener→固定绝对worker managed Spawn(NO_STREAM_INHERIT)→JobQuery childPID/gen→只Accept该worker；worker反向核真实caller peer/SID/subject，不能凭args信人。
- PLAN/COMMIT值协议、动态item/hash/排序、64KiB真实IO、ReadStable源、privateCreate目的候选、Flush、条件Publish、成功ACK、同卷整树Rename、后序Remove/SetMode。
- overwrite的private候选不占已存在target名；用独立可写recovery父目录里的动态唯一短名，未公开直到EXACT Publish，不能先删target或借SYSTEM temp。
- MOVE仅在真正dest durable ACK后条件删源；源删除失败明确source retained；uncertain无falseACK，不回滚已发布文件。
- 显式replace/skip/rename/merge/retry/cancel、逐项结果、CRC恢复ledger、取消/Close/exit、权限与foreign pin。
- 目录初始snapshot失败/不可读恢复后的Retry用新本人规划轮次，只加未提交成员，已完成事实不重放；已发布目录只重试余下mode/remove。

WIRE现有Header48/Begin64/Item48/Progress96/Ack232；COPY1/MOVE2/DELETE3/RENAME4/MKDIR5。Progress PRIVATE_PREFIX表示尚未公开拷贝量，不能冒充已提交字节。
恢复日志需先真实Flush后才做MOVE删源，RecoveryWritten与文件Durable分列；CRC不是签名，不自动重放日志里的删除/执行。

剩余：最终freeze/依赖SHA对齐，真实GCC/SCCC代码与SCX262144B预算测量、正式可信安装/owner闭包、普通SID执行/权限/readonly-dir最终mode/uncertain/回收与性能。不能因约100KB源估计输出一定装得下；若实际超限，应按真实规模拆真正动态engine库或设计新版装载，不能删功能换过编译。

## 18. Root 的 SCSHELL.SCX 桌面正文

位置`drafts/m10a2-shell-desktop/`，6个实际源码文件：`DESKTOP.H`、`SCSHELL.C`、`desktop_model.inc`、`desktop_view.inc`、`desktop_tasks.inc`、`desktop_actions.inc`。聚合inc是**应用自己职责拆分**，不是把SCSHELL/SCUI库正文静态复制到应用。

另有`SCSHELL.json`候选、`make_manifest.py`、`source-manifest.json`、`DESKTOP.mk`与README。最近静态候选142 imports；后续源码改动后摘要可能旧。正式公开头是 **user/SCSHELL.H**，Make依赖已改正，不能误用user/scshell/SCSHELL.H。

### 18.1 已写真实能力

- 核HostQuery与自身PID/gen/SID/normal realm，真实Context/两窗口/SESSION endpoint/Prepare/首帧Pump/Activate/ServiceCreate；正常先Deactivate再清理。
- 自定义SCUI Class绘Desktop与taskbar，根布局显式OVERLAY让AUTO canvas填满；不能沿默认ABS导致0尺寸。
- 只用公开库，无SCAPI/int桥/NUI/private内核结构；颜色走Theme角色，背景透明复用旧壁纸，不复制一份全屏背景。
- 动态图标、空间cell hash、路径hash、任务候选与菜单work链；无24/32图标/菜单总数上限。
- 图标单选/Ctrl切换/Shift范围、框选、方向键/Enter、双击、F2、ShiftF10、CtrlShiftN、复制剪切粘贴；F6在Desktop/Taskbar间换焦点，焦点内CtrlEsc开Start。
- 只真实左键边沿开始/提交，capture丢失/输入epoch变取消；拖动候选直到UP，组位移保相对位置，网格按共同delta吸附。
- WM真实WindowAt区分覆盖窗口，不以创建顺序猜hit；跨窗或Alt启动真正FILES拖动，LNK传自身文件路径，不搬目标。
- Desktop/folder Drop明确“复制/移动/创建快捷方式”，目标高亮、body拥有复制、不让borrowed IPC指针活过对话框；DropComplete不当磁盘ACK。
- 分页8条目录候选，完整/布局/索引成功才替活动模型；失败保旧，普通STALE重取不反复弹错。
- 只在Paint外每轮4图标读取、只强持视口资源，离屏释放，SCSHELL另有预算cache；隐藏暂停图标读取/重载，后台service/job继续。
- 逻辑96坐标持久化、自由/网格/自动排列、水平/竖直视口、焦点自动滚动、Shift滚轮横向；保存不会把更小显示下的视口裁剪坐标写回原位置。
- 同SID窗口每轮8行完整候选换taskbar、滚动overflow、正常focus/min/max/restore/close-request、cascade/tile、键盘任务按钮导航。
- 旧Files/Studio/Debug/Note/Lens等真实路径种子与旧类型路由；未知二进制不自动当文本。路径已修为DEBUGGER、NOTE、BIN/SHELL等真实旧入口。
- PATH/程序/登记应用三类新shortcut属性表，参数/profile/图标字段；LNK Rename只改显示label；普通rename/move的真实Durable结果迁移布局身份，delete/move-out从新布局剔除，不能把已删身份留给同名重建。
- 文件选择保存真实observed revision，后续操作带EXPECT_SOURCE，不用裸路径假作birth ID。
- 桌面布局Store异步保存；底栏“未保存”仅当前layout serial真durable完成后清。外部配置冲突给保留画面不保存/Reload，不能盲重读META覆盖其它SID。
- 保部分失败/取消/uncertain/源保留进度到用户Close，全成功才自动收起；菜单消失不杀owned job。
- 查询真实下一GUI期限，service/store/job/menu真实READY才Yield继续，不永久1tick poll；RTC按PIT约1秒查询，分钟/状态未变不重画。
- username显示代替裸SID；旧标题/旧文件名不完整UTF8只作显示替代，不改真实路径与窗口身份，避免单坏标题杀整桌面Paint。

### 18.2 Root最后未完成的静态收尾

1. **不要立刻构建P5。** 先收齐library/worker最后manifest/doc/freeze，并完成Root桌面静态审阅。
2. Root最近加入`removed`、`item_index`和case-fold path key、异步Store/READY、job terminal UI寿命；需要专门审读这些接线，而不是只看曾生成的142 import JSON。
3. 路径hash按FS的ASCII case-fold，仅是索引；真实路径/显示名分别保留。检查rename overwrite替换目标的removed/icon/focus、delete后的空间索引skip和完整刷新交换map的rollback。
4. `desk_index`候选map/cells必须OOM保原；`desk_refresh`已保存old_names/cells，失败恢复，成功才Release旧。
5. namespace/job结果callback虽来自库已验真实peer，UI observer失败仍不能改真实job结果；资源OOM时应提示并重取模型，不伪称文件回滚。
6. 当前菜单work链每轮逐项Step，后续按实际数量/成本检查是否需有界round-robin，不能引入固定总作业数；不凭小夹具宣称大桌面性能。
7. 继续核Store并发catalog epoch、layout_save与refresh顺序、新动作覆盖原UI意图、回调重入与modal边界。
8. 快捷方式、目录路径上限、属性表高DPI/窄窗、Root自定义canvas交互的真实视觉/键鼠都未验；不能从源码称成熟桌面。
9. 最终P6默认入口切到真正新Apps时更新种子/关联；当前兼容旧入口不是Files2/Studio2已做。

## 19. Phase5 统一整合顺序

1. 冻结所有隔离源，更新静态清单但不先构建单组件；核最后公共头/唯一WIRE与全部imports。
2. 按当前build18后的工作源重生成FS/host/WM接线diff。FS patch只改10个既有文件，新源另拷；host patch有新files与19既有变更；WM patch改wm/wm_native/GUI头，新inc只拷一次。
3. 人工统一task/fs/session/main/streams/SC32聚合、owner停止/cleanup、version/cap/exports，不顺次覆盖。旧缓冲与SCAPI raw bytes保留。
4. 正式放`user/SCSHELL.H`、`user/SC32HOST.H`、`user/SC32LEASE.H`与各private职责目录；worker只借唯一WIRE，SDK只公开H。
5. 补真正SCSHELL.SCL/SCFILEWORK/SCSHELL.SCX的Make、安装、native输入闭包、`/SYS/SHELL`保护、源码/许可归档、SDK staging。
6. 明确可信host构建依赖图：CORE issue header依赖实际SCSHELL.SCX，不能产生循环或默认放行。HOST bootstrap与最终G1 host摘要分开。
7. 一批统一WSL构建与实际SCCC后端预检，记录真正source/token/pool/code/load/BSS/stack；不碰默认全M8目标。
8. 单一Windows双盘profile实际G1生成新SC32/SCSHELL/worker/desktop，按来源装载执行；最后CORE固定同一原生host字节，必要重建CORE而非重跑所有旧平台。
9. 只做必要新合同：host任命/伪装拒绝、普通/SYSTEM/SID边界、两surface首帧/恢复、detach/正常fini、lease权限/META/readonly目录最终mode/条件覆盖/uncertain、长copy消息泵、真实桌面键鼠/布局与回收、Class/Aurora/显示边界。
10. 通过后截图+日志+变更清单，更新README/对应规范/修订记录与handoff，提交；自动继续P6，不把P5说成整个M10a2。

## 20. Phase6/P7 仍未完成的应用与工具链

设计入口：`FILES2-DESIGN.md`、`STUDIO2-DEBUG2-DESIGN.md`、`M10A2-COMPONENT-AUDIT.md`。

- Files2：树、虚拟多列表、排序列宽、多选、复制移动/冲突/进度、剪贴板/拖放、地址/工具/状态、搜索过滤、关联和属性。旧files.c512行单选不能冒充。
- Studio2：多文件/项目树/语法高亮/查找替换/UndoRedo/编译输出/错误导航/停靠/集成调试/SCL/SDK。当前SCCC还缺真正多TU/object linker与statement SDB工作流，不能用单TU成功冒充工程构建。
- Debug2：真实受控目标、disassembly/register/memory/stack、多个断点、模块/运行时符号/地址导航、与Studio联动。旧64word CPUINFO按PID>=8下标越界，不能搬过来或扩旧ABI掩盖。
- Notes/Canvas/Lens/Settings/Monitor/Calculator/Palette/音频/会话/终端/安装等逐项保功能。Canvas作品原分辨率不能被窗口缩放回写；Lens保原图/候选失败/权限；音频隐藏继续流；Settings保15秒显示Keep/Revert和VGA；Monitor使用真实分页/采样不是假0%。
- 终端要真正字节流/历史/全屏CLI/原nano/scdbg管道工作流，不把无stdio的detached SH当成可用图形终端。
- 安装目前原CLI，不可宣传已有成熟GUI安装器；后续要真实包/事务/权限。
- 历史Mines/Lumen/Race/World及探针清单保留，探针不能计作完整桌面软件，M8游戏内容/昂贵渲染仍封存。
- 最终SDK要真实普通会话内编译链接运行复杂示例，并交可复现入口、完整许可与边界。

## 21. 验证与性能纪律

没有真实证据不得宣布完成，但也不得无限扩测试。每一轮先说明具体未解决问题、必要性和结束条件。

- 代码/资源/依赖没变就沿原报告复用，保旧PID/PIT/CPU/页账来源，不改成“本VM刚测”。
- HOST生成、SCCC宿主后端生成、客体G1生成、装载执行、GUI行为、画质、整版验收分别记录。
- 100Hz不是FPS，QEMU宿主CPU%不是硬件瓶颈证明；实际PIT/CPU/帧提交/输入长尾/像素/页回收分开。
- 隐藏必须停画/停无效图片生成，不能暂停CLI/audio/network/worker整个会话。
- 索引/cache须有真实失效与归属；不能强持所有隐藏图片让cache预算形同虚设。
- 不降渲染尺寸、删效果/玩法/功能或假造空闲数；GUI图标缩略尺寸是设计，不得借此改原图片/游戏分辨率。
- 新profile优先只读基盘+COW；若prepare确需raw离线字节编辑才保必要raw副本。不得为每个同字节重试全量复制/冻结整树。
- 截图须首帧真正EndPaint后看图，数值布局PASS不能掩盖裁剪。原prepaint白popup图可留作失败证据，不能作验收图。

## 22. 常用恢复命令与Windows边界

仓库根为PowerShell，编译工具放WSL；Windows实际GUI主控不要套WSL运行。

```powershell
git status --short
git log -3 --format='%H %s'
Get-FileHash -LiteralPath 'sandcore/user/SCAPI.H' -Algorithm SHA256
Get-CimInstance Win32_Process | Where-Object {$_.Name -match '^qemu-system|^qemu-img'} | Select-Object ProcessId,Name,CommandLine
```

原成功推送使用WSL git/GH认证环境；最近实际push命令为：

```powershell
wsl.exe -e env HTTPS_PROXY=http://172.27.160.1:17890 HTTP_PROXY=http://172.27.160.1:17890 ALL_PROXY=http://172.27.160.1:17890 NO_PROXY= git -C /mnt/c/Users/Administrator/Desktop/projectos push origin main
```

这是当时已成功的本机代理地址，不是永久配置；若网络环境变化先核当前地址。WSL的UTF16 localhost代理提示曾是无害警告，不能凭它说push失败；以git exit和远端提交为准。不要打印凭据/私钥。

Windows文件操作使用同一PowerShell的LiteralPath。递归删除/移动前先核绝对目标落在授权目录；本轮没有新清理授权，也没有实际删除。

## 23. 接力者最容易犯的错误

1. 从旧HANDOFF顶部或M10A2状态表重启已经通过的first04/05整批。
2. 忘记原报告文件名refresh仍叫`ui-platform.json`，猜错路径。
3. 把build18 HOST、host adapter670644/76741B当成已客体G1生成。
4. 新Demo源已变，却只刷新SCUI后安装旧Demo。
5. 覆写原FAIL JSON，或以图片里的drop文本替代真实sender/ACK核对。
6. 重新改正确的timer以“修”隐藏regex，或给隐藏桌面发F12制造样本。
7. 把旧平台/文本所有结果一概归零，或反过来把旧SCUI执行冒充新库。
8. 忽略Toolbar overflow/Label实际宽度，用更大窗口/删按钮掩盖200%缺陷。
9. 盲apply过期P5 patch，覆盖共同task/fs/SC32 owner接线。
10. 将draft声明/manifest/静态include记录当成实际内核/三环实现通过。
11. 同时开多个QEMU，或沿旧session/PID假定还live。
12. 把worker DropComplete/CUT意图、CRC日志或私有copy prefix当durable ACK并删源。
13. 通过same UID/文件名/IPC名字授host角色，或把固定路径上旧用户文件当正式host。
14. 把menu popup关闭等同job寿命结束，把失败结果dialog自动销掉。
15. 标Goal complete、封存/缩减M10a2、创建Release，或自动重启M8任务。
16. 忽略用户本次“别用subagent”，自动恢复三代理。

## 24. 本次保存的范围

本次只读取当前状态与源码，写本文件及其下方实际摘要附录。未执行新的构建/编译/测试/QEMU/删除/推送，未修改项目实现，未更新用户记忆目录。

后续开发按用户新的继续指令进行；第一件事是核本文SHA附录与工作树是否一致，随后接续build18之后尚未运行的最小P4原生/GUI收尾。只有整个合同及必要最终证据真正达到完成定义，才能宣布M10a2完成。

---

## 附录 A：本次实际采集的工作树、报告与文件摘要

以下附录由本次保存脚本直接读取工作区生成；只追加本文，不改被采集文件。任何摘要清单都不是运行PASS。


</details>


修订：2026-10-17，统一HOST21/native11/final12、GUI12与登录回归实际范围，SDK及发布入口同批整理。
