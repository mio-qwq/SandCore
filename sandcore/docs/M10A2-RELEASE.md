# SandCore M10a2 — The Application Platform Era

本版建立可复用的Ring3应用平台：真正SCL动态链接、SCCLIB/SCLIB/SC32API/SCUI、统一桌面和主要应用，以及可在系统内编译复杂多源应用的SDK。发布前检查与实际证据见[M10A2-ACCEPTANCE.md](M10A2-ACCEPTANCE.md)。GitHub发布在检查后执行；实际签名提交、发布结果及附件摘要另由delivery/publish凭证记录。

## 交付内容

| 附件 | 内容 |
|---|---|
| SandCore-M10a2.zip | 根目录严格4文件：run.py、run.bat、sandcore.img、sanddata.img；干净基盘迁移，含新平台/应用和内置SDK |
| SandCore-M10a2-SDK.zip | 独立公开SDK：INC、9库、实际原生编译/链接工具、SCW/多源例子、规范、固定源码与完整许可 |
| SHA256SUMS | 两个附件的SHA256摘要；Release上传后按GitHub资产摘要核对 |

不捆绑QEMU/GCC/Python本体。使用已安装Windows QEMU（默认C:/Program Files/qemu）及Python，双击run.bat；启动参数见run.py --help。run.bat为纯ASCII。默认SDL原生相对鼠标、物理DPI/最近邻，WHPX不可用时回退TCG；当前最大1920×1080，16:10宿主全屏黑边是保持比例的结果。

## 主要变化

- 动态SCL、版本/依赖/符号/生命周期与真实SCCC→SCO→SCLD开发闭环；新应用通过大写公共头和库开发，旧SCAPI.H原字节/ABI保留。
- Ring3桌面、图标持久化/多选、开始菜单/任务栏、关联、跨应用剪贴板和有释放凭证的拖放；文件作业支持持久REC、权限与条件提交。
- Files、Studio、Debug、Notes、Canvas、Lens、Settings、Monitor、Calculator、Terminal、Palette、Sound、Session和Installer统一框架。Studio的工程后缀为SCW。Installer是既有文件安装流程的GUI，不宣称完整软件包依赖管理器。
- 实际多源ResearchDesk开发/运行/动态插件/调试证据；缩放/Classic/Aurora、文件/图形/音频/会话定向证据复用。
- 修复高DPI入口、全屏鼠标、快拖取消、终端滚动与安装结果列；退出桌面返回登录并保留会话，注销关闭当前会话，固定本机LOGIN2允许未设密码的默认mio空输入登录。root、普通su及网络没有此空口令例外。

## 准确范围

8个普通系统库与SCCC/SCLD来自实际客体编译；SCSOUND是明确记录的宿主codec例外。SCCLIB只承诺文档中的C32范围；SCL初版不宣称跨进程物理代码共享或NX。当前无HTTPS/TLS、GPU、SMP、IPv6或全系统完整双语承诺。

**登录回归功能通过，但该轮自动资源账没有通过。** 首次保留LOCKED登录桌面后+8页，与编译出的32660B状态及生命周期匹配；未直接抓取地址归属。GUI13原FAIL_OR_INTERRUPTED、未归因8及驱动退出1保持。native11及GUI12分别有独立零差证据，不冒充GUI13零差。详细记录和截图在evidence/m10a2-session-return。其他历史失败同样保留。

C:/Users/Administrator/Desktop/test中的现有用户盘另行做系统载荷迁移，1626个未列入更新的对象（含34个HOME/桌面对象）正文、权限、属主、代数保留；原盘备份在本机build。Release使用独立干净基盘，不收集用户test数据，不覆盖它。M8封存、M9/M10a1正式产物保持；完整M10及基础内核冻结并未在本版宣称完成。

修订：2026-10-17，形成检查后发布的交付说明与已知限制。
