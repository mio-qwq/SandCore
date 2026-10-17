# SC32AUTH v1

2026-10-16。公开SC32AUTH.H由SC32API.SCL ABI1.4/v6提供四个cdecl入口，私有适配只调用旧AUTHLOGIN201、STATUS202、EXEC203、CANCEL206。旧ABI、PBKDF、真实owner/generation、数据库代数失效、60000 tick期限及2秒限速都保留。Error.operation为1800..1803组。

Begin只借本次调用的用户名/密码NUL字节串，1..31B用户名、0..128B密码，不允许嵌入NUL；不存明文或复制到普通String。GUI输入UTF-8，内核既有账户名字规则不变。一个owner沿原内核只有一个认证请求；新API在已有票据时BUSY，调用者显式Cancel，不把原有任务旁表改成固定任务上限。

公开Ticket是16B v1记录：单调不回绕的库handle及实际owner generation，私有raw随机token不外露。Query输出PENDING/READY/DENIED真实状态；只有PENDING且内核给真实总数才输出PBKDF计数，完成不伪造百分比。DENIED是成功查询到认证拒绝，cause=ACCESS；陈旧、过期、数据库变化等是查询错误，仍需Cancel释放库状态。

Activate只创建并激活新的普通会话，不改变调用者UID/SID，不启动调用者自报的命令，不创建SYSTEM身份。成功消费票据并取消旧内核证明；内存/桌面构造失败保留READY证明，允许期限内重试或Cancel。Cancel/库fini撤本人票据。最高SYSTEM仅真实外部硬件授权链，普通root管理策略不变。

固定LOGIN2与普通SESSION2共用这些入口，固定服务额外受内核发行role/只读资源/系统调用名单约束，详见SESSION2.md。没有新增认证selector或修改内核凭据API寄存器。

修订：2026-10-16，新增公开typed认证，实际宿主/guest边界见STATUS.md。

2026-10-17，本机默认账户回路：仅内核发行核验的固定LOGIN2角色，可以用空输入登录启用且从未设置密码的默认mio/UID1000。已设密码仍执行原PBKDF；root、其他账户、普通Session2/Shell/su及网络调用不使用此例外。账户库损坏或默认账户禁用仍失败关闭；票据仍绑定owner/generation、数据库代数、期限和限速。此项无新selector，无账户库格式或SCAPI字节变化；HOST21/native11/final12实际构建完成；GUI13真实root空输入拒绝、mio空输入登录SID5、注销后再登录SID6成立。原资源账+8/FAIL保持，详见M10A2-ACCEPTANCE.md及evidence/m10a2-session-return。

修订：2026-10-17，补固定本机LOGIN2的默认mio空输入策略和边界。


修订：2026-10-17，统一HOST21/native11/final12、GUI12与登录回归实际范围，SDK及发布入口同批整理。
