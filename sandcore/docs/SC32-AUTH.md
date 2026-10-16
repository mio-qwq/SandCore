# SC32AUTH v1

2026-10-16。公开SC32AUTH.H由SC32API.SCL ABI1.4/v6提供四个cdecl入口，私有适配只调用旧AUTHLOGIN201、STATUS202、EXEC203、CANCEL206。旧ABI、PBKDF、真实owner/generation、数据库代数失效、60000 tick期限及2秒限速都保留。Error.operation为1800..1803组。

Begin只借本次调用的用户名/密码NUL字节串，1..31B用户名、0..128B密码，不允许嵌入NUL；不存明文或复制到普通String。GUI输入UTF-8，内核既有账户名字规则不变。一个owner沿原内核只有一个认证请求；新API在已有票据时BUSY，调用者显式Cancel，不把原有任务旁表改成固定任务上限。

公开Ticket是16B v1记录：单调不回绕的库handle及实际owner generation，私有raw随机token不外露。Query输出PENDING/READY/DENIED真实状态；只有PENDING且内核给真实总数才输出PBKDF计数，完成不伪造百分比。DENIED是成功查询到认证拒绝，cause=ACCESS；陈旧、过期、数据库变化等是查询错误，仍需Cancel释放库状态。

Activate只创建并激活新的普通会话，不改变调用者UID/SID，不启动调用者自报的命令，不创建SYSTEM身份。成功消费票据并取消旧内核证明；内存/桌面构造失败保留READY证明，允许期限内重试或Cancel。Cancel/库fini撤本人票据。最高SYSTEM仅真实外部硬件授权链，普通root管理策略不变。

固定LOGIN2与普通SESSION2共用这些入口，固定服务额外受内核发行role/只读资源/系统调用名单约束，详见SESSION2.md。没有新增认证selector或修改内核凭据API寄存器。

修订：2026-10-16，新增公开typed认证，实际宿主/guest边界见STATUS.md。
