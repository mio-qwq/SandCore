# Settings2 启动只读审阅

Root 已取得菜单实际高亮“设置”的截图、独立 Enter 动作，以及随后 PID4 UID/GID1000 `SETTINGS2.S` 的真实 ps 记录，因此程序实际启动已确认。本代理没有操作 VM、读取活进程或回放菜单；晚期原始退出值没有实测，不为重复已确定的 constructor 拒绝另开 Terminal。

源码已有一个**确定的 constructor 拒绝点**：`settings_view.inc::settings_table` 的首列 `columns[0].column=0`，而实际 `ui_provider.c::ScUiTableSetColumns` 明确检查 `!columns[i].column` 并返回 `SC_UI_E_INVALID=-301`。`settings_create` 先创建程序表，之后才创建环境表、文档和 shell 连接。如果前面的创建调用均成功，必在首个程序表返回 -301，main 立即 `settings_close` 再返回 301。首个 `app_step / ScUiContextStep` 位于整个 create 成功之后，因此这个路径可能在首个客户区画面提交前就关闭窗口。

这个确定缺陷与已确认启动后无持久窗口的现场相符。`301` 仍是从明确调用条件与 main 返回合同推导的值，不能把它伪装成晚期实测退出码或已取到的具体调用栈。

最小成套修复是 3 份局部 hunk：表头 ID 改为 `i+1`，数据 field 继续为 `i`；程序与环境 provider 的 CELL 分支改读 `q->field`。SCUI 的 `ui_provider_cell` 实际只设置 `request.field`，`request.column` 保持初始化的 0，所以不能仅把表头 ID 改非零后仍按 column 解释行数据。修复后列身份为 1..4/1..2，模型字段仍 0..3/0..1，不影响列重排或持久模型身份。

按 Root 追加范围核了同目录 Installer：它自己的 `install_view.inc` 也传首列 0，`install_model.inc` 也按 `q->column` 解释 CELL。另交独立 `installer-column-field.patch` 的 2 文件修复，保持 4 列原数据 field 和字节列数值类型；没有修改共享 `app_ui.inc`。两应用目前均未设置 SORTABLE 标志或 provider 排序能力。SCUI 的真实排序入口会由列 ID 找 descriptor.field，修复保留该映射，不额外加入排序功能。

已沿启动前序逐项审阅，没有据猜测另改它们：

- `app_create` 的 Context 描述保留零 reserved、支持的 scale/font 默认值、64 quantum；Window 描述采用已查询显示的有效客户尺寸和标准样式。
- layout helper 已设置 spans=1、合法 AUTO/max/alignment；GRID 的 columns=0 在当前严格校验中并不构成 constructor 错误，不改布局来“试试看”。
- 菜单项/CommandSet 的记录头、命令、快捷键和修饰符构造符合当前入口；程序/环境 provider 和 selection 的记录也有效。
- Tab 页通过真实 panel 创建后加入，page/root 布局条件没有发现确定的常量错误。
- `ScSessionPath` 对六个单 leaf 没有白名单排斥 DISPLAY.CFG；后续系统显示配置路径仍按现有代码单独指定。不能把这段误判为权限失败而放开权限。
- USER/ENV 读取失败仅打印并保留编辑器，catalog request 失败也不覆盖 constructor 的状态；它们不应被硬修为“成功”。ScShellConnect 属于后段，若实际返回负值仍需按原错误处理。

`settings-column-field.patch` 只改 Settings 的上述 3 份局部函数；Installer 的 2 份独立修复另列补丁。没有修改 main、共享 app_ui、SCUI 库或公共条件，也没有新增导入或改 HOST/NATIVE manifest。根因条件和对应实际 GUI02 源摘要均列在 pins；只合主补丁时刷新 SETTINGS2，同时合 Installer 补丁时再增加 INSTALLER2，不影响其它组件。

`prepare.py` 仅在本目录从固定原像生成文本与摘要。主树、当前 VM 输入、旧 Notes/Studio 草稿保持冻结。本次没有构建、单元夹具或 VM 执行，不声明修后已通过。Root 后续必要回归是实际 Settings 窗口与显示预览回退，顺便确认程序/环境列按各自字段显示，再继续当前 GUI；不新增 probe 或矩阵。
