# M9 内核身份与票据（源码阶段）

> 当前验证范围已更新，见末尾“2026-10-05 M9最终验证记录”及[M9验收报告](M9-ACCEPTANCE.md)。早期未验证/待验标记保留阶段背景。

> 当前仅实现/静态审阅，未构建或运行；完整安全验收另见 M9-IMPLEMENTATION.md。

## 1. 固定身份

UID/GID 使用32位有符号整数，0为root，-1为SYSTEM，普通账户非负。
task_t仍168B，凭据放supervisor旁表，绑定task_generation；任务创建时
复制身份，此后登录/桌面切换不重新标记已有进程。普通执行按调用者身份，
程序文件所有者不赋予权限。默认GUI身份mio/1000，初始root没有可登录密码，
首次设置密码从外部串口管理进行；没有公开默认root或SYSTEM口令。
有效账户库的GUI启动身份取真实mio UID对应的GID；不存在账户库才走首次安装
默认值。已存在但格式/读取损坏、缺少默认用户或默认用户禁用时，不恢复默认
组权限：新桌面任务取内部UID/GID=-2无权限哨兵，普通文件入口拒绝访问。
该值不能由账户API创建；外部SYSTEM修复/重新保存账户库后重启恢复正常启动。

身份realm分别为NORMAL=0、SERVICE=1、SERIAL=2、KERNEL=3。
SYSTEM服务可以由内核创建，SERVICE不取得MOD管理/任意零环执行权限。
服务的文件访问subject可以委托为请求者UID/GID，避免图片解码器读取其客户端
不能读取的文件。串口realm只由硬件UART管理入口建立，并绑定本次外部会话。
断开先撤会话授权再停止管理链进程；已常驻模块按合同仍留到重启。

TSS.iomap_base置于TSS限长之外，IOPL保持0，三环IN/OUT产生GP。
不得把TSS的BSS零值误当I/O位图，或给root提供raw-I/O系统调用。

## 2. 账户库与密码

/SYS/AUTH/USERS.DAT为内核保护目录中的账户库，普通文件接口包括root均不能改，
root经受控用户管理API操作；SYSTEM普通登录始终拒绝。文件头32B：
SCUSR1两NUL、version=1、count、database_generation、iterations=100000、MIO及保留。
最多32个160B账户记录：name32/home64/uid4/gid4/salt16/hash32/flags4/reserved4。
flags bit0启用、bit1已有密码。删除保留UID和名称为禁用记录，防止新账户接管旧文件。

密码为PBKDF2-HMAC-SHA256，16B随机salt、32B派生值，100000次迭代。
内核自写整数SHA/HMAC，无libc/浮点。每客体tick总预算256次HMAC，待办轮转，
不会在一个系统调用中跑完全部工作；耗时、输入响应与负载第二阶段实际测量。
派生值比较不按首个不同字节提前结束，完成/取消/任务退出清除HMAC中间状态。
不在Shell命令日志、串口协议诊断或snapshot中打印密码/派生值/系统令牌。

## 3. 强熵与SYSTEM令牌

每次启动由内核生成32B随机SYSTEM令牌，留在内核，不经SCAPI输出。
强熵来自有能力检测/成功位/有界重试的RDRAND，或QEMU外部调试机在启动前
使用宿主OS强随机生成的32Bfw_cfg种子opt/sandcore/entropy。
fw_cfg是模拟硬件熵源，不是身份授权入口；客体三环不能访问其I/O端口。
无强熵时失败关闭票据及串口SYSTEM创建，不用PIT、MAC、磁盘号或地址替代。
宿主种子不写进SandFS，不输出到报告；内核读取后清除临时混合缓冲。

普通登录票据为独立随机31位句柄，绑定调用者PID/代数、目标账户、账户库代数、
创建时间与操作。最长60000tick，数据库修改会使旧待办/票据失效。普通登录
不会创建SYSTEM票据；拿一个PID、realm数字或打印的用户名不能伪造凭据。

## 4. 新增SCAPI

| 号 | 入参 | 返回/输出 |
|---|---|---|
| 0x200 AUTHINFO | EBX=32B输出 | version,uid,gid,realm,mod_authorized,task_generation,0,0 |
| 0x201 AUTHLOGIN | EBX=name，ECX=password | 正票据；负错误；当前进程身份不变 |
| 0x202 AUTHSTATUS | EBX=ticket，ECX=32B输出 | 1等待、0成功、负失败；version,state,iterations_done,total,target_uid,action,0,0 |
| 0x203 AUTHEXEC | EBX=ticket，ECX=command，EDX=0创建/1激活桌面身份 | 新PID或0；激活不改已有任务 |
| 0x204 AUTHUSERS | EBX=输出，ECX=容量1..4096 | root/串口SYSTEM列举用户，正文长度 |
| 0x205 AUTHACCOUNT | EBX=0创建/1禁用，ECX=name，EDX=home，ESI=uid，EDI=gid | 0或负错误；不能创建UID -1 |
| 0x206 AUTHCANCEL | EBX=ticket | 取消/清除本调用者票据 |
| 0x207 AUTHPASSWORD | EBX=name，ECX=password，EDX=旧认证proof | 正异步票据；root/串口SYSTEM或已认证本人 |
| 0x208 FSMETA | EBX=path，ECX=32B输出 | version,kind,size,uid,gid,rw,flags,generation |
| 0x209 FSPERMISSIONS | EBX=path，ECX=uid，EDX=gid，ESI=rw | uid/gid=-2保留；0或负错误 |

所有字符串/输出先检查调用者映射。0x203不改已有进程身份、不使用SUID，
SCAPI保留旧USERQUERY/STAT/CPU/MONITOR的缓冲边界。任务槽扩为32，旧
48字MONITOR/64字CPU继续只列历史8槽；全量进程信息另加版本接口。

## 5. 修订记录

| 日期 | 变更 |
|---|---|
| 2026-10-04 | 写入凭据、realm、异步密码派生、强熵/令牌及0x200..209合同；未验证 |
| 2026-10-05 | 坏账户库关闭默认文件访问，启动GID取真实账户；保留无库首次安装与外部SYSTEM恢复，未运行验证 |

参考：[fw_cfg硬件布局](https://www.qemu.org/docs/master/specs/fw_cfg.html)、
[PBKDF2定义](https://www.rfc-editor.org/rfc/rfc8018.html)。

2026-10-05运行修订：07已实际验证root及普通UID 1001的进程身份绑定、
SYSTEM普通登录拒绝、MOD/零环/其它身份窗口拒绝、UART真实GP、环境UID
不能改凭据，父SYSTEM身份在两次子身份创建后保持。相同创建者登录尝试
至少相隔200tick，AUTHLOGIN返回-6要求等待；06夹具忽略间隔报失败，
修夹具后07通过，内核策略未放宽。完整认证/重启/坏库及会话撤销仍待验。

## 2026-10-05 M9最终验证记录

账户/改密/UID/GID/模块边界118项、真实会话/BYE/3000tick租约50项及最终内核身份回归通过。密码新旧检查在普通UID下实际执行；root/SYSTEM能管理普通账户，SYSTEM普通登录始终拒绝。串口资格在内核，客体root不取得MOD或UART。

修订：2026-10-05，记录实际范围与证据，待用户验收；前述早期未验证叙述保留为历史。
