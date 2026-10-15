# SC32 平台定向应用

2026-10-15：`tests/m10a2/PLATFORMPROBE.C`、四字段动态清单及本文已收齐，**仅源码与静态审阅，尚未构建、尚未运行**。打印 PASS 的字符串是实际条件全部成立之后的应用分支，不是已经得到的客体证据。Root 负责本批统一构建、真正普通登录、Windows 双盘运行与全任务退出账。

本应用覆盖新 Clipboard/IPC/Shared/Font/Palette 的实质接线风险；不把它称完整 SCUI、拖放、桌面或 M10a2 验收。不写 SCAPI、软中断、寄存器桥、内核地址或库正文。

## 1. 应用身份、清单与部署

程序入口为 `int main(void)`。清单只有 `kind/name/dependencies/imports`，名字为 `PLATFORMPROBE.SCX`；唯一直接依赖为 SC32API.SCL ABI1.2、library_version≥3。52项 import 全为当前正式 export 的 function/size1。自动 CRT 与根运行时由原动态工具链提供，应用不手写启动桥或显式装载根库。

公共头为 SC32API.H、SC32BROKER.H、SC32FONT.H、SC32PALETTE.H。小范围测试用的字节比较、十进制和固定32B帧是本应用私有代码，不是带入另一套标准库实现。大正文临时区明确使用正式页 API；程序没有把每个小对象或普通 malloc 变成一次页分配。

源码和 JSON 安装到主控自己的 HOME/SDK 测试目录，产物安装到 HOME 的测试目录。完整父进程调用形式为：

```
/HOME/M10P/PLATFORMPROBE.SCX all /HOME/M10P/PLATFORMPROBE.SCX
```

显式第二个路径必须是可执行同一 SCX 的绝对名字，不含空白，输入长度≤63B。父通过 ScPathResolve 得到规范绝对路径，一份用于真实 Spawn、一份作为 typed FILES 的路径意图；不依赖隐含 cwd 或对方的地址。无参数只选择上述默认测试路径，主控使用其它位置时应显式传 `all <absolute-path>`。

完整流程**必须由真实 `su mio` 登录 Shell 执行**，要求 API 返回 UID1000、realmNORMAL、非零真实 SID、无硬件 SYSTEM 资格。此项由主控走已存在的合法登录链完成；源码既不读取密码，也不自报 UID、创建假 realm 或连接管理串口。父从真正 Job 得到孩子 PID/gen，再以 Accept/Peer/Packet 的可信身份核对，不能拿 Spawn 正文里的数字充当授权。

`font` 参数只运行 Font/Palette 流程，允许主控在真正 SYSTEM caller 中单列核对值接口，打印自身真实身份。它的完成标记为 `PLATFORM_FONT_MODE_PASS`，不会打印完整的 `PLATFORM_FOUNDATION_PASS`。默认 SYSTEM 执行 `all` 会明确失败于普通登录条件，不能冒充普通用户 PASS。

## 2. 一父一子、两个队列

父在本人 SESSION namespace 创建 `P.<pid>.<generation>` 与其 `.data` 两个监听端点。control 的 queue budget4096B、frame max1024B；data 的 budget1024B、frame max128B；pending budget都为4096B。SYSTEM namespace 的同一候选由普通进程先调用，必须 ACCESS 且 out 保持哨兵，才继续 SESSION 注册。名称实际关联当前 SID，不用永久全局名，不留下固定数量的 fd 或 endpoint 门槛。

父通过 ScProcessSpawn 启动同一 SCX：

```
child <endpoint-name> <parent-pid> <parent-generation>
```

这些都是值；孩子仍必须通过正式命名 Connect 获得本人 channel。父接受两个真实连接，核对返回的 PID/gen、UID1000、realmNORMAL、SID和无 HARDWARE_SYSTEM。孩子反向 Peer 核对真实父，普通命名端点继续沿当前默认完整 ACL，应用不降低身份条件。

data 队列仅在孩子收到 control 的 DRAIN 命令后消费。因此父填满队列时不会被孩子无意提前排空。每个小队列充填有64次安全结束界限，实际能容纳几帧以内核的完整元数据 charge 为准，不在应用里伪造 message 结构或硬编码“必须四帧”。

## 3. Clipboard 工作流

父先创建本会话测试候选：严格 UTF8 `seed 中`，以及一个 COPY 类型 FILES 列表。FILES 是16B头、20B行、同一真实应用的规范绝对路径和实际 ScPathMetadata revision；object_id/flags均0，明确不是持久 birthID 或文件授权。父提交后 Open 持有旧 snapshot、分页读取两个 format，并登记 Watch。

孩子准备 **131079B** TEXT。普通字节为 `a + offset % 26`，唯一末尾 NUL 位于131078；`F0 9F 99 82` 从65535开始，真正跨过64KiB私有传输边界。通过一次公共 Offer 完整提交内部多段候选，随后同一 TEXT 的 `C0 80 00` 非法 UTF8 Offer 必须 INVALID并保留先前有效正文，再 Commit发布一次。临时原始页在 Commit后主动 Free，正文须已属于内核 immutable对象。

父以真实 Watch/Ready/Wait 等到变化，Open核对 epoch、format数与真实孩子 publisher。旧 snapshot 中 `seed 中` 和 typed FILES 路径仍能读；另一个候选以旧epoch Commit必须STALE，整个宽 out 保持、候选可Abort、current仍是孩子版本。没有用一次失败 CAS 覆盖当前内容。

孩子退出后，父再以三个有界 Read（至多65536B/次）读取全部131079B，逐字节核对含跨段四字节 scalar和最终 END；不是只验证头或猜哈希。结束关闭两个 snapshot，以已知 current epoch CAS Clear，再关闭 Watch。测试只运行在专门登录的测试会话，会替换该会话剪贴板；Clear不会清其它 SID，也不会无条件清后来被别人改过的新epoch。

## 4. IPC 原子性、sender、背压与等待

孩子的 HELLO正文故意给出父 PID作 claimed PID。Packet.sender仍必须是 Job/Accept证明的真实孩子，父据 Packet 与Peer核实，不从正文得到权限。

父先Peek32B HELLO，再用1B输出Receive：必须RANGE、body哨兵和整个Packet输出保持，第二次Peek仍同一双字sequence，随后完整Receive才能消费。data队列的每帧按 `FRAME_FILL` 和递增索引核对真正FIFO正文与 sender。

父在孩子等待control时将data填到AGAIN，Ready的write_bytes必须不足以承诺下一完整帧。通过独立control发送DRAIN，孩子读取已提交的确切帧数并ACK，空间恢复后父发送真正零字节 DATA。孩子以 Packet.flags/type/bytes判它为 DATA，再ACK；不把bytes0当EOF。

空data方向有一次3tick实际 `ScBrokerWait`。旧 STREAM 提示只触发重新Ready，必须最终看到 TIMEOUT状态和 TIMEOUT事件，且真实单调tick达到预算。没有以用户态两次API拼出“检查后无限睡”的伪原子性，也不把每次唤醒当队列预约。普通准备/读写等待500ticks上限，另有明确尝试数防止新接线错误形成无限循环。

Root静态收尾发现一个具体WAIT风险：立即ready成功时若不消费本次已返回INPUT/STREAM事实位，旧events会常驻，使后续空等待忙转。当前 `task_broker_wait()` 已在同一IF=0成功路径清 `events&255`；这只说明源码修复已接线，尚无运行证据。本应用先消化可能尚存的通知、重新检查真实队列，然后要求实际TIMEOUT和至少3tick，直接覆盖这个问题，不扩成排列矩阵。

孩子发送 DONE 后从main正常返回，故意保留其IPC句柄。父真实 JobWait领取退出码0后等待CLOSED，再收到明确 EOF；EOF的真实peer身份仍需匹配，Send必须BROKEN_PIPE。父的channel/listener显式Close均检查成功。EOF/动态owner清理出错会失败，不由应用主动提前close掩盖。

## 5. 18页真实共享映射与权限

孩子 CreateShared73728B（18页）并RW Map，先检查所有实际映射字均为0，再在首字和最后字写 MARK_INITIAL。18页使Create/Map/Unmap跨过现16页短步预算，不能仅靠单页成功代表续进。

grant目标是Job实际父PID/gen。错误gen须STALE且整个out不改；给父RO/RW两种grant之后，孩子自身Open面向父的grant须ACCESS，说明grant数值不授予非目标能力。父直接Query孩子的foreign reference也须STALE，再通过正式OpenShared取得两个本人reference。

父不得把RO ref以RW映射。成功RO/RW两个独立VA须报告同object_id和READY，实际 ScSystemMemory 的本人 present/US PTE计数增36。RW别名设置一个合法96B FontInfo头，向对应RO VA请求 FontQuery输出必须INVALID，哨兵保持；这一拒绝经过实际PTE写范围检查，不依赖应用自报readonly。ScMemoryFreePages不能释放Shared VA。

已打开后孩子显式 Revoke RO grant，后续Open须STALE，既有RO mapping仍读到正文。父在首、末字写 MARK_PARENT，通过IPC命令让孩子直接读取其自己VA；孩子写第二字、倒数第二字，父RO别名马上观察 MARK_CHILD。没有向对方传用户VA、通过消息复制整个对象或把Heap字符串称共享。实际同PF来自已审阅的broker/paging接线；公共应用没有获取PF号的越权接口，不以自身值比较冒充内核页表审计。

孩子退出后，未撤销的RW grant也不能再Open，但父已有ref/map仍活。父先Close两个reference，再以仍活RW别名写一个字、通过RO别名读到它，证明Close不偷偷Unmap。最后依次Unmap18页；旧VA作为安全typed输出请求必须INVALID，本人映射正文页数与页表页数均回到Map前的实际值。

`PLATFORM_SHARED_PTE`日志记录实际读取的before/mapped/after、tables_before/tables_after及填满的真实帧数。没有把共享alias的36个虚拟PTE称36个新PF，也没有把它当全机释放统计。此程序未故意CPU写RO制造异常；真实CPU异常与全owner/最后PF回收由主控结合当前内核接线与已有退出证据定向核实。

## 6. Font与Palette前缀

Font12/16分别Query，必须是真正激活的凤凰TTF和非零generation；宽out的未知尾部保持。错误expected_generation必须STALE且整个Glyph out保持，当前generation的合法缺字U+10FFFF须OK/mapped0、真实.notdef、规范1600B头、96×4行布局及未用行全0。程序不把缺字当错误、不声称自行栅格化、也不从位图放大替代字号请求。

Palette先以capacity255请求，必须RANGE且260项全保持；capacity260成功只改真实前256项，值为24位RGB，后四项保持。哨兵只是测试数据，不是新增UI配色槽。字体缓存/页面首次增长属于真实服务成本，不能要求第一轮和完全冷基线PF差0后伪称泄漏，也不能据这一值接口称SCUI视觉验收。

## 7. marker、失败与证据边界

完整成功应出现以下具体marker，主控需同时记录真正普通登录的父/child身份、退出码和实际产物摘要：

```
PLATFORM_ID pid=... gen=... uid_bits=1000 realm=0 sid=... flags=...
PLATFORM_CHILD_PASS
PLATFORM_SHARED_PTE before=... mapped=... after=... tables_before=... tables_after=... filled=...
PLATFORM_CLIPBOARD_PASS
PLATFORM_IPC_SHARED_PASS
PLATFORM_FONT_PALETTE_PASS
PLATFORM_FOUNDATION_PASS checks=...
```

child的IDENTITY行也来自真正API，不是填入HELLO正文。最终checks随真实队列可装帧数而变，不固定一个数字当验收门槛。`font`模式仅有实际身份、FONT_PALETTE和FONT_MODE三个阶段输出，不能计作Clipboard/IPC/Shared成功。

失败输出 `PLATFORM FAIL <step> code_bits=... domain=... raw_bits=... operation=...`，捕获第一次失败时的真正ScErrorQuery，随后清理不会把错误原因覆盖。code/raw以32位无符号位模式打印以避免INT_MIN负号转换问题，逻辑条件失败时error可能是此前成功0，不能伪造驱动错误。父失败先取消并有限等待孩子，再撤本人map/ref/channel/candidate/snapshot/临时页；任何仍活对象最后由main失败退出的真实owner链收束。

本次静态确认公共声明/实际export与52个调用一一对应、动态清单四键和ABI版本匹配、无SCAPI/asm/库正文、C定界符和差异空白。没有运行宿主预处理、编译器、生成的i386或QEMU。后续Root只跑这组实际父子workflow及已选的Font上下文，不新增理论全排列。跨SID/logout、其它UID/GID ACL、Drop窗口票据、GUI同时输入与broker唤醒及异常路径仍按它们的真实接线安排，不由本应用默认计PASS。

静态物理include图为8文件、正文71017B/各源一个NUL后71025B，宏名并集含11预定义228项；这是文件读取快照，不是actual token/Pool/code或原生容量PASS。按确定性构造生成的131079B测试正文SHA256为 `7f42bb6e02fe5188fab31efda04d617bb5bb9bf08f68c66f5f4595e9e3ec0796`，三个Read长度为65536/65536/7。该摘要只固定预期fixture，未冒充读取了客体剪贴板；客体内容由实际Read逐字节核对。

全机PF回收由Root在实际任务完全退出、broker分批GC结束之后记录。Session epoch元数据会留到登录session寿命结束，Font cache会保留真实暖态资源；已知首次增长与真正泄漏分别记录，不能只看parent的user PTE回基线便称全底座回收完成。

## 修订

2026-10-15：新增纯公开SC32动态平台应用，真实mio登录父子剪贴板/IPC/18页共享工作流、Font/Palette前缀、精确身份与PTE日志、受控失败清理；源/JSON/静态合同收齐，未构建或运行。
