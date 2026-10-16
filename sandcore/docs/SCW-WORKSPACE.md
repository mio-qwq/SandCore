# SCW 工程工作区

SCW 表示 **SandCore Workspace**（工程工作区），正式扩展名为 **.SCW**。Studio 2 用工作区描述组织源文件、公开 SDK、构建工具、输出和布局；源代码与构建产物仍是各自独立的文件。

## 打开与保存

在 Studio 2 选择“工程 → 打开工作区…”，或在 Shell 的显式 Open with 中选择 Studio。工作区对话框筛选 .SCW，程序参数也可传入一个完整工作区路径。普通文本的打开和编辑入口保持独立。

“新建工作区”在内存建立默认配置。第一次保存由用户选择目录和名称；无扩展名时使用 .SCW。自动补全后的目标必须不存在；如该名称已存在，请显式选择完整 .SCW 文件名，由标准覆盖确认和精确发布处理。完整绝对路径仍受当前64字节、无空白的文件系统边界约束。

读取先取得 READ_STABLE 文件身份，完整解析后再发布候选配置；无效正文或 schema 不改变当前工作区。保存使用现有候选、flush和条件发布；只有真实 durable COMMITTED 才更新磁盘修订和已保存状态。已有目标继承其原 uid/gid/mode/flags，取消、权限失败或冲突保留当前配置。

## 文本合同

正文为公开 ScConfig 的UTF8文本，当前 [project] 的 schema=1u。file_count 是 [file.N] 记录数；每项 path 是绝对文件路径，source=1u 表示独立编译的源TU，source=0u 表示头等成员。不会把所有 .C include 到一个聚合文件冒充独立编译。

project 节保留 kind（SCX或SCL）、compiler、linker、manifest、output、include_dirs、workspace、arguments、debug_host、publish_dir。include_dirs 以分号分隔；workspace 是可写构建目录。UI布局由同一配置序列化。源、manifest、输出和副文件别名检查、只读SDK和普通用户权限边界继续有效。

## ResearchDesk

公开示例目录为 /SYS/SDK/EXAMPLES/RDESK。

- **RDSTATS.SCW**：RDSTATS.C 为1个独立TU，链接到 /HOME/RDESK/RDSTATS.SCL；RDSTATS.H 是头成员。
- **RDESK.SCW**：MAIN.C、MODEL.C、VIEW.C、IO.C 分别编译为4个SCO，再链接到 /HOME/RDESK/RDESK.SCX；两个头是普通成员。

先构建 RDSTATS.SCW，再构建 RDESK.SCW。SCL工作区不独立Run；RDESK.SCX实际加载同目录插件。F5构建、F6运行、F7调试，构建成功与真实运行分别留证。普通用户也可执行 /BIN/SH.SCX /SYS/SDK/EXAMPLES/RDESK/BUILD.SH；该脚本直接逐条调用公开 SCCC/SCLD，不解析工作区描述。

SDK中的工作区与源文件是只读示例；编辑时先复制到本人目录并相应调整成员、manifest、include_dirs及workspace路径。

修订记录：2026-10-16，SCW工作区入口、schema1、条件保存和ResearchDesk双目标合同；本合同对应源码接线，实际新字节编译与运行证据另列。
