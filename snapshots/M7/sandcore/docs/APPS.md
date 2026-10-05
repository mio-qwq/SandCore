# M6 用户态组件规范 v0.2

> 作者 mio。以下所有组件均为 SandFS 上的普通 ring3 SCX 程序；内核不包含它们的业务逻辑。
> 公共接口先定义于 `user/api.h`，cdecl 桥在 `user/start.asm`，全部系统调用见 SYSCALL.md。

## 1. 应用与操作

| 程序 | 启动 | 功能与按键 |
|---|---|---|
| SandShell | 桌面 Shell | help/clear/echo/ls/run/exit；bin 命令查找；63 字符长行 |
| SandAsm | `asm home/demo.asm apps/demo.scx` | 两遍汇编，结果/错误页，Esc 关闭；详见 ASM.md |
| Files | 桌面 Files / `run apps/files.scx` | 0 全部、1 sys、2 bin、3 apps、4 home、5 desk；j/k 或上下选择，Enter 打开，F5 刷新 |
| Notes | 桌面 Notes / `run apps/note.scx 路径` | 默认 home/notes.txt；ASCII 追加/退格/LF、尾部滚动；F2 保存、F3 重载、Esc 关闭 |
| Palette | 桌面 Palette / `run apps/palette.scx` | 展示正式槽位 1..42；1 沙丘/2 夜色/3 暖沙，保存模式号，Esc 关闭 |
| Calculator | `run apps/calc.scx` | 两个 i32 十进制整数的 + - * /；Enter 计算、C 清除、Esc 关闭；除零/溢出显示错误 |
| Mines | `run apps/mines.scx` | 8×6、8 雷；方向键选择、空格揭开、F 标旗、R 重开、鼠标左键揭开、Esc 关闭 |
| hello | `run apps/hello.scx` | 窗口绘图示例，按任意键正常退出 |
| panic | `run apps/panic.scx` | 验收专用真实 ring3 除零；系统红屏，需重启 |

Files 是前缀目录导航，SCX 直接 EXEC；txt/asm/cfg 经 Notes 打开，其他二进制显示资源提示。
Notes 当前是末尾编辑模型，不包含任意位置插入、中文输入或搜索；保存正文最多 4095B，未保存关闭会舍弃本次编辑。
Calculator 的中间 + - * 用 i64 检查范围，除法仍为 i32；INT_MIN/-1 与除零先拒绝，避免异常红屏。
Mines 首次揭开才布雷并排除首格；整数 LCG 由 tick 播种。零邻雷扩展队列最多 48 项，40 个安全格全部揭开才胜利；失败显示所有雷。

## 2. 统一界面

应用复用 palette.h，背景 PAL_CON_BG、正文 PAL_TITLE、强调 PAL_CON_TINT、状态警示 PAL_CON_WARN。
页边距 8px，标题有 3px 青色强调线，28px 分隔线，128px 页脚；正文与操作说明分开，避免遮住内容。
桌面图标为独立 16×16 SCF 文件，用户中文字库未改动；默认图标可由 .lnk 修改目标。
壁纸配置为 `sys/wall.cfg` 的一个 ASCII 模式字符 0/1/2；切换成功后落盘，下一次启动读取。

## 3. 构建与边界

宿主 GCC 以 freestanding/no-libc/no-float 编译，链接基址 0x400000。BSS 在当前平映像中物化为零，SCX 加载器为每份应用建立私有页表。
公共封装只负责 ABI 与安全容量复制，不绕过 ring3，不访问 IO 端口；用户态随机、解析、编辑、计算、文件导航均自己执行。
SCX 容器仍 v1，不添加任意魔数；这批应用借宿主 C 工具链构建，不代表系统内 SCCC 已完成。
用户运行期文件写进挂载数据盘；资源变化导致重新 mkfs 会重建目录，重要文件先备份整盘，详见 FS.md。

## 4. 验证

`python tools/verify_apps.py` 自动使用 Windows QEMU 双盘、IDE 测试盘副本、HMP/QMP 输入和截图。
行为断言覆盖：生成程序运行、三类非法汇编不输出、Notes 精确写入/重读/F3、Files 新文件导航/打开、Calculator 正常/除零/溢出/INT_MIN 除法守卫、Mines 首格/旗标/鼠标/方向/胜负、Palette 设置与重启恢复。
`build/m6/results.json` 记录结果；`build/m6/*.png` 为证据。用户界面验收尚待确认，程序自动化通过不能替代用户审美判断。

## 5. 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-10-02 | ring3 汇编器及 Files/Notes/Palette/Calculator/Mines 矩阵、操作/限制/界面/构建与自动化验证 |
| v0.2 | 2026-10-02 | 详细中文注释解释字节/视觉行、目录缓冲与异步执行、数值边界、扫雷队列/点击边沿；新增 F3/旗标/溢出和汇编边界验证 |

### M7 图形工具接力

Files 已升级完整目录导航与创建/重命名/删除，2 HOME/3 SYS/4 APPS/5 BIN，
文本默认交给 SC STUDIO。Studio 65535B 源码、任意位置编辑、UTF-8 边界
与同源原生汉字，F1 打开/F2 保存/F3 新建/F4 源码/F5 C 或 ASM 内部编译/
F6 运行/F7 调试/F8 开头/F9 结尾。打开失败保留当前源码，编译失败禁用旧产物。
SC DEBUG：B 断点/U 撤销、F8 真单步/F5 继续/F4 暂停、M 内存/I 指令，
退出连带结束专属目标。最大 6 窗口，任务条可切回被全屏目标盖住的工具。
游戏/短片详见 GAMES.md/WELCOME.md，操作与联合验收见 TESTING.md。

2026-10-02：图形 Files/Studio/Debug 与新版目录/编辑/编译操作同步。
