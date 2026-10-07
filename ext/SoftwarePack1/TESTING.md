# 正式包验证记录

用户于 2026-10-06 声明完全验收主体功能。本次补齐六个 About 页面、版权、作者链接、made with ❤️；所有修改与产物只位于 ext，未启动 SandCore、QEMU 或实机。

| 源码宿主测试 | 通过检查数 |
|---|---:|
| utils/pcalc/audio | 69 |
| sheet | 55 |
| blocks | 119 |
| hex | 20 |
| raider | 137 |
| installer/payload | 42 |
| 合计 | 442 |

采用真实应用源码与私有宿主 syscall 桩，启用 ASan/UBSan。保留原有表达式、CSV、文件失败、方块锁定/Hold、六关路径、战斗、音频、压缩/安装检查；本次增加关于页的鼠标 Back / Enter / Esc 关闭、编辑内容保留、游戏暂停/释放鼠标、安装器不改变文件或页面，以及 200%/深色主题绘图检查。

最终构建日志 build/release-build.log；测试日志 build/host-release-tests.log；产物检查 build/release-preflight.log 与 preflight.json。SCX 头/尺寸/32px 图标/压缩回解/身份字符串/链接符号全部通过，平台 SCAPI/NUI/SCMEM 与打包器逐字节一致。链接器保留平映像脚本的 RWX 提示，无 C 编译错误或未解析符号。

build/about-preview.png 是六个关于页的源码绘图预览，非客体截图。用户验收与本次宿主证据分别记录，未新增客体运行声明。

进入方式：五个应用与安装器的页面底部 About；Raider 在菜单/暂停页。Back/Enter/Esc 返回，退出关于页不会退出应用或丢失未保存编辑。

原 baseline 与署名前的用户已验收安装器均保留在 ext/SoftwarePack1-baseline-20261006，未包含在最终 SCX 中。
