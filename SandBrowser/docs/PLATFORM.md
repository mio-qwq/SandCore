# 平台与进程边界

作者：mio。既有 sandcore/、ext/、公共库和构建工具仅被读取，所有适配在 SandBrowser 内。

## 平台接口

- SC32API/SCCLIB 的 cdecl 导出用于进程、文件对象、GUI 消息、字体、图片、IPC 与共享内存。
- SCNET.H 是 socket/DNS 的源码便利层，没有 SCNET.SCL，也没有相应的正式 SC32 socket 导出。SBNET.SCL 内部保留很窄的寄存器桥，只使用既有 0x252–0x262 网络入口，不修改内核。
- SCX 的初始化正文仍受 262108 字节上限约束，因此三部分分别使用小 SCX 入口与私有 SCL。没有修改装载器或添加 SCX 尾部载荷。
- GNU ELF32 的真实符号、段和重定位封装为标准 SCL。导入逐项匹配平台导出；缺实现即停止。
- C++ 补充使用真实数学、setjmp、分配、格式化与容器实现，没有返回成功的空函数。编译禁用异常/RTTI；不可恢复的容器错误终止页面 worker，由 UI 报告。

## IPC 与绘制

UI 建立限当前会话、UID/GID、realm 的 endpoint，再通过作业信息获得子进程的准确 PID/generation。接入时比较内核给出的 peer 身份；worker 也核对 UI 的 PID/generation、会话和身份。线上包不含私有指针。

资源请求带请求号与导航 epoch。网络进程拥有 socket、TLS context 和 Cookie。资源通过限长消息回传；旧导航响应被丢弃，Stop 和新导航取消旧请求。

UI 建立共享像素对象，只向准确的页面子进程身份授权。页面写完一帧发布 FRAME，UI 拷贝后 ACK；页面等 ACK 再改写。UI 拥有原生窗口和提交画布，网络进程不传 socket 句柄给其他进程。

结束时关闭通道、等待/取消子作业、撤销 grant、解除映射并释放对象。worker 非正常退出有提示；自动重建尚未实现。

## 系统资源

字体通过 ScFontGetGlyph 按字号栅格化，保留真实 advance/baseline；绘制显式裁剪。系统图片服务提供 PNG/JPEG/WebP/BMP，stb_image 补充 GIF 首帧。没有宿主代理服务器或宿主浏览器参与浏览。

小文件用 ScPathReplace 整体替换；大文件用 ScFileOpen/Write/Flush/Close 分块写，处理 AGAIN 和实际已写前缀，避开 Path 接口的 64 KiB 上限。

TLS 强熵来自 CPUID 检测后的 RDSEED / RDRAND，失败即拒绝 HTTPS。UTC 优先来自 ScSystemClock；不可用或证书日期错误时，网络进程通过既有 UDP 接口查询时间服务器，核对两个不同地址后按 ScSystemMonotonic 推进。无全局时钟写入、时间/计数器熵或证书绕过开关；网络时间的具体限制见 NETWORK.md。

页面缩放使用独立 SB_ZOOM 消息和共享帧中的缩放标记。CSS 布局按物理视口 / 缩放比例计算，媒体查询随视口更新，字体按物理字号栅格化；页面指针转换回 CSS 坐标，滚动尺寸转换为物理像素。UI 丢弃旧缩放的帧但仍 ACK，避免缩放时旧帧覆盖新滚动位置。

## 验证状态

只检查编译、链接、导入、SCX/SCL 格式与发行摘要。未启动客体，未宣称跨站兼容通过。产物和 SHA-256 见 dist/BUILD.json。
