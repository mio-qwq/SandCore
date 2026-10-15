# SCSHELL 实施状态与源码交接

所属版本：**SandCore M10a2 — The Application Platform Era**。2026-10-15。完整设计为 [SCSHELL-DESIGN.md](SCSHELL-DESIGN.md)，目标为根 `M10a2RULE.MD` §6及相关系统库/主要应用合同。**此次仅写这两份文档，未修改实现、构建规则或当前验证输入；没有新增构建、宿主编译或QEMU。Phase5 尚未实现/验收。**

## 1. 当前真实证据及不能外推的结论

| 已有工作 | 本次可引用的事实 | 本次不能宣布的结果 |
|---|---|---|
| Phase1动态运行时 | ROOT/SCL与自动动态SCX已有独立实际运行、客体编译、异常/退出和多进程证据 | 不代替新桌面或SCSHELL |
| SC32文件/标准流、SCCLIB/SCLIB | 已有分别绑定源码/产物的Foundation与标准库/utility证据；旧ABI与旧端点保留 | 不等于Shell文件事务、全GUI或完整M10a2完成 |
| `ui-build-04.json` | exit0、source_modified=[]，45.7486秒；CORE full507120B/payload506992B/BSS374600B，SC32API138410B/130exports，SCUI328844B/157exports/137imports | 当前ContextRootNext等后续源不能用build04字节冒充已构建；后续build由主代理记录 |
| `ui-platform-guest-01/ui-platform.json` | 真实普通su mio：SID4、父PID6/gen1与childPID5/gen2、UID1000/realmNORMAL/flags0；87平台功能检查实际PASS | 该轮整体仍FAIL，不能改为PASS或称SCUI成熟 |
| guest01共享/资源 | parent PTE89→125→89、PT4→4；运行0.781秒/QEMU一核CPU0.671875秒；br/drop/window/file/job/pipe对象归原，Font952/Desktop3818/records5页不变 | PF9007→9008，旧wait_pages0→1归属未在该轮测完整，不能追溯补证 |
| 下一轮主控 | `verify_m10a2_ui_platform.py` 已修准确读取wait_count/capacity/heap/pages/PF归属并冻结；SHA `f162c98c998c167a92796ca0269b281ff92bb3805fdc42cc7ed9ddafd14f4c36` | out02未运行；官方3013个边界oracle、该轮G1/SC32/SCUI生成执行与实际GUI尚待主代理继续 |

源码审阅发现旧task有限等待heap在最后项撤销后刻意保留一页缓存。新driver只对实际count0/capacity32、aligned有效managed且allocatedPF的冷0→1承认这份冷成本，其它增长仍FAIL；该修正不是内核泄漏修复，不改变guest01原FAIL。下一轮现状以主代理实际新报告为准，本文件不预写结果。

## 2. 审阅入口与保留清单

本次读取根AGENTS/M10a2RULE及下列实际源码/规范；不是从M8的旧状态文字推测现能力。行号随主代理后续源改动变化，入口以文件与函数为准。

| 源码/规范 | 对应内容 | 必须保留的结果 |
|---|---|---|
| `kernel/desktop.c/.h`：menu解析、link加载、icon/wallpaper/poll | perSID资源/候选/权限；旧32菜单/24图标/32缓存 | 原用户菜单/LNK、壁纸、图标与有效快照；新活动容器动态 |
| `kernel/wm.c`、`wm_native.inc`：desktop view、taskbar、native_mouse/context | 动态窗口/会话、taskbar滚动、旧Start/context、网格按下执行 | 旧窗口/输入/聚焦/几何/关闭、模态、VGA与会话隔离 |
| `kernel/session.c/.h`、`SESSION.md` | 普通登录真实SID；同UID分离；配置路径和注销 | 源PID/gen、普通/SYSTEM/锁屏边界、隐藏停画/后台继续 |
| `user/files.c`、`FILES.md` | 512行单选/NUI、导航分页、mkdir/rename/delete、exec/IDE/Debug/Lens分派 | 完整UTF8身份、取消保原、真实错误、紧凑布局及全部原操作 |
| `user/settings.c`、`CONFIG.md`、`USERSPACE.md` | 六页、配置草稿、候选校验/保存、主题/壁纸、显示预览 | 坏值保原、环境未知项、15秒Keep/Revert、菜单/shortcut编辑 |
| `user/shell.c`、`user/m9/sh.c`、`SHELL.md`、`CLI-M9.md` | 历史GUI Shell与现字节流Shell、语法/作业/stream profile | 全部已支持语法、stdout/stderr/exit、default spawn、串口正常权限 |
| `assets/desk/*.lnk`、`assets/m9/desk/shell.lnk`、`gen_m8_assets.py` | label/command/icon与menu种子；新Shell入口BIN/SH.SCX | 数据原字节、已发布旧SCX及历史来源不删、不整树复制 |
| `kernel/fs.c`、`sc32_file.c`、`streams.c`及`FS.md/SC32-FILE.md` | 原v5布局、随机IO/保持打开flush、pin、owner、原std weak proxy | 旧16fd/事务，64KiB转移、128扇区增长/16扇区metadata、真实prefix/uncertain |
| `user/sc32/sc32_path.c`：ScPathCreate/Rename/Remove | CREATEEX真实exclusive-create；rename/remove当前path式、pin会BUSY | 不能虚报已有条件publish/remove或独占read lease |
| `SC32API.H/SC32GUI.H/SC32BROKER.H/SC32DROP.H`及各bridge | 正式权限、窗口、clipboard/IPC/Shared、144B真实DropInfo | 不修改SCAPI冻结字节、不把foreignID当本人权限 |
| `SCUI.H`、`ui_data/dialog.c`、`SCUI.md` | 动态provider/selection/table/menu/dialog/document、真实clipboard/drag | 新Shell复用正文；ScUiIconLoad仅SCF/SCB，不冒充完整wallpaper服务 |
| `runtime/scl_platform.c`：system_library/read_file | 系统名包括SCSHELL/SCGFX/SCRES且固定SYS/LIB | 不需重发明搜索保护，不允许应用同名SCL替换 |
| `GFX.md`、`THEME.md`、`FONT.md`、`IMAGE*.md` | 调色角色、Classic/Aurora、凤凰、旧icon/wallpaper | 原生尺寸/资源、主题、VGA回退和用户视觉选择 |

## 3. 新能力状态：SOURCE与缺项逐项分开

| 工作包 | 当前已有正文 | Phase5缺项/状态 |
|---|---|---|
| 可信会话服务 | Broker SESSION/SYSTEM namespace、ACL、真Peer、动态owner、EOF/背压 | 无官方SCSHELL发现/host任命；**NOT_IMPLEMENTED** |
| desktop/taskbar角色 | 现kernel perSID桌面与z序、本人SC32Window | 无Ring3底层/任务栏专层、工作区role、跨窗获准动作；**NOT_IMPLEMENTED** |
| 应用生命周期启动 | 旧EXEC异步；ScProcessSpawn受管job与streams | 无modern session-lifetime GUI launch；**NOT_IMPLEMENTED** |
| 独立SCSHELL库/头/manifest | ROOT已保留系统库名 | 无SCSHELL.H、exports/imports/init/fini正文；**NOT_IMPLEMENTED** |
| 用户态service/desktop | 旧内核桌面还能用 | 无SCSHELL.SCX模型/协议/SCUI绘制；**NOT_IMPLEMENTED** |
| 文件稳定读取与namespace条件事务 | FILE动态pin、revision、真实IO/flush；ScPathCreate exclusive | 无owner/gen独占lease、持pin条件publish/remove；**NOT_IMPLEMENTED** |
| 文件作业/递归/冲突/恢复 | 旧CLI cp/mv等独立实现；旧Files修改单项 | 无共享Shell逐项worker与durable ledger/ACK；**NOT_IMPLEMENTED** |
| 关联/应用/菜单注册 | 旧SMENU/LNK和Files硬编码种子 | 无动态关联/verbs、注册CAS、Open With和扩展provider；**NOT_IMPLEMENTED** |
| 布局/多选/自由拖动/持久保存 | 旧按索引网格/分页；SCUI selection/provider可复用 | 无Desktop模型/空间索引/持久layout；**NOT_IMPLEMENTED** |
| 剪贴板Shell文件流程 | Broker immutable TEXT/FILES/CAS/源退出；SCUI文本Copy/Cut/Paste正文 | 无Files/Desktop multi-item COPY/CUT/Paste UI与条件删源；**NOT_IMPLEMENTED** |
| Drop Shell业务 | kernel真实ticket/WM hit/Connect/源目标身份；SCUI Drag正文 | 无文件持久结果ledger、Desktop各角色与完整跨窗口文件工作流；**NOT_IMPLEMENTED** |
| 统一Shell对话框 | SCUI MessageBox/FileDialog/PropertySheet正文 | 无关联/真实file job/conflict/properties Shell层；**NOT_IMPLEMENTED** |
| 壁纸/资源迁移 | kernel原异步wallpaper，SCUI图标SCF/SCB | 缺现代完整image service/SCGFX/SCRES或等价正式Ring3正文；**NOT_IMPLEMENTED** |
| Files2及其它主要组件 | 历史源码/产物和原工作流 | Phase6全范围保留，尚未实施重制；不能拿本表作削减清单 |

NOT_IMPLEMENTED表示具体正文/接线不存在，不注册cap、不列manifest假出口，不用返回成功占位。可复用的Foundation不因Phase5设计归零；其原源码、依赖、证据版本各自保持边界。

## 4. 建议源归属分割

以下是供主代理分工的文件边界，**不是本次已获准写这些源**。当前代理只拥有本文件和SCSHELL-DESIGN.md；各owner应先冻公共合同再实施，不能并写SC32/WM/fs同一个文件。

| 建议owner工作包 | 新文件与必要旧接线 | 接收/提供合同 |
|---|---|---|
| 主代理系统整合 | `kernel/sc32_desktop.c/.h`；必要`wm/session/task/main`、受保护路径gate、Make/包/SDK | 真实role任命、发现、专层/工作区、窗口page/watch/action、session-lifetime launch；退出与fallback |
| 文件事务owner | 新`kernel/fs_shell.c/.h`或`fs_shell.inc`；必要`fs/sc32_file`与owner清理 | exclusive-create-and-pin、稳定/namespace lease、conditionpublish/remove、metadata短步、结果未知与rollback |
| SC32 public桥owner | 新`SC32DESKTOP.H`、`SC32FILETX.H`及各private WIRE/bridge；必要SC32API聚合/json | 版本化typed记录、selector/cap/error；RO/RW先验与无效输出无效果 |
| SCSHELL SDK/服务owner | 新`user/SCSHELL.H`、`user/scshell/`公共client/registry/assoc/menu/icon/config/dialog，SCSHELL.C/json；新服务正文/json | 库cdecl、跨进程frame v1、真实imports/dep/fini；无SCAPI/asm、无库正文静态复制 |
| 文件job/drag owner | `user/scshell/shell_file.c/shell_transfer.c/shell_job.c`及SCFILEWORK.C/json/private头 | 动态工作树/leases、64KiB prefix、per-item状态、worker ledger/ACK/Cancel/恢复记录 |
| desktop/taskbar UI owner | `user/scshell/shell_desktop.c/shell_taskbar.c/shell_start.c/shell_layout.c`及资源descriptor | SCUI控件/自有绘图、动态provider/空间索引、持久layout、事件泵、隐藏停止绘制 |
| 后续Phase6应用owner | 新Files2/Studio2/Debug2及其它正式应用目录，历史入口接线由主代理 | 使用已真实成熟SCSHELL/SCUI；保旧功能；无NUI/控件复制 |
| 资源owner | SCRES/SCGFX正式头/库与现代图像桥；自有icon/layout/theme元数据 | 旧SCX嵌入icon/SCF/SCB、原wallpaper来源/许可/权限和正确缓存 |

SDK、服务与worker可以再细分正文，但公共header/json和同一aggregate只能一个owner。依赖图不允许SCUI基础反向依赖SCSHELL。新增内核对象与task私有链字段由主代理统一接线，别让每个owner各建全局PID扫描表。

## 5. 需要先冻结的最小接口细节

### 5.1 主代理host与launch

- trusted任命键固定SID/sessiongen/PID/PIDgen/真实endpoint；候选role激活以前旧桌面继续可用；只有首帧/工作区成功才切换。
- discovery提供身份值与受ACL保护的连接能力，不将foreign endpoint编号当caller拥有权；UID=-1/普通名字都非信任条件。
- desktop/taskbar专层均为host本人真实窗口，几何/visibility/input epoch、workarea与退出归属明确；注销不重启，异常回退与有限重启。
- 窗口page/watch只当前SID授权值；动作核ID/gen/role/真实WM资格，close-request支持veto；不泄canvas/proc。
- session-lifetime launch与managedworker分开，继承真凭据/环境/CWD和明确std端点寿命；服务死去不连带杀其启动的正常GUI，不留writer保活。

### 5.2 文件条件事务

- 路径只resolve一次；lease核本人handle/object/revision/name/owner PIDgen及当前真实权限。ordinarypin不等于read稳定，foreign现代write、旧事务、祖先rename都必须统一受lease约束。
- exclusive-create和pin在同事务内；候选mode先私有；OOM/前置拒绝out保持，候选全部rollback。
- publish带目标absent或exact identity/revision，在本人pin仍活时提交；foreign pin/事务BUSY，明确超级块前失败与结果不明区别。
- remove只能核真实ledger后用exact仍活source lease+path/object/revision删除；chmod/撤权仍复验，ACK本身不授delete。
- namespace树lease与文件lease归动态owner链；canonical-order try-acquire不持锁互等；退出有限步骤，元数据16sector poll不得借PID0越权。
- directory自身revision不足以证明子项稳定，lease取得时核view epoch并固定成员；同lease分页不接入后来的子项，不因无关目录变化重放整棵copy。绕过冲突的资格绑定具体handle/operation，不授同PID其它path调用隐形通行权。
- 返回包含确定commit阶段/真raw/error/prefix；未知结果不自动重放；release撤候选，不伪回滚已发布extent；PF/候选/锁归属需要统一实测。

以上均先在新header/private wire冻结，不私定未协调selector或借用旧SCAPI寄存器。现SC32/ROOT/SCUI源保持本批冻结输入，后续源码版本与manifest按主代理统一构建记录更新。

## 6. 实施顺序与阶段出口

| 顺序 | 必须交接内容 | 完成条件 |
|---|---|---|
| P5-A | 三项最小安全底座、公共/私有header及主代理owner/exit接线 | 真正文收齐，无假cap；静态授权/对象/失败路径可审阅 |
| P5-B | 独立SCSHELL.SCL、SCSHELL.SCX、SCFILEWORK.SCX和typed协议 | exports/imports/dep/init/fini真实；OOM/EOF/异常清理、回调pin、有限消息泵 |
| P5-C | 注册/关联/菜单/shortcut/对话框、文件job和MOVE/CUT ledger | 默认数据导入不覆盖用户盘；逐项持久语义、Cancel/冲突/权限错误准确 |
| P5-D | Desktop多选/框选/drag/layout、Start/taskbar、壁纸/资源 | 真Ring3 SCUI界面/role，原视觉资源/旧窗口兼容，session/隐藏完整 |
| P5-E | 少量真实代表工作流、原生工具链、磁盘/资源/截图 | 风险定向通过；问题具体修复/重测后结束，不扩理论排列矩阵 |
| Phase6 | Files2、Studio2、Debug2及其余应用完整工程重制 | 全原功能保留清单逐项对齐实际行为，完整M10a2仍持续 |

这不是先用旧桌面薄封装交差再删完整目标的计划。安全底座就绪之前仍可写独立模型/候选解析，不能把待接线函数列为正式成功出口；新默认桌面必须在功能真实实现后接管。

## 7. 后续定向验证的必要性与结束条件

本次不执行下列验证。先收齐Phase5源/资源/接线，再由主代理统一Windows双盘运行；与实际变更无关的原网络/音频/标准库历史结果直接复用。

1. **可信host与旧新窗口**：真实普通登录、另SID同UID、非host假注册拒绝、host异常回退；taskbar控制旧SCX和新SCUI窗口，CLOSE veto仍活，session退出动态链归原。解决新增跨窗/发现权限风险。
2. **桌面实际操作与持久布局**：一组真实鼠标/键盘选择、Ctrl/Shift/框选、多图标drag、snap/autoarrange、cancel，保存后同盘重启恢复；Classic/Aurora原尺寸截图及既有VGA入口。解决固定位置改为用户态模型的真实交互/存储风险，不重复旧19显示组合。
3. **关联/菜单/快捷方式**：旧menu/LNK导入、真实多级Start、Open With/unknown二进制、中文完整名、失败保存保原、动态注册/退出；与原exe/argv profile核实际子程序。解决新调度/配置副作用，不按每扩展名排列测试。
4. **一组真实文件事务**：Files↔Desktop/跨窗口COPY、MOVE、CUT/Paste；在覆盖确认后改目标、源写冲突/权限撤回、发布后删源失败、Cancel检查真字节/目录和保副本。解答lease/条件commit/ACK最主要竞态与持久语义，不假称多文件/掉电全原子。
5. **原生库与资源**：真实SCCC/G1编当前SCSHELL/服务/worker，格式核实后执行生成文件；源码和用户盘原字节保持；cold成本与暖PF/owner/cache分别记；IO作业同时测WM输入与实际长尾。解决“生成未执行”“host冒充native”和阻塞/泄漏风险。

具体FAULT/IO不明需依实际可控故障路径安排最小有效重测；不为理论覆盖擅加大矩阵。达成以上且完整工作流真正运行后才更新SOURCE_ONLY状态、同期同步规范/README与截图验收包；完整M10a2仍需Phase6/7出口。

修订：2026-10-15，建立Phase5现有源码/功能保留审计、真实Phase4局部证据边界、全部SCSHELL缺项、最小安全合同和分工建议；本次仅两份文档，未实现、未构建、未运行Phase5。
