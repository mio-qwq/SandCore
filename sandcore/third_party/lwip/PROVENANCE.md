# lwIP 2.2.1 固定源码

官方仓库：https://github.com/lwip-tcpip/lwip 。标签 `STABLE-2_2_1_RELEASE`
解引用提交 `77dcd25a72509eb83f72b033d219b1d40cd8eb95`，标签对象
`009c2256469004009488b3385ba269461e8eb616`。宿主原始 ZIP 位于
`third_party/imports/lwip-2.2.1.zip`，SHA-256
`e09bf3518f1d23204cdd38203e1bdbbb5c634a1dce8dde487b7eaca3f2407245`。

本目录保留未经改写的 COPYING、README、CHANGELOG、IPv4/公共核心、
完整公共头文件与 Ethernet 适配源码，共 153 个原始文件。采用各文件自带的
宽松条款；完整版权、作者、免责与禁止背书声明原样保留。包含 IPv6 声明的
头文件是公共头文件的一部分，本轮不编译 IPv6 实现，不启用 PPP、TLS、
上游应用或其外部依赖。原始 ZIP 仅作为宿主来源证据，不作为运行组件发布。

统一构建发现上游 init.c/memp.c/ethernet.c 无条件包含 PPP 选项，
init.c 还包含 PPP 声明。因此从同一固定 ZIP 补原始 ppp_opts.h 与
ppp_impl.h，不修改上游翻译单元。前者为原 BSD-3-Clause 条款；后者
明确授予任意用途使用、复制、修改、分发和授权，仅要求保留版权与
在分发中逐字附上声明/免责。Marc Boucher/MBSI（2003）与 Global
Election Systems（1997）原文完整随头文件归档，逐文件表另登记
LicenseRef-MBSI-GES-Permissive；没有将它误标为统一 BSD 许可证。
PPP_SUPPORT/PPPOE/PPPOL2TP/PPPOS 全为0，不引入PPP实现或其它依赖。

SandCore 适配位于 `kernel/net_port/`，不改写本目录。自写私有字符串操作、
物理页/小块分配器、NO_SYS 时钟、随机数、受控服务上下文与定时器预算；
这些接口不是系统用户态 C 运行库。原始超时文件在独立适配翻译单元中包含，
其原函数更名保留，再提供有单批预算的主循环入口。e1000 驱动为项目自写。
原始文件集合和逐文件摘要登记在 `third_party/M10-SOURCES.json`；构建时
完整复制本目录至 `/SYS/LICENSE/LWIP/`，同时归档本项目适配源码与配置。

状态：统一构建与修复中；尚无整包成功或客体网络验收。
