# LZMA SDK 解码器固定快照

来源：<https://www.7-zip.org/sdk.html>，官方 26.03 发布归档
<https://github.com/ip7z/7zip/releases/download/26.03/lzma2603.7z>。
原压缩包 SHA256：86c213f752520ab5325c310f50bef63ec344b56dd1c80b0246d06dc6cec953b2。
原包另存 third_party/imports/lzma2603.7z；摘要是来源固定记录，未声称验证签名。

只引入 C/LzmaDec.c 及其五个选用/参考头文件，保留原字节；完整发布声明
DOC/lzma-sdk.txt 也归档。作者 Igor Pavlov，上游把 SDK 置于公有领域；
各原文件的作者/公有领域声明与发布说明完整保留。
发布说明还讨论 PPMd、SHA256 的来源；本组件没有引入它们，也没有引入
整个 SDK 的其它格式/编码器/宿主二进制。不得把 SDK 名称当成隐藏的全量依赖。

SandCore 自写 .lzma 包装、流读取、字典/输出上限和 SandFS 事务在本目录之外，
私有头只给固定 i386 类型与字节操作，不装入 SYS/INC 或提供标准 C 运行库。
支持 LZMA-Alone 解码，未知长度要求真实结束标记；不声称实现 XZ/7z 容器。
源码状态 SOURCE-UNVERIFIED。
