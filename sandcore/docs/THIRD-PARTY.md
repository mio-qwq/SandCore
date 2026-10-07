# 第三方代码与资源来源

> 当前验证范围已更新，见末尾“2026-10-05 M9最终验证记录”及[M9验收报告](M9-ACCEPTANCE.md)。早期未验证/待验标记保留阶段背景。

> 2026-10-05：按用户要求建立统一署名清单。此表记录仓库中的源码和接线，
> 不把“已引入”视为“已构建、已运行”。M9已进入统一构建/运行；原221份
> 客体归档已实际核对，部分解码/压缩已运行，完整输入矩阵仍待验收。

用户最新规则已持久化到根`AGENTS.md`第十节：只引入署名/保留声明类
宽松许可或更宽松的组件，完整遵守实际条款；所有引入的第三方开源源码
及其版权/许可原文归档到`/SYS/LICENSE/`，不以一行署名代替完整声明。

## 系统组件引入的代码

| 组件 | 使用范围 | 固定版本 / 提交 | 采用的许可与署名 | 本地原文 |
|---|---|---|---|---|
| Monocypher | M10a1 CORE扩展的Ed25519公开验签；内核只引用验签路径 | 4.0.3；`ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f` | 选择BSD-2-Clause；Loup Vaillant及AUTHORS列出的作者；双许可及内部实现原声明完整保留 | [LICENCE.md](../third_party/monocypher/LICENCE.md)、[AUTHORS](../third_party/monocypher/AUTHORS.md)、[PROVENANCE](../third_party/monocypher/PROVENANCE.md) |
| stb_image | PNG/JPEG图片解码；三环IMAGE服务 | v2.30；`2c980bb59875b0d32144a71867fbdebb2f77cd20` | MIT；Sean Barrett及文件内列出的贡献者；双许可原文完整保留 | [LICENSE](../third_party/stb/LICENSE)、[源码](../third_party/stb/stb_image.h) |
| libwebp | WebP图片解码；三环IMAGE服务 | `a1d89ff209ca01e7a87aca64317201890bac2749` | BSD-3-Clause；Google Inc.及AUTHORS列出的贡献者；保留专利授权原文 | [COPYING](../third_party/libwebp/COPYING)、[AUTHORS](../third_party/libwebp/AUTHORS)、[PATENTS](../third_party/libwebp/PATENTS) |
| dr_mp3 | M9 MP3解码；GUI/CLI播放器共用核心 | 头文件标识v0.7.4；`dfe8377631000664666519fdb83da193fd8037f4` | MIT No Attribution；David Reid；即使上游不强制署名，本项目仍署名并保留原文 | [许可证](../user/audio/vendor/LICENSE)、[来源与摘要](../user/audio/vendor/PROVENANCE.md)、[源码末尾声明](../user/audio/vendor/dr_mp3.h) |
| minimp3（包含在dr_mp3内） | dr_mp3解码实现的上游基础，非另一份独立链接库 | 随上述dr_mp3固定头文件；未另取独立minimp3快照 | 头文件末尾保留作者的CC0/public-domain声明；来源为lieff/minimp3及其贡献者 | [原声明副本](../user/audio/vendor/MINIMP3-NOTICE)、[上游](https://github.com/lieff/minimp3) |
| miniz | M9 gzip/gunzip/zcat/unzip共用整数DEFLATE压缩/解压核心 | 头文件标识3.1.2；`b9dd683c42c965a5e98bdb9d4eb322defd966a7c` | MIT；RAD Game Tools、Valve Software、Rich Geldreich、Tenacious Software LLC及源文件贡献者 | [LICENSE](../user/compress/vendor/LICENSE)、[来源与逐文件摘要](../user/compress/vendor/PROVENANCE.md) |
| libbzip2 | M9 bzip2/bunzip2/bzcat整数压缩/解码核心 | 1.0.8官方归档，SHA256固定登记 | bzip2宽松许可；Julian Seward；完整保留声明/免责声明/来源及禁止背书等要求 | [LICENSE](../user/pack/bzip2/LICENSE)、[README](../user/pack/bzip2/README)、[固定来源](../user/pack/bzip2/PROVENANCE.md) |
| LZMA SDK decoder | M9 unlzma/lzmacat的Alone解码核心 | 26.03官方归档；仅选LzmaDec.c及头文件 | Igor Pavlov；公有领域；仍保留原作者/发布说明；不引入SDK的PPMd/SHA256/其它容器或宿主二进制 | [发布/许可说明](../user/pack/lzma/DOC/lzma-sdk.txt)、[固定来源](../user/pack/lzma/PROVENANCE.md) |
| lwIP | M10a1 IPv4主机栈；不启IPv6/PPP/TLS/上游应用 | 2.2.1，STABLE-2_2_1_RELEASE解引用提交77dcd25a72509eb83f72b033d219b1d40cd8eb95 | BSD-3-Clause；无条件引用的PPP声明头另有MBSI/GES保留声明宽松条款，完整原文及内含组件登记；完整版权、免责和禁止背书条款保留 | [COPYING](../third_party/lwip/COPYING)、[固定153份原文件/摘要说明](../third_party/lwip/PROVENANCE.md)、[M10逐文件表](../third_party/M10-SOURCES.json) |

stb/libwebp官方仓库分别为[stb](https://github.com/nothings/stb)和
[libwebp](https://github.com/webmproject/libwebp)；原始文件逐项SHA256见
[SOURCES.json](../third_party/SOURCES.json)。dr_mp3上游为
[dr_libs](https://github.com/mackron/dr_libs)，miniz上游为
[miniz](https://github.com/richgel999/miniz)。固定提交优先于浮动分支。

图片解码沿用M8a组件；历史验证只覆盖其当时记录的输入和运行盘，见
[IMAGE-SERVICE.md](IMAGE-SERVICE.md)。M9音频和压缩组件已整包构建，
06实际MP3/WAV、gzip/ZIP/bzip2/LZMA及独立压缩校验用例通过；不能因
这些分项或上游成熟而宣布完整SandCore适配验收通过。

## 自写代码与适配范围

M10a1新增Monocypher固定原文件7份及独立PROVENANCE，全部逐字节摘要登记于[M10-SOURCES.json](../third_party/M10-SOURCES.json)。官方来源为[Monocypher](https://github.com/LoupVaillant/Monocypher/tree/4.0.3)，官方codeload归档另保留third_party/imports/monocypher-4.0.3.zip，归档SHA256为3f2a78032686ae8f085bd7892a890e932cf4f6ce42328214ce7aaf14d58b6c18。原源码不改，包含可选标准Ed25519与SHA512；上游AUTHORS中列出的SUPERCOP/ref10等声明随包保留。内核只调用公开验签，通过函数/数据节回收未引用算法；公开接口包装与更严格点编码检查在kernel/core_signature.c。源码含上游其它功能不意味着客体暴露私钥签署接口。规则复制完整选用原源码、README/作者/许可/来源到SYS/LICENSE/MONOCYPHER，M10.JSON登记摘要。尚未执行构建或客体归档核对。

引导器、内核、SandFS、权限/串口协议、窗口系统、SCCC/SandAsm、Shell及
独立CLI实现由本项目编写。BusyBox只用于功能/命令行为对照，没有移植
BusyBox源码，也没有用它的多调用二进制替代独立可执行文件。

`user/codec/`、`user/audio/`、`user/compress/`、`user/pack/`中的SandCore接口、资源生命周期、
私有内存适配及构建胶水与上游快照分开。GZIP/ZIP容器、路径校验、命令行和
SandFS事务由本项目编写；miniz仅采用低层tdefl/tinfl核心。`miniz_zip.h`是
上游总头文件的包含依赖，不意味着使用了miniz的ZIP归档实现。

libwebp构建副本由[adapt_webp.py](../tools/adapt_webp.py)生成：保留版权与原始
源码快照，检查登记摘要后替换随机幅度的浮点运算，并关闭未使用的编码gamma。
生成副本及其摘要另存于构建目录。音频采用私有回调、关闭stdio；库的浮点仅
在三环音频组件使用。私有兼容头不向`SYS/INC`发布标准C运行库。

libbzip2原文件未改，用BZ_NO_STDIO和私有分配头关闭标准库路径，独立命令
及事务由项目编写。LZMA原选中文件未改，包装/字典限制/回调分配与CLI由
项目编写。只引入解码器，不把SDK发布说明讨论的其它算法隐含装入系统。
原官方归档另保留在third_party/imports；逐文件摘要在M9-SOURCES.json，
摘要记录不声称验证了上游数字签名；适配已有构建/分项运行证据，完整
外部编码器/坏数据/资源矩阵仍未完成。

## 字体与资源

| 资源 | 来源 | 许可 / 记录 |
|---|---|---|
| 凤凰点阵体 / Vonwaon Bitmap 1.02 | Haoyu Qiu (Timothy Qiu)；16px字形用于系统字体 | CC0；[官方页](https://timothyqiu.itch.io/vonwaon-bitmap)、[原压缩包license.txt副本](../assets/licenses/VONWAON.LIC)；提取流程与来源见`kernel/font16.txt`头部 |
| M10a1原始12/16 TTF完整脸 | 同一用户原ZIP，不改轮廓或字符表 | CC0-1.0；[完整原声明](../third_party/vonwaon/license.txt)、[法律文本](../third_party/vonwaon/CC0-1.0.txt)、[来源/摘要](../third_party/vonwaon/PROVENANCE.md)，完整原字体另归SYS/LICENSE/VONWAON |
| 用户指定Logo/壁纸 | 用户提供、指定的素材 | 保留原来源；本清单不为这些素材补造许可证或作者声明 |
| 默认真彩图片 | 项目已有资源 | 来源单独记录于[WELCOME-TRUECOLOR-V1.md](../assets/pictures/WELCOME-TRUECOLOR-V1.md) |
| M9系统提示音 | 自写`tools/mksounds.py`合成 | 尚未执行生成器；不使用外部音效采样 |

## 宿主工具与交付接线

GCC/工具链、NASM、QEMU、Python及宿主脚本使用的Pillow属于开发环境依赖；
其实现代码不因此成为SandCore内核或CLI源码。旧影片工程下载的Blender、
FFmpeg/imageio-ffmpeg及其依赖属于封存的宿主工具，保留各自随包许可证；
不把这些GPL/LGPL工具的许可说成系统解码组件的许可。宿主OpenCL驱动/API
也不代表SandCore已有GPU驱动。今后若分发这些工具本体，要以实际分发清单
记录其版本、依赖与完整随包声明；当前M9接线不复制宿主工具目录到客体盘。

M10a1的OpenSSL仅作为独立宿主**公开验签**工具，使用已有工具，不复制其实现或本体到客体；不读取用户私钥。用户离线签署由用户自行操作，合同见[CORE.md](CORE.md)。如果以后分发OpenSSL本体，须另登记实际版本、依赖和随包条款。

M10a1全量字形验收使用已有宿主Pillow / FreeType作默认hint差异诊断，正式原轮廓参考用自写ctypes脚本动态调用已有WSL FreeType，明确关闭hint、自动调形与嵌入位图。当前font-04实际报告版本2.14.2，共享库SHA de0b38b01924302a7fa7bfbaee47afcb9468ad2f8a22b4a8cca56ad54f64569c；此前批次的软件版本以其独立记录为准，不能用旧环境版本替换当前报告。每份参考报告记录实际共享库/原TTF/客体转储摘要、加载标志与全映射像素结果，不复制或链接这些参考工具到SandCore内核、CLI或客体盘。两来源两脸实际各7540映射逐像素通过，完整字体/Notes合同未收齐；参考工具不是新增系统运行组件。以后若分发工具本体，另列其实际版本/依赖/许可证，不用本项目运行组件声明代替。

M9构建规则将统一说明与独立原文安装到`/SYS/LICENSE/`：

- `STB/`、`LIBWEBP/`、`DR_MP3/`、`MINIZ/`、`BZIP2/`、`LZMA/`：实际引入的完整固定源码快照及随附声明/来源；不只是许可证文件。宿主原源码路径和已有开发源码接线继续保留。
- `THIRDPARTY.MD`：本说明；`IMAGE.JSON`：图片源码固定来源与摘要；`M9.JSON`：六个直接代码组件、内含实现、许可选择及源码归档路径。
- `STB.LIC`、`WEBP.LIC`、`WEBP.AUTHORS`、`WEBP.PATENTS`：图片库原文。
- `DR_MP3.LIC`：dr_libs原文与嵌入minimp3声明；`DR_MP3.MD`：固定来源及摘要。
- `MINIZ.LIC`、`MINIZ.MD`：压缩库原文与固定来源及摘要。
- `VONWAON.LIC`：字体归档原声明。

既有`/SYS/CORE/IMAGE.LIC`与历史发布包保留。上述M9规则未执行，不能称
当前镜像已经含有这些新增文件。后续引入新的第三方组件时，同批更新本表、
固定来源、必要版权/许可原文和对应交付规则。

`tools/audit_third_party.py`已接发布门槛，检查固定文件集合、SHA256、登记的
许可选择/嵌入组件、客体63B路径及无符号链接，`--tree`核对源码归档字节。
它不替代阅读许可证实际条款，也不生成产品运行结论；当前尚未执行。

## 修订记录

2026-10-07：登记全量字形独立宿主Pillow/FreeType参考与固定模式/摘要记录；动态调用已有共享库，不随系统复制或链接，实际参考结果与完整验收范围分列。

2026-10-06：统一构建补上游无条件引用的两个PPP原始头，固定来源153份；协议仍关闭。逐字归档MBSI/GES版权/授权/免责并单列内含组件许可，未改第三方原字节。

2026-10-06：登记lwIP 2.2.1/固定提交/BSD原声明及151份未经改写的实际引用源码/公共头，完整复制SYS/LICENSE/LWIP。私有freestanding适配在kernel/net_port，原始ZIP仅宿主来源证据；自写驱动/桥/socket源码归SYS/NETSRC。原IPv6声明头保留而未编译IPv6实现，未引PPP/TLS/上游应用或外部依赖。构建审计与客体归档规则尚未执行。

2026-10-06：登记Monocypher 4.0.3/BSD-2-Clause固定原源码、内含实现原声明与完整客体归档接线；源码/归档规则未构建未执行。OpenSSL仅宿主公开验签。
2026-10-06：原样归档用户凤凰12/16 TTF及原声明/完整CC0法律文本、M10固定摘要与运行/许可复制规则；未构建未运行。

| 日期 | 变更 |
|---|---|
| 2026-10-05 | 登记四个直接代码组件、dr_mp3内的minimp3、凤凰字体、适配范围与宿主工具边界；新增M9统一署名/许可证交付接线，未构建运行 |
| 2026-10-05 | 按用户新规补完整第三方源码归档目录与宽松许可门槛，根AGENTS.md同批持久化；固定快照184个libwebp文件的客体路径静态检查未超63B，打包容量仍待统一验证 |
| 2026-10-05 | 引入官方libbzip2 1.0.8与LZMA SDK 26.03选用解码核心，原文/固定摘要/适配/客体完整源码接线同批登记；未构建运行 |


### 2026-10-05追加dr_flac

dr_flac v0.13.4，David Reid，固定dr_libs提交dfe8377631000664666519fdb83da193fd8037f4；选择MIT No Attribution (MIT-0)，保留上游双许可原文和版权。官方源：https://github.com/mackron/dr_libs/blob/dfe8377631000664666519fdb83da193fd8037f4/dr_flac.h 。本地user/audio/flacvendor固定四文件，客体SYS/LICENSE/DR_FLAC全文归档；独立DR_FLAC.LIC/MD便于阅读。私有流/内存包装不改上游头，关闭stdio/SIMD，保留CRC；图片继续使用原IMAGE服务，未新引其它图片库。归档从221增加至225，本批实际audit已确认固定来源与客体副本一致。宿主FFmpeg只作为独立编码测试工具，不复制进系统或试玩包。

修订：2026-10-05，登记dr_flac来源/许可/完整源码归档及独立测试工具，构建运行待验。


修订：2026-10-05，本轮播放器07双盘88项、整数无FPU20项、身份/旧程序54项、默认音效/关机和宿主进度17项通过，范围与全部失败见M9-VERIFICATION.md；独立FLAC试玩包保留旧基线/会话，完整M9仍未验收。

## 2026-10-05 M9最终验证记录

七个直接代码组件及225份实际引用源码/原文归档核对通过，/SYS/LICENSE完整保留。FFmpeg/GNU as仅用作宿主独立参考，不随产品复制或链接。最终源码/产物未增加第三方运行组件。

修订：2026-10-05，记录实际范围与证据，待用户验收；前述早期未验证叙述保留为历史。

### 2026-10-07短租约实际失败与适配修复待验

原desktop02主核的dhcp-lifecycle01退出1。探针真实绑定60秒租约/
T1=20/T2=40后等待3500 PIT tick，peak_lease_seconds=0、exit1；
独立PCAP/LeasePeer只有两次DISCOVER/SELECT，没有自动RENEW，
对端没有错误。失败快照仍BOUND10/地址10.23.0.2/租约60、使用0，
驱动/收包坏帧/坏释放未报错。原FAIL、输入、完整冻结和现场保持。
不是放宽480秒宿主期限或改静态配置后把此用例标PASS。

适配已改DHCP_COARSE_TIMER_SECS=1，单IPv4接口每秒一回调，
DHCP_TIMEOUT_SIZE_T为32位，并使用上游支持的自定义换算宏，
保留全部32位有限秒数，0秒转最小1秒、原60秒重试阈值/退避保留。
NETINFO仍原96字布局，其租约使用字段保持秒含义。私有dhcp.c
原样包含上游翻译单元，只更名粗定时函数并在同一网络上下文清除
有限→无限租约时被bind跳过赋值遗留的旧t0/T1/T2。上游固定BSD
原文件不改，完整声明及实际适配源码继续进SYS/LICENSE/NETSRC。

这些修复尚未构建或运行，不能用旧308项/资源PF结果充新主核
验收。新独立dhcp-fix01构建/双盘迁移/原三策略重测正在准备；
长租约和无限租约的实测、其它完整网络与整机验收继续。主核改变
不改变用户已签扩展，不需要读取私钥或重新签原模块。

修订：2026-10-07，登记60秒短租约自动续期真实FAIL与1秒/32位/无限过渡适配待验，保留原超时和完整目标。


2026-10-07 DHCP私有派生更新：短租约三策略双来源22项实际通过；长租约省略T2的真实边界发现上游先32位乘7溢出。tools/prepare_lwip_dhcp.py严格核对固定原件SHAf89cf1aacbd9a04dcb47a03c81a1a231331f4edb323d2e0cf26e640ca78c3453，仅替换一次默认T2表达式为t-ceil(t/8)，完整版权/BSD条文逐字节保留。原件third_party/lwip/src/core/ipv4/dhcp.c及固定摘要不变，构建派生dhcp_upstream.inc与DHCPPREP.PY进SYS/NETSRC/PORT，所有上游源码继续SYS/LICENSE/LWIP。私有dhcp.c继续处理无限租约旧计数。新候选尚未构建/运行，不借旧22或308项结果宣称通过。

修订：2026-10-07，登记真实默认T2溢出、一处受固定摘要保护的BSD派生及完整实际源码归档待验。
