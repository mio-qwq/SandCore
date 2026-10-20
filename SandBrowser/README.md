# SandBrowser

作者：**mio**。面向 SandCore 的原生 i386 Ring 3 浏览器初版，界面致敬 **IE5 / Windows 98**。所有新增源码、依赖、缓存和产物均在本目录，既有系统源码和公共库没有修改。

## 安装和启动

发行包：`dist/SandBrowser-0.1.4.zip`。ZIP 中只有两个文件：

- `SETUP.SCX`：原生解包安装向导。
- `BROWSER.PAK`：全部程序、私有库、资源、帮助和许可的总载荷。

在 SandCore 中把两个文件解压到同一目录，运行 **SETUP.SCX**。默认安装到 `$HOME/SBROWSE`；安装完成可直接 Launch，之后运行安装目录中的 **BROWSER.SCX**。不要单独搬走某个运行时 SCX 或 SCL。

需要与本工程读取的 SDK 对应的 SandCore / SC32API / SCCLIB，包含 typed GUI、IPC、共享内存、字体和图片服务。HTTPS 使用 **RDRAND / RDSEED** 提供强随机数；本地 UTC 缺失或证书日期校验失败时，自动向时间服务器查询，核对两份结果后用于证书校验。

M10a2 原启动器的 `-cpu qemu32` 默认隐藏 RDRAND，会出现 `network -4, TLS 0`。关闭原 QEMU 后，在 Windows 双击本目录的 **RUN-HTTPS.BAT** 再启动系统。它复用旁边 `SandCore-M10a2/run.py`，只为此次启动增加 `+rdrand`。系统在其他位置时执行 `RUN-HTTPS.BAT --system "系统目录"`；`--check` 只检查启动条件。浏览器安装包仍是两个文件，宿主启动工具不放进客体安装载荷。

## 实现内容

| 部分 | 当前代码 |
|---|---|
| 经典外壳 | 六组菜单、原创工具图标、地址栏、链接栏、收藏/历史侧栏、状态与进度、键盘操作、新窗口 |
| 导航与资料 | 后退/前进、停止/刷新/主页、地址打开、网页查找、源码查看和保存、收藏/历史/选项持久化 |
| 页面 | litehtml 0.9 + 随附 Gumbo，真实 HTML/CSS 布局；块/内联、列表、浮动、定位、嵌套表格、媒体查询、flex；50%–200% 缩放 |
| 图片与表单 | 系统 PNG/JPEG/WebP/BMP；stb_image 的 GIF 首帧；基础表单控件、GET/POST URL 编码提交 |
| 脚本 | Duktape 2.7.0；基础 DOM、属性/正文/样式修改、事件、计时器、表单值、导航；8 MiB heap 与执行预算 |
| 网络 | SBNET.SCL 的独立 C API；IPv4 DNS/TCP、HTTP/1.1、重定向、分块响应、gzip/deflate、会话 Cookie |
| HTTPS | BearSSL 0.6 / TLS 1.2，验证证书链、主机名和有效期；100 个固定的活动 CA 信任锚；主页面正常跳转，页面子资源按来源处理 |
| 安装 | 经典灰色向导、原创地球/网络动画；主题文字为 welcome to the World Wide Web !；CRC/解压校验、暂存提交、取消清理、更新保留 profile 和 .OLD 备份 |
| 关于 | Help → About 以及 about:about 页面，作者 **mio**、版本、真实编译时间和离线第三方许可 |

这张表说明代码已接入的范围，**不代表客体功能实测通过**。构建检查编译、链接、标准容器和针对性的宿主回归；没有启动客体系统或按指定网站做适配。由用户在 SandCore 中实际运行。

### 0.1.4 安装回归修复

仅修复 0.1.3 新增备份路径检查的错误码判断。现有 ScPathStat 经旧 fs_stat 接口，在路径不存在时返回 SC32_E_FAILED（-116），并非 SC32_E_NOT_FOUND（-104）。安装器兼容这一返回值选择备份候选名，最终 ScPathRename 仍拒绝覆盖已存在的目标。权限错误继续报告。针对性检查直接编译 SDK 的 32 位路径适配代码，另检查实际安装器的缺失路径、旧备份和权限分支；网络逻辑沿用 0.1.3。

### 0.1.3 网络通路修复

修正主页面跳转策略：正常跟随 HTTP/HTTPS 重定向，地址栏与锁标记反映最终连接；仅 HTTPS 文档的子资源要求重定向全程使用 HTTPS。此前把所有 HTTPS → HTTP 跳转都拦下，是错误地把子资源策略套在了主页面上。

修正信任锚生成：固定快照中的 100 个活动 CA 按标准 X.509 CA 模型使用。此前把包含 Chrome 专属版本/SCT 约束的 47 个 CA 整体删除，连目前仍可签发有效证书的 DigiCert Global Root G2 也被删掉，导致合法站点报 TLS 62。这些 Chrome 专属策略尚未实现，本浏览器不宣称拥有 Chrome 的完整信任策略。

网络层兼容 zlib/raw deflate、连续 gzip 成员、gzip 头部 CRC、相同值的合并 Content-Length、带空白的 chunk 扩展及 HTTP 关闭定界响应；解压按需扩容，不再为小响应固定分配 16 MiB。显式长度和分块正文仍校验完整性。支持 20 次重定向，URL 空格与 UTF-8 路径编码后发送，错误页按实际原因提示。连续升级自动选择 .OLD、.O01… 的空闲备份名。

`tools/test_network_paths.py` 使用实际 SBNET 代码和独立的宿主 socket 适配检查 HTTP/TLS 通路，包含 48 项确定性响应与故障检查。TLS 测试 CA 只进入单独的宿主测试程序。另以同一生产信任根和 TLS 1.2 配置实连检查 mio.blue 与 g.cn 的证书/跳转；没有对这些域名写例外。宿主适配不会进入安装包。

### 0.1.2 HTTPS、显示与快捷方式

硬件熵先检测 CPUID，优先 RDSEED、再用 RDRAND，短暂繁忙时重试；失败不给 TLS 注入弱种子。时间回退通过 UDP/123 查询阿里云、微软、Cloudflare 的 NTP 服务，匹配随机请求标识并要求两个不同地址的时间基本一致。结果只存于网络进程，按单调时钟推进，最长一小时后重新同步，不改系统时钟。详细行为与限制见 docs/NETWORK.md。

安装器随包写入原创 32×32 的 BROWSER.SCB 图标，并修复旧安装器创建的同路径空图标快捷方式。常规界面字号从 14 调到 15；页面缩放独立调整。View 菜单和状态栏提供缩小、放大、恢复 100%，也可用 Ctrl++、Ctrl+-、Ctrl+0、Ctrl+滚轮，每次 10%，保存退出后的缩放偏好。页面按新视口重新排版，字体按目标字号重新栅格化，链接与表单按缩放转换点击坐标。

### 0.1.1 安装器修复

0.1.0 的安装器与浏览器遗漏 SC32 要求的提交矩形，窗口创建后无法显示正文。0.1.1 两处共用完整的 Invalidate → PaintBegin → Submit（带矩形）→ PaintEnd 流程；失败撤回 claim、保留重绘并显示错误标题。安装器先绘制首帧再读载荷，支持 Tab/Shift+Tab、Enter/Space、路径光标和 Ctrl+A、左键按下/松开与移出取消、失败重试。安装文件写入后核对实际长度，回滚清理和恢复失败会单独报告。更新时两个文件均使用同一版本。

## 多进程结构

```text
BROWSER.SCX → SBUI.SCL        界面、导航、profile、IPC 协调
                  ↕ IPC
WEBPAGE.SCX → SBPAGE.SCL      HTML/CSS、DOM/JS、绘制
                  ↕ IPC（经 UI 转发资源请求）
WEBNET.SCX  → SBNET.SCL       DNS、socket、HTTP、TLS、Cookie

页面像素：按目标 PID + generation 授权的共享内存，FRAME / ACK 交接。
```

SCX 只是小入口；较大的引擎位于私有 SCL。因此没有修改 SCX 装载限制，也没有给 SCX 偷接尾部载荷。IPC 消息带请求号和导航 epoch，连接双方核对内核提供的 PID、generation、会话和身份。socket 属于网络进程；窗口属于 UI 进程。

## 明确的缺口

这是第一版，尚不能宣称完整对齐 IE5。frames/iframe、SVG、GIF 动画播放、CSS grid、下载字体、页面正文选区/右键菜单、地址自动完成、打印、完整网页离线保存和下载管理没有实现。表单编辑和 select 交互仍较基础。UTF-8 是主要文档编码；GBK、Big5 等旧网页编码尚未适配。页面文字使用一个系统字体家族。

JS 的语言内核和浏览器 DOM 能力分开：没有完整现代 DOM、fetch/XHR、canvas、媒体、模块和现代框架兼容承诺；事件监听采用每元素/事件一个槽。ActiveX、VBScript、Java 插件和 Windows COM 不在实现中。

网络尚无 TLS 1.3、HTTP/2、IPv6、代理、在线撤销检查和磁盘缓存。Cookie 仅会话内、精确 host/path 匹配，拒绝父域共享 Cookie；Expiry/SameSite 不是完整现代实现。CA 信任来自固定的活动证书列表，Chrome 版本、SCT 与根计划的完整附加策略尚未实现。

边界：每响应 16 MiB，HTML 4 MiB，资源 96 项，JS heap 8 MiB，表单正文 32 KiB，最大视口 1920×1080。worker 退出会提示，重新打开浏览器重启。收藏上限 64、历史 128 项。

## 构建

在 Windows PowerShell 执行 `./build.ps1`。宿主需要 WSL、GCC/G++ 15、binutils、patch、Python 3。入口先按 vendor/SOURCES.json 下载并校验固定依赖，再展开源码和 32 位工具链运行库。Git 只收录依赖清单和 vendor/patches/ 中的适配补丁；下载缓存、展开目录及 build/ 不提交。已有依赖可重复使用，补丁按前后摘要核对，不覆盖其他本地改动。所有临时目录和输出都留在本目录。不要求在 SandCore 内编译。

构建顺序：prepare.py → make_anchors.py → build_native.py all → package.py。导入表逐项匹配 SDK 导出，缺实现即停止；不链接 Linux libc，不伪造 Linux 系统调用。GCC STL 模板在应用中实例化，只链接需要的 tree/list/hash 与 compiler builtins。所有工具位于 tools/。

## 文档

- [IE5 参照与当前差距](docs/IE5-ALIGNMENT.md)
- [平台、IPC 和库边界](docs/PLATFORM.md)
- [安装格式与恢复行为](docs/INSTALLER.md)
- [网络库 API](docs/NETWORK.md)

第三方 MIT/BSD/Apache 许可保留原作者声明。GCC 运行库与头文件适用 GPLv3 + Runtime Library Exception 3.1，构建采用其允许的编译流程，不要求将 mio 的独立应用代码改为 GPL。完整文本随包安装在 NOTICE.TXT，并在帮助菜单离线提供。

Copyright (c) 2026 mio.
