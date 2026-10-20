# SBNET.SCL

作者：mio。公开头文件 include/SBNET.H。导出 SbNetCreate、SbNetFetch、SbNetRelease、SbNetDestroy；另有浏览器专用 worker 入口 SbNetMain。

创建 context 后，以 size/version=sizeof(record)/1 的 SbNetRequest 发起 GET、HEAD 或 POST，SbNetResult 持有响应。SbNetFetch 为可取消的同步调用；回调可泵消息并返回非零取消。结束后 SbNetRelease 释放正文，最后 SbNetDestroy 关闭 context。复用结果前先释放旧正文。一个 context 供单线程使用。

网络进程使用同一 API 对 UI 的请求做分帧回传。GUI 不在自己的线程等待 DNS、TLS 或正文。取消回调同时检查 owner 通道关闭、SHUTDOWN 和导航取消。

HTTP 验证状态行、头长、Content-Length、分块边界与正文完整性，限制响应/解压大小、超时与十次重定向。TLS 错误报告 BearSSL 错误号，不能获得安全标记。gzip 校验 CRC/ISIZE；zlib deflate 验证校验和。

TLS 1.2、RDRAND、UTC、信任锚和 Cookie 限制见 README 与安装帮助。没有 TLS 1.3、完整现代 Cookie 策略或 Chrome 根证书策略兼容声明。
