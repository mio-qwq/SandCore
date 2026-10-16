# SCW 工程工作区接线

SCW 表示 SandCore Workspace（工程工作区）。Studio 2 的工作区入口、打开筛选、保存默认扩展名、ResearchDesk 示例和公开 SDK 文档统一使用 .SCW；内容保持现有 ScConfig、[project] schema=1 和 [file.N] 格式，不增加目标管理能力。

本组仅生成固定前像的局部补丁和两个小文件的精确移动计划。main、当前 GUI/helper、已生成的 SDK 副本、历史证据、游戏文件及冻结草稿均不改；不构建、不运行 VM。

Studio 2 的显式工作区打开只接受大小写无关 .SCW。文件对话框使用公开 filters/help glob，显示 SCW 工作区；保存时无扩展名则补 .SCW，显式其它后缀明确拒绝。自动补全只准创建不存在的目标，并把 ABSENT 条件落实到实际候选发布；补全后已有目标不会借对另一条路径的覆盖确认而被覆盖。用户可显式选择完整 .SCW 文件名，沿原覆盖确认和 Preserving EXACT 保存。

公开 FileDialog 没有初始文件名/默认扩展字段；本组不改 SCUI、不按私有节点次序设置名称框。新建工作区仍先在内存建立既有默认配置，第一次保存由用户选择目录和名字。默认的是扩展名，不暗定可覆盖的磁盘目标。

Studio 的启动路径接收当前公开 Shell MODERN 传入的单个引号包围路径；剥离成对外引号后再做绝对路径与 .SCW 识别。仍只接收单个路径，不增加命令行语法。

Shell 现有关联只按后缀，不支持内容条件。本组不增加泛 .SCW 默认关联、不增加内容嗅探系统。游戏二进制存档和原有用户关联保持；用户通过 Studio 工作区菜单或显式 Open with 打开工作区，Studio 完整解析与 schema 校验拒绝非工作区正文。

canonical 示例为 /SYS/SDK/EXAMPLES/RDESK/RDSTATS.SCW（1个源TU独立SCL）和 RDESK.SCW（4个源TU动态SCX）；两份文本内容原字节移动。BUILD.SH 仍逐条直接调用真实 SCCC/SCLD，不读取工作区描述，只补充自然的 SCW 说明。SDK staging 只登记 .scw，源码归档后缀同样只登记 .scw。

所有活跃正式文档自然说明 SCW 工作区含义/路径/合同，不讲命名沿革。M10A2-LIVE-GUI 与 CURRENT-GOAL 只给局部文字hunk，Root在当前GUI正常收束后整合；不改真实历史日志或旧截图对应的原始事实。

修订记录：2026-10-16，建立 SCW 工作区入口、默认扩展、SDK示例和发布文档合同；隔离源码，尚未编译或客体验证。
