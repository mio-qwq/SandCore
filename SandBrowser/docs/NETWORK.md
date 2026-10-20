# SBNET.SCL

作者：mio。公开头文件 include/SBNET.H。导出 SbNetCreate、SbNetFetch、SbNetRelease、SbNetDestroy；另有浏览器专用 worker 入口 SbNetMain。

创建 context 后，以 size/version=sizeof(record)/1 的 SbNetRequest 发起 GET、HEAD 或 POST，SbNetResult 持有响应。SbNetFetch 为可取消的同步调用；回调可泵消息并返回非零取消。结束后 SbNetRelease 释放正文，最后 SbNetDestroy 关闭 context。复用结果前先释放旧正文。一个 context 供单线程使用。

网络进程使用同一 API 对 UI 的请求做分帧回传。GUI 不在自己的线程等待 DNS、TLS 或正文。取消回调同时检查 owner 通道关闭、SHUTDOWN 和导航取消。

HTTP 验证状态行、头长、Content-Length、分块边界与正文完整性，限制响应/解压大小、超时与十次重定向。TLS 错误报告 BearSSL 错误号，不能获得安全标记。gzip 校验 CRC/ISIZE；zlib deflate 验证校验和。

TLS 1.2、硬件熵、UTC、信任锚和 Cookie 限制见 README 与安装帮助。没有 TLS 1.3、完整现代 Cookie 策略或 Chrome 根证书策略兼容声明。

## 随机数与启动配置

CPUID 检测 RDSEED 与 RDRAND，先尝试 RDSEED，硬件繁忙时重试并回退到 RDRAND。没有可用硬件源或指令持续失败时拒绝 HTTPS，失败缓冲区清零。时间和计数器不作为 TLS 熵。M10a2 的 qemu32 默认隐藏 RDRAND，根目录 RUN-HTTPS.BAT 通过 tools/run_https.py 包装原启动器，仅向这次 QEMU 命令的 CPU 参数添加 +rdrand；不修改系统文件。切换 CPU 功能需要关闭旧 QEMU 后重新启动。

## 无本地时间时的证书时钟

先读取 ScSystemClock，严格验证 2020–2099 年内的日历日期。本地 UTC 无效时，以 SNTP 请求查询 ntp.aliyun.com、time.windows.com、time.cloudflare.com。每个域名解析和 UDP 请求分别限时两秒，总流程受原请求超时及取消控制，socket 在全部路径关闭。对看似有效的本地时间，若 TLS 握手报告证书日期错误，则查询时间服务器并仅重试一次握手。

按 [RFC 4330](https://www.rfc-editor.org/rfc/rfc4330.txt) 验证服务端模式、协议版本、同步状态、stratum、请求/响应 originate 标识、源地址和端口、包长及时间范围。请求标识来自强随机数。处理 NTP 在 2036 年的秒数回绕，支持到 2099 年。要求两个不同服务器地址的结果在扣除单调时钟经过时间后相差不超过五秒，才采用网络 UTC。服务器名称参照 [阿里云](https://help.aliyun.com/en/eci/user-guide/configure-the-ntp-service)、[微软](https://learn.microsoft.com/en-us/windows-server/networking/windows-time-service/Windows-Time-Service-Tools-and-Settings)、[Cloudflare](https://developers.cloudflare.com/time-services/ntp/usage/) 的文档。

网络 UTC 只在 context 内缓存，按 ScSystemMonotonic 推进，一小时后重新同步。单调时钟倒退或频率改变时重新查询；不修改全局时钟。无法取得可用时间则报告错误，证书链、主机名与有效期检查照常执行。

SNTP 没有密码学身份认证；双来源核对不能防住同时控制网络路径的攻击者，也不等同于 NTS。此实现不宣称认证授时。需要可用 DNS 和出站 UDP/123；当前仅支持 IPv4。
