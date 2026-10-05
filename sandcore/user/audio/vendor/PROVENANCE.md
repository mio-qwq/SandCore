# dr_mp3 来源

- 上游：https://github.com/mackron/dr_libs
- 固定提交：dfe8377631000664666519fdb83da193fd8037f4
- dr_mp3.h SHA-256：997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f
- LICENSE SHA-256：dd1c647e6f767f8ff4b2dfae0fed314726600a01e0cf1ef556afddd5fa96ff15
- 选择上游提供的 MIT No Attribution；保留完整版权/许可证与上游文件。
- 2026-10-04仅下载固定源码用于实现；尚未编译或执行。

头文件末尾另有Copyright 2023 David Reid及内含minimp3的CC0声明，
上游根LICENSE的年份为2020，两处原文均保留。发布DR_MP3.LIC将根LICENSE、
DR-MP3-NOTICE与MINIMP3-NOTICE合并，源码头文件保留完整末尾声明。minimp3没有另取独立
快照，其版本绑定本次dr_mp3提交。统一清单见docs/THIRD-PARTY.md。

宿主引导编译成SandCore三环程序，DR_MP3_NO_STDIO，私有内存回调。
本轮先使用标量解码器；内部浮点仅在三环组件，不扩展SCCC浮点语言或内核。
