# M10a2 用户组件产品修正

本轮处理用户实际遇到的日常操作缺陷。各组件按独立产品研究成熟交互，正确使用系统公共库，并将验证限定到具体改动；设计说明不代替程序修复。

## Studio：独立开发工具

成熟参照是 VC6 的项目类型、名称、位置向导，源文件类型选择和直接可见的构建输出。参考 Microsoft 原始知识库 [Q187912](https://jeffpar.github.io/kbarchive/kb/187/Q187912/)、[Q301357](https://jeffpar.github.io/kbarchive/kb/301/Q301357/) 及 [Visual C++ 项目文档](https://learn.microsoft.com/en-us/cpp/build/creating-and-managing-visual-cpp-projects?view=msvc-170)。采用基本交互，不宣称 MFC 或 C++ 支持。

- 首次启动显示开始页，提供新建程序、打开源码和打开 SCW 项目。
- 只写一个 C 文件时直接打开即可构建；新建明确区分 `.C`、`.H` 并自动补后缀。不要求先手写 JSON 或了解链接表。
- 项目向导询问位置、名称和类型，创建项目文件夹、MAIN.C、BUILD 和 SCW。保留空项目、多源项目、第三方库、SCL、高级显式清单、调试和发布功能。
- 普通程序按真实 SCO 符号及系统 SCL 导出生成精确链接清单；版本、数据宽度和依赖来自实际库。内部映像标识遵守大写合同，用户文件名可以使用小写。
- F7 构建、Ctrl+F5 运行、F5 调试。运行前有修改则先构建，失败不运行旧产物；输出区显示真实工具报错，诊断保留文件和行号。
- 默认工具位于实际 SDK 的 `/SYS/BIN`。恢复 SCAPI.H、SCIO.H、SCMEM.H 的包含目录接线，不修改 SCAPI.H 冻结原字节。

操作见 [STUDIO2-USER-GUIDE](STUDIO2-USER-GUIDE.md)，可选自动模式见 [SCW-WORKSPACE](SCW-WORKSPACE.md)。旧显式 SCW 不需要迁移。

## Files：独立文件管理器

成熟参照是桌面文件管理器的导航、滚动范围和一致的文件关联。参考 Microsoft [文件关联](https://learn.microsoft.com/en-us/windows/win32/shell/fa-how-work)、[Shell verbs](https://learn.microsoft.com/en-us/windows/win32/shell/fa-verbs) 和 [滚动范围](https://learn.microsoft.com/en-us/windows/win32/controls/scroll-bars)。

- 原编辑、调试及回退路径硬编码旧 IDE、DEBUGGER、LENS、FILES。本轮按实际类型使用 Studio2、Notes2、Debug2、Lens2 和 Files2；只有新版未安装才回退，权限或启动失败不悄悄改用旧程序。
- 普通打开尊重账户关联；补充 SCW 和常见文本的系统默认关联。自定义和旧程序入口保留。
- 公共表、树、列表原滚轮只限制一端。本轮将偏移限制在内容高度减可见高度之内；考虑固定表头，内容缩短或窗口变大也归位，偏移不变时不重复损伤。
- 公共对话框双击和 Enter 进入目录，“选择文件夹”提交目录；显示文件名、实际类型和打开、保存、选择按钮。双击命中在列表目标阶段处理，避免混用对话框和列表坐标。

## 本轮状态与验证范围

仅构建变更组件，必要检查覆盖普通 C 程序、旧公开头、具体编译错误、目录双击、滚动边界和新版打开入口。未受影响的证据复用，不重跑旧矩阵，不追查用户已放下的资源事项。源码、宿主构建、原生生成、实际操作和试用盘更新分别记录。

实际操作已完成，证据见 [产品修复实测](../evidence/m10a2-product-fix/README.md)。HOST26/native15/final15 使用实际系统内编译产物，SCUI 保持 ABI1.4、174 个导出，库修订为8。

已确认打开旧 C 文件即可构建，SCAPI.H 程序实际输出 Hello legacy!；目录双击进入、Files 滚动边界及右键进入 Studio2 有效。保持新建项目的默认 HelloWorld 名称、C 控制台类型和源码，F7 构建成功，Ctrl+F5 实际输出 Hello, world!；类型显示和输出末行自动跟随正常。故意加入一处语法错误，弹窗与输出均显示 MAIN.C:1 及 expression needs a value。检查结束，未追加资源矩阵。

第一轮发现的临时产物文件名与链接清单大小写不一致已修正，原失败保留。试用盘及发布包的同步状态以交付记录为准。

修订：2026-10-17，两个组件的独立产品设计、实际缺陷与收敛验证范围。
