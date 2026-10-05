# Windows QEMU 硬件加速与软件回退

> mio，2026-10-04。硬件加速接线保留原设备与32位内核；本轮WHPX
> 初始化、实际双盘与同源World对照已执行；仍不代表全游戏60FPS。

WHPX使用Windows Hypervisor Platform，减少TCG的软件指令翻译成本。
它不是显卡3D加速，也不会自动把单CPU内核改成多核系统。
SandCore继续单vCPU、qemu32客体CPU、传统PC、PS/2、VGA/std和ATA。
Windows `qemu-system-x86_64.exe`可以执行32位SandCore，名称不表示
内核升级到64位。所有路径仍须if=floppy引导盘+IDE数据盘。

本机QEMU 11.1.0的`qemu-system-i386.exe -accel help`仅TCG；同目录
x86_64文件提供TCG/WHPX。Windows原HypervisorPresent=true、VMP已
启用，HypervisorPlatform为Disabled。本轮以NoRestart启用该接口，
系统返回RestartNeeded=true，未自动重启电脑；当前实际WHPX分区
初始化/QMP握手/正常退出已经成功，不能仅靠功能状态猜是否能用。

## 启动方式

```bat
run.bat
run.bat whpx
run.bat tcg
run.bat whpx headless
```

缺省auto：有WHPX编译支持时优先尝试，初始化失败由QEMU明确输出
错误后回退TCG；运行后客体异常不触发重启。显式whpx失败立即停，
不回退冒充硬件性能；显式tcg保留原i386软件路径。`temp miotest`
的独立run.bat采用同样选择，但仅访问该目录的两张盘。

Makefile的Windows/WSL互操作使用Windows x86_64 QEMU；
`make run QEMU_ACCEL=whpx`和`make run QEMU_ACCEL=tcg`可显式选择，
缺省auto。原Windows i386文件继续存在并可用QEMU变量指定。
构建仍是build.bat/WSL ELF，不改变内核编译器或发布来源。

## 可复现测试与身份

第二阶段公用`verify_truecolor.launch`通过qemu_config.py选择，
历史测试缺省保持tcg。每次启动保存精确命令、宿主可执行文件SHA、
模式和stderr，不修改已有报告。设置环境变量
`SANDCORE_QEMU_ACCEL=whpx`才启用明确硬件路径；auto探测暂停无盘
VM，用临时QMP端口握手后退出，正式启动选定模式后失败不回退。
`SANDCORE_QEMU_TCG_TARGET=x86_64`允许与WHPX使用同一宿主文件，
消除i386/x86_64目标差异；未设置仍是历史i386。

游戏观察器增加`--accel tcg|whpx --tcg-x64`。对照须同一SCX/map、
内核/盘输入、分辨率/主题/玩法和绘制尺寸，分别记录墙钟帧率、
PIT和宿主一核心CPU。CPU100%不自动等于硬件不足；WHPX通过也不
免除渲染算法优化、画质、输入延迟和长尾要求。

`build/m8-phase2-resume-20261003-01/acceleration-01`保存启用前后
Windows功能状态和真实探测。`probe-03.json`为实际成功的TCP QMP
握手/quit记录。首两次stdio探测在初始化时丢字符而超时，保留
原输出，不将观察器失败报告成硬件不可用；后续双盘运行另记录。

依据：[QEMU官方WHPX文档](https://www.qemu.org/docs/master/system/whpx.html)、
[Microsoft平台API](https://learn.microsoft.com/en-us/virtualization/api/hypervisor-platform/hypervisor-platform)。
旧VGA模式、PIC和MMIO兼容仍实际验证，必要时用原TCG；不为了
加速删设备或开放内核浮点，也不把SIMD直接写MMIO的兼容限制忽略。

## 修订记录

2026-10-04：接口NoRestart启用、宿主文件能力识别、auto/明确模式、
同文件TCG对照和可复现启动身份接线；完整双盘/游戏性能继续。

2026-10-04实际验收：launchers-03两份冻结batch各auto/tcg/whpx共6项，
实际cmd执行、无头双盘、欢迎页/鼠标开始菜单/Shell/正常退出PASS。
另外的镜像副本用于验证，试玩盘前后SHA相同。首轮观察器未等端口，
次轮混合LF/CRLF使cmd找不到TCG标签，失败保留；统一纯ASCII/CRLF
并等待实际监听后才通过。显式whpx World运行/捕获/暂停/关闭及
严格物理页回收、零输入溢出通过，详细量化见PERFORMANCE.md。
