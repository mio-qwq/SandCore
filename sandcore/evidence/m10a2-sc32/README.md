# M10a2 SC32 基础证据

正式版本：**SandCore M10a2 — The Application Platform Era**。完整目标仍为 active；此入口只登记实际已经运行的基础行为，不代表四库、SCUI、桌面/组件重制或整个版本完成。

`build.json` 是 2026-10-15 WSL 定向 build03 的完整输入与输出记录，退出 0、77.56 秒，期间源码未改。首次两批构建的实际新源码缩进拒绝和修复保留在本机 `build/m10a2-sc32-bootstrap`；修复仅拆行，去空白源码前后一致，没有降低 Werror。新 CORE payload 451504B/BSS 356584B；SC32API 宿主引导库 101297B，85 exports/5 imports。

`foundation.json` 来自一次 Windows QEMU/TCG、软盘+256MiB IDE 的实际运行。只修改自己的验证副本，原 M10a1 数据盘与本轮安装的源码摘要保持。外部 SYSTEM Shell 启动真实 Ring3 应用，此批不冒充普通身份或完整 GUI 验收。

公开应用只包含 SC32API.H，通过独立库实际取得能力、规范前缀及未知尾部保留、本人 PID/代数、陈旧代数拒绝、只读输入允许/只读输出拒绝与稳定错误、固定生命周期符号、真实本人堆页/单调时钟、可读写更新文件、保持打开 flush、定位读写、131075B 定位后的零洞、真实叶名目录页，以及接管标准流后的旧子进程输出。GCC 应用和客体 SCCC 动态应用均运行同一组行为，五参数调用及要求 16B 的 GCC 局部栈检查通过。

客体应用 10223B，SHA256 `15e05af18c9578fb5a2dbee66e4f2b15bf2134d4537480050e1aef115b8d8a83`。客体还生成 81686B 的 SC32API.SCL，SHA256 `1856bc475ec79ec9b3921a6096365633d1c682d630c7f58ab4b7bec29f32bda1`，85 exports/5 imports 格式检查通过；首轮 VM 没有执行这份新生成的库，实际装入由下节单列。

暖态再次运行公开应用，退出物理页为 8894→8894；既有 SCX ls 仍实际运行。`desktop.png` 是已查看的原尺寸 1920×1080 现有桌面截图，没有称为桌面重制。`compiler-capacity.json` 是本轮限定结构体身份/前向完成修复后的宿主真实编译器后端容量检查；它本身不执行生成的 i386 编译器。

复现：先 `wsl.exe -e python3 /mnt/c/Users/Administrator/Desktop/projectos/sandcore/tools/build_m10a2_sc32.py --tag <新标签>`，再 Windows Python 运行 `tools/verify_m10a2_sc32.py --out <新目录> --build-report <该标签JSON>`。GUI 定向入口为 `tools/verify_m10a2_sc32_gui.py`，其退出状态须依据实际报告登记。

## 原生库与真实窗口

`gui-build.json` 记录 build04 的新 CORE/CapsLock重复通码修复与HOST三窗口应用构建，退出0。`gui.json` 来自 `m10a2-sc32-gui-03`，实际状态 **DECLARED_NATIVE_LIBRARY_AND_GUI_CASES_PASS**。两份原生库与窗口应用正文同目录保存；它们是实际客体生成的机器码，未把HOST文件拷成编译结果。

这轮固定 SYS/LIB 安装前轮客体实际生成的 SC32API（81686B）与 Phase1 客体根（45989B，SHA256 `c0a4b47cab3f83a578b23dbe3baa3fcbf2d1a5592871a218546f02e55fd51aba`）。根文件复用原生成产物，未声称这轮重编；它的9导出/无依赖引导合同保留。公开文件探针先经两份原生库实际运行。随后SCCC在系统内编译25927B三窗口应用，SHA256 `4a6d80d734e6920ef8f3903aa5173bba0c5edb5a3014e4e58a234c43d1f97780`，真正调用原生库的窗口过程。

明确显示外部SYSTEM自己的桌面，再以SYSTEM Ring3运行双根A/B与owner模态C，未冒充普通身份。HMP Shift+A真实通断/大写文本、QMP左/右/中真实按钮、模态阻挡后的C焦点、A跨B capture到真实UP、首次CLOSE拒绝、B/C单窗关闭后A继续、隐藏B停止PAINT但模型继续、恢复与旧Submit/Paint的STALE、真实标题拖动保持客户geometry、正常main返回全部通过；最终coverage位0..26恰0x07ffffff。三张1920×1080图已查看，显示开发验证应用，不是SCUI或桌面/组件最终重制。

首轮 GUI02 的6877→6878留在原FAIL报告。定位为既有Shell首次 `&` 的后台作业表：`sh.c::background_reserve`申请16×8B，USERALLOC实际一页，wait清项目后保留容量复用。GUI03先独立测同一个Shell背景成本6877→6878，然后GUI生命周期实测6878→6878；只读池计账显示GUI owners/window objects与pages都0，其它池当前页数无增量，仅历史high_water/peak改变。应用本人heap10→10，窗口0，正常main退出0。没有将未知差额直接抹除或改PASS。

GUI01是宿主脚本把输出命名TWINN.SCX与严格manifest不匹配，编译器正确拒绝；修正脚本输出目录/原basename。GUI01/02脚本、报告、日志、截图、原生机器码保留，重复生成盘的精确清理清单随报告保存。当前GUI03盘与原盘继续保留。

2026-10-15：保存首次SC32基础与客体库真实执行、有序窗口/键鼠/回收证据；记录两次实际拒绝/归属修正，不把P0通过称为完整M10a2完成。
