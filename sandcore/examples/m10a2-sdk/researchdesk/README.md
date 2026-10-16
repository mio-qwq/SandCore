# ResearchDesk · 课题台

这是只使用公开 SDK 的独立桌面程序。MAIN.C、MODEL.C、VIEW.C、IO.C 分别生成四个 SCO；RDSTATS.C 单独生成 RDSTATS.SCL。工程不包含任何 `.C` 聚合，不导入 Files、Studio 的私有控件，不直接调用旧 SCAPI 或内核入口。

左侧项目树、中间多文档 Edit、右侧可排序数据表与分布图使用 SCUI。两个 Splitter 分别调整树/正文和正文/数据宽度，第三个调整表/图高度。菜单、工具栏、文件对话框、Tab、共享 Document/UndoRedo、查找和剪贴板均使用公共库。图表只实现绘制数据本身的公开 Class/Painter 回调，颜色来自主题角色；字体可在视图菜单切换。Classic/Aurora 及窗口尺寸变化由同一布局处理，隐藏时 SCUI 停帧，IO、解析和统计仍分批推进。

RDSTATS.SCL 通过 ScLoadLibrary 和 ScGetSymbol 实际装入，版本化状态/结果记录为 64/72 字节。插件计算完整 int32 最小/最大、正负零计数、精确双字整数总和及向零取整平均值，不使用浮点或 long long。插件查找以实际主程序目录为准：库必须与 RDESK.SCX 同在 /HOME/RDESK。菜单的“释放插件引用”释放实际持有引用；已取函数地址按公共合同仍固定 provider 到任务退出，因此替换插件后须重开程序。未连接或 ABI 错误只报告失败，文档与已提交数据保留。

## 普通用户构建

先安装同批 SDK 并提供真正的 SCCC/SCLD 与依赖库；用普通登录用户执行：

```text
/BIN/SH.SCX /SYS/SDK/EXAMPLES/RDESK/BUILD.SH
/HOME/RDESK/RDESK.SCX
```

BUILD.SH 每一步检查退出值，只写 /HOME 的三个目录。APP.RSP 的四行仅是对象路径。SCLD 输出真实 SCX/SCL、`.map` 和 `.sdbg` 副文件；副文件绑定完整映像 SHA256。脚本最后的 BUILD_OK 只证明构建命令成功，不能代表 GUI、客体执行或整个 SDK 已验收。

SCW（SandCore Workspace）是 Studio 2 的工程工作区。通过“工程 → 打开工作区…”分别打开 RDSTATS.SCW、RDESK.SCW，先构建插件，再构建和运行应用。SDK 工程本身只读；需要改源码或工程配置时先复制到普通用户目录，并修改源码、manifest、include_dirs、workspace 路径。Studio 自动建立独立构建目录和对象响应文件，不使用本示例固定 APP.RSP。SCL 工程不能单独 Run，RDESK 是实际宿主。

## 数据与保存

每行格式为“标签 + 一个 Tab + 有符号十进制 int32”；空行与以 # 开头的行忽略，UTF8 标签按实际长度分配。单文档上限 4MiB、单标签上限 65536 字节，超限明确拒绝，不截断。解析与统计每轮最多处理 64 行/值；提交的数据、源行导航和统计都绑定文档 ID、正文 revision 和数据代数。修改源文档立即让旧统计失效，错误候选不覆盖原有效数据。

打开先固定 READ_STABLE 身份，按实际 EOF 完整读入，再验证 UTF8/无 NUL 才建文档。保存持有正文修订与快照，写同目录不可见候选并 flush；已有文件用 EXACT 和 ScFileLeasePublishPreserving 保留原 uid/gid/mode/flags，新文件用 ABSENT。只有真实 durable COMMITTED 终态且正文 revision 仍相同才清 dirty；期间继续编辑，成功保存的旧快照也不能将新正文标为已保存。磁盘冲突、权限错误、取消均保留文档和 Undo。

当前文件名采用既有绝对路径边界：含开头 / 最多 64 字节（rootless 63），不接受空白、分号、冒号或反斜线；同目录候选后缀需要父目录路径最多 34 字节。未知长度文件动态读取至公开上限；不会将当前旧文件的 size 当成永远准确的 EOF。

## 同一客体轮次的结束条件

1. BUILD.SH 返回 0，四个不同源的 SCO 和 RDSTATS.SCO 均存在，SCLD 两次输出实际对象计数 1 与 4。
2. 启动窗口出现树、两个文档、四行数据及分布图，标准输出出现 `RDESK_ANALYSIS`，`rows=4 sum=40 mean=10`。激活值列排序，双击数据行定位到当前同修订正文；拖三个分隔条，切换 Classic/Aurora 并调整窗口。
3. 文档中将 12 改为 20，旧结果立即失效；执行“读取数据并统计”得到 sum=48、mean=12；Undo 后重新统计回到 40/10。输入非法数值再解析应报告真实源行，原数据保留。查找、复制/粘贴与 Redo 使用同一 Document 历史。
4. 另存为 /HOME/RDESK/DEMO.RDT，修改后再保存；从另一任务改同一磁盘目标再保存应报告冲突并保留 dirty，普通用户不能覆盖只读 SDK 源。用取消中止未提交读/写，文档仍可操作。已有共享文件保存后 uid/gid/mode/flags 保留。
5. 先退出程序，将插件移到其它用户目录，再启动，窗口/文档仍可用并明确插件失败；恢复插件并“连接统计插件”后统计恢复。此动作只针对自建 /HOME 产物，不修改系统库。最后退出，受管任务与本任务句柄/页回收恢复。

这些是主代理待执行的客体结束条件。本目录的宿主真实后端与模型报告分列，不把它们冒充上述结果。

修订记录：2026-10-16，SCW工作区路径、公开Studio入口和ResearchDesk构建合同。
