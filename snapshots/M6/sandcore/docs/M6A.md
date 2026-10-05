# M6a 验收包（2026-10-02，作者 mio）

状态：实现与自动化验证通过；界面审美由用户验收，尚未收到确认。M6 后续规格继续执行。

## 变更清单

1. 按 HANDOFF §3 顺序修复 sanddata 依赖、字体路径、文件快捷方式/图标、启动器双盘。
2. SYS_TXT/FILL 改写独占画布；稳定句柄与画布指针不受置顶/关闭重排影响。
3. SYS_PUTS 改为本人最近窗口的流式输出，折行/滚动/整格退格；SandShell 修复 LF、命令匹配与提示符。
4. SYS_EXIT 与红叉回收 owner 窗口；EXIT 出口立即调度，普通系统调用返回原任务，PIT 继续抢占；后续安全释放用户页/页表/内核栈，任务槽可复用。
5. 修复实际联调发现的 SCF 少一字节、汇编源码误打包、CR0/CR3 指令方向、初始数据段为空、未赋值 uphys。
6. 文件末扇区读写中转，检查用户缓冲映射；修复 hello 未轮询 GETKEY 就退出；开机标题与桌面壁纸分开。
7. 构建添加自动头文件依赖、禁浮点/SSE/MMX、BSS 栈边界检查，移除旧 term 链接。

## 怎样验证

WSL Ubuntu `make` 构建正统 ELF 工具链，Windows `C:\Program Files\qemu\qemu-system-i386.exe` 运行。
自动工具 `python tools/verify_m6.py all` 挂载引导盘 `if=floppy` 与 IDE 测试数据盘副本，经 HMP sendkey 驱动命令，经 QMP 分沿操作鼠标，经 HMP screendump 输出 PNG。
HMP pmemsave 验证磁盘字体实际激活 27 字、无自动 Shell、画布摘要、稳定句柄/owner、分页位以及任务/窗口回收。
退格前后画布 SHA256 一致；拖拽前后画布一致；后台 hello 不得取走 Shell 的按键；同一个 hello 同时两份；连续启动/退出 10 轮超过任务表容量仍可复用。
最后运行真正 ring3 除零程序，验证 PAL_PANIC 红屏并目检 VEC 0x00 字距。

## 截图证据

| 文件（build/m6a/） | 证据 |
|---|---|
| 01-boot.png / 02-desktop.png | 开机画面、只有桌面与磁盘图标 |
| 03-shell.png / 04-help.png / 05-files.png | 图标启动、Shell 命令、磁盘字体/程序/快捷方式路径 |
| 06-concurrent.png / 09-two-hello.png | 分页多任务、同程序两份并发 |
| 07-exit-reclaimed.png / 08-backspace.png / 10-repeated-launch.png | 退出回收、退格纯净、任务槽循环复用 |
| 11-panic.png | 真实除零 VEC 0x00 红屏 |

![并发窗口](../build/m6a/06-concurrent.png)

源码基线 `build/m6a-source-baseline.zip` 保留接手时文件（Makefile 第 1 条依赖修复之后）。用户字库本次未改动，SHA256：
`1ef3876e7adbf0f42dc432dbdf21c927282a7e28bb076613c6cb50fbf0de9b55`。

## 修订记录

| 日期 | 变更 |
|---|---|
| 2026-10-02 | 初版：M6a 实现、自动化行为断言与截图验收包，用户界面验收待确认 |
| 2026-10-02 | 按 M6 最终调度策略澄清只有 EXIT 立即调度；普通系统调用避免逐次让出造成输入延迟，基础测试再次通过 |
