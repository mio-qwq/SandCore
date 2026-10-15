# M10a2 public SDK examples

这两个独立程序只包含官方公开头和标准头，通过 manifest 动态导入系统库。
本批已通过公开INC的HOST GCC与真实SCCC宿主后端编译，**尚未客体编译或运行**，尚无示例客体截图或验收结果。
它们不复制 `SCUIDEMO`、NUI、私有 syscall 桥或库正文，也不代表主要系统组件重制已经完成。

## EDIT2：同一个正文，两个视图

`EDIT2.C` 创建两个真实 SCUI 窗口。每个窗口都有统一菜单栏、工具栏、查找输入、按钮、
代码编辑区和状态栏；第二个视图启用 wrap。两者共享一个 Document 和 Undo/Redo 历史，
但选择与滚动各自独立。用户在一个视图输入后另一个视图显示同一份正文。

| 操作 | 入口 |
|---|---|
| 新建、打开、保存 | File 菜单；Ctrl+N、Ctrl+O、Ctrl+S |
| 另存为 | File / Save as；真实 Save 文件对话框和覆盖确认 |
| 撤销、重做 | Edit；Ctrl+Z、Ctrl+Y |
| 系统剪贴板 | Copy/Cut/Paste；Ctrl+C、Ctrl+X、Ctrl+V |
| 全选 | Ctrl+A |
| 查找下一处 | 查找输入框 + 下一个按钮；Ctrl+F 执行当前查找，末尾 wrap |
| 关闭一个视图 | 真实标题栏关闭；另一个视图继续 |
| 退出全部 | File / Exit；有 dirty 正文时 Save/Discard/Cancel |

关闭最后一个视图也会询问是否保存。若另一个视图仍活，关闭当前视图不会销毁共享 Document。
New/Open 遇 dirty 正文先保留原文并在状态栏提示保存；不静默丢失改动。
打开/保存对话框默认目录为 `/HOME`，实际目录和文件仍由当前身份权限决定。
Open 完整读入动态 Buffer 并严格验证 UTF-8，成功后一次替换原 Document，并清除旧 Undo。
Save 使用正文快照，只有完整写入并成功 fclose 后才标为已保存；失败的目标可能留下部分文件，
正文和 dirty 状态保留，应用不冒称原子 rename。

代码没有 RGB 常量、绘图循环或轮询。标准控件、Classic/Aurora 外观、Phoenix 字形、DPI、
焦点、Tab、选择、caret、损伤与隐藏停画都使用真实框架行为。`framework_error` 保留错误的
cause/domain/raw/operation/node 输出到 stderr；普通操作失败写状态栏，窗口继续可用。

资源顺序：应用持有 Context/Document/Menu/Toolbar/当前路径；每个 EditBind 持有同一 Document；
监听器由节点/Context 归属。弹出对话框持有到完成回调，回调先撤应用指针再 Release，
取消不提交路径。正常结束先关闭对话框/窗口，再 Release Context、正文、路径与菜单。
内存候选失败不提交正文；保存路径的引用在磁盘操作前准备，最后交换不再分配。

## UTILITY：stdio → Model → typed Config

`UTILITY.C` 用 FILE 每次读 4096B，以动态 Buffer 累积整行，允许跨块长行和跨块 UTF-8。
每行完成后严格校验，再提交带 UTF-8 `label` 属性的 stable Model item。
ModelVisit 读取当前项目并建立 typed Config，最后使用库的 canonical serialize。
空行保留，LF 分隔前的 CR 去掉；末尾无 LF 的非空行也保留。模型没有用户 drop 回调。

```text
export SDK_INPUT=/HOME/INPUT.TXT
export SDK_OUTPUT=/HOME/OUTPUT.INI
/HOME/UTILITY.SCX
```

不设置 `SDK_INPUT` 时读三条内置 UTF-8 示例；不设置 `SDK_OUTPUT` 时将规范配置写入 stdout。
程序同时报告实际 PID/generation、项目数、UTF-8 正文字节和序列化长度。
UTF-8、容量、内存、FILE 读写/flush/close 任一失败返回非零，并释放 Model/Config/String/Buffer。
失败当前行的空候选会删除，已完成的项不会被伪称整批回滚；输出文件可能有已写前缀。

## 从 SDK 编译

以下命令只说明复现方法，本批没有执行。需要维护者已安装与 SDK 对应的实际 CORE、五库、
公开头、Font/Unicode 资源及 SCCC CLI；普通用户只写自己有权限的输出目录。

```text
/SYS/BIN/SCCC.SCX /SYS/SDK/EXAMPLES/EDIT2.C /HOME/EDIT2.SCX --dynamic /SYS/SDK/EXAMPLES/EDIT2.JSON
/SYS/BIN/SCCC.SCX /SYS/SDK/EXAMPLES/UTILITY.C /HOME/UTILITY.SCX --dynamic /SYS/SDK/EXAMPLES/UTILITY.JSON
```

`EDIT2.JSON` 直接依赖 SCCLIB/SCLIB/SCUI；SC32/ROOT 由真实库依赖闭包装载。
`UTILITY.JSON` 直接依赖 SC32API/SCCLIB/SCLIB。两份 JSON 仅列正文与标准宏实际使用的导入，
没有“以后可能会用”的符号。ROOT 只在过程引导时统一装载，不在每个应用重新实现 runtime。
SDK 来源、profile、插件清单、SHA、许可与失败规则见 `M10A2-SDK.md`。

## 修订记录

| 日期 | 修订 |
|---|---|
| 2026-10-15 | 新增公开 SDK 双窗编辑器与 CLI 示例完整源码、精确动态清单和操作说明；仅源码/静态检查，未编译或运行。 |
| 2026-10-15 | 79文件native-preview暂存及公开INC双编译通过，实际输出/来源见m10a2-sdk-public-compile证据；客体运行与界面未验。 |
