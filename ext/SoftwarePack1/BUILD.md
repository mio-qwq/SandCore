# SoftwarePack1 构建与验证

实际验证工具链：WSL GCC 15.2、GNU ld、nasm、Python 3。目标为 freestanding i386，无 libc/libm/libgcc；平台链接基址 0x400000、128 KiB 栈。使用 -Os 和内联字符串操作减少装载正文，ABI 与平台一致。

在 WSL 进入本目录后：

```sh
env -u OS make -j4
python3 tests/run_host.py
python3 tools/preflight.py
```

Makefile 采用 GNU make 4.3+ grouped targets 和编译依赖文件，包含片段修改也会触发重建。Windows 分支保留 MinGW PE 挑段支持，本轮最终产物由 WSL 构建，未把旧 MinGW 验证作为本轮证据。

图标源 icons/*.txt 由 tools/make_icons.py 生成并可手工编辑，../tools/mkicon.py 输出 SCB2，../tools/mkscx.py 原样封装 SCX。五个应用和图标由 tools/pack_payload.py 做包内 SPLZ1MIO 压缩、回解逐字节核对与 CRC32，再编译入安装器。平台收到的仍是原始标准 SCX/SCB2。

tests/run_host.py 只在 build/host 复制平台头，替换 syscall 桥为 64 位宿主模拟调用；编译真实应用源码，启用 ASan/UBSan，用原凤凰 ASCII 字形绘制界面。测试覆盖表达式、CSV/公式、失败保留数据、Hold/锁定/顶出、换弹/伤害/碰撞/关卡可达性、音频尾巴、安装/资源拒绝与缩放布局。模拟器不能证明真实内核、硬件或权限行为。

tools/preflight.py 验证所有 SCX 的头/装载+BSS上限/图标精确长度、压缩回解、未解析链接符号，以及平台头和打包器未变。输出 build/preflight.json，记录 SHA256。所有中间物在 build，唯一安装交付物在 dist。

不使用 64 位除法/取模或 double 到 i64 转换，避免引入 libgcc 符号。ELF 链接器的 RWX 段告警来自平台平映像链接脚本；交付的是 SCX，检查无未解析符号。没有进行客体启动或实机测试。
