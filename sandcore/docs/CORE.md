# M10a1 磁盘主内核与签名扩展

> **2026-10-07最新实测：** core-08冻结10两来源各六组扩展/恢复声明用例全部通过，失败初始化与无失败对照的活对象/池页账一致；现场ATA失败/超时均0。默认公开键/已签WALL100的formal-build-01退出0，MAIN与受测冻结10全字节相同，两正常新迁移盘仅含编号100扩展。Shift/其它完整格式/资源边界仍待验；下方“第二来源未收齐/默认构建待验”是前一批次状态，不覆盖此记录。私钥始终只由用户持有。

> 2026-10-07当前。MAIN/loader已实际构建、装两来源盘并在Windows QEMU启动；冻结10第一来源六组扩展/恢复声明用例通过，第二来源完整矩阵因宿主重启未收齐，独立重测继续。用户六份公开签名已独立验证，默认构建接仓库公开键与已签WALL100，普通整包新发布仍待验；验收10/20/30夹具不进入正常扩展目录。M9的CORE.SKM壁纸不是这里的主内核。正式私钥始终只由用户持有。

## 启动与恢复

BIOS仍从1.44MiB软盘启动。boot.asm在M10_LOADER模式读取LBA1起128扇区到0x10000，然后进入最小loader。loader只链接只读ATA、SandFS目录定位、CRC/SHA256、早期串口/VGA诊断和交接，不含调度、权限、完整FS、桌面、音频、网络或SCAPI。初始化段不超过64KiB，BSS独立在0x20000..0x28000，栈顶0x90000。

loader从IDE数据盘读取SYS/CORE/CORE.SKM；格式、边界、活动目录CRC、同名冲突、与其它文件的扇区重叠、E820可用范围、载荷SHA256均通过后，装入8MiB。初始化载荷上限4MiB，BSS页对齐，映像及BSS终点不超过16MiB。内核入口先复制交接快照，memory_init后立即预留真实占用物理页。主内核更新重启生效。

主内核失败或BIOS记录的Shift状态有效时，尝试SYS/RECOVERY/CORE.SKM；两者都失败则诊断并停机，不尝试执行部分映像。恢复目录不参与扩展扫描，受内核最高管理路径保护。首次迁移生成的恢复副本与本轮候选核心同源，**只有实际启动证据到达后才可称为已知可用恢复核心**。后续迁移保留已有合法恢复副本。CRC/SHA用于检测损坏；该主内核合同没有把它们称作密码签名。

## SKM2 文件布局

所有u32为小端，固定头128B，magic为8B的SKM2MIO加NUL。版本2，header_bytes=128，保留字段必须0。

| 偏移 | 字段 |
|---|---|
| 8..36，每4B | version、header_bytes、kind、flags、number、abi_min、abi_max、load_base |
| 40..68，每4B | entry_offset、image_bytes、bss_offset、bss_bytes、memory_bytes、relocations、required_cpu、file_bytes |
| 72..103 | SHA256，32B；MAIN覆盖初始化载荷，EXT覆盖初始化载荷＋重定位表 |
| 104..123 | 五个保留u32 |
| 124..127 | 前124B的CRC32 |
| 128起 | 初始化载荷、随后relocations个u32重定位偏移 |
| EXT文件尾64B | Ed25519签名；MAIN没有此尾部 |

MAIN：kind=1，flags/number/relocations/required_cpu=0，abi_min=abi_max=1，load_base=0x800000，entry_offset在初始化载荷内。bss_offset页对齐且不早于载荷末端，memory_bytes=bss_offset+bss_bytes，file_bytes=128+image_bytes。tools/mkcore.py以ELF32链接符号为权威生成，不能把旧壁纸SKM1改名当主核。

EXT：kind=2，flags=0保留，number为非零31位唯一编号，load_base=0，当前abi_min=abi_max=1。bss_offset=image_bytes，memory_bytes=image_bytes+bss_bytes≤16MiB，载荷≤4MiB。required_cpu目前只允许值1的SSE2位；其余位拒绝。重定位表严格递增、相邻至少4B，每个偏移与原值必须处于相应映像范围。完整文件长度包括64B签名，签署正文为文件前file_bytes-64字节，因而编号、ABI、入口、BSS、重定位、CPU要求、载荷与摘要都受认证。通过CORE链的EXT统一常驻，不以保留flags临时扩合同。

## 公钥与开机执行

正式公钥为用户提供的32B原始Ed25519公钥，由mkcore_public_key.py写入构建头后固化在主内核。该工具不接受私钥/PEM，不生成键。未配置/不可用公钥时，所有CORE扩展拒绝并记NO_USER_KEY；没有开发旁路、客体可写信任库或自动信任的测试键。内核验签采用固定Monocypher 4.0.3，另拒绝非规范Edwards编码与低阶公钥/随机量。

只扫描SYS/CORE的直接子文件*.SKM，跳过CORE.SKM；SCX、恢复目录、旧SKM1不在签名自动链。先对所有候选验证格式/资源/ABI/签名，保存文件代数、完整摘要与内部编号，以堆排序按编号升序排列。重复编号的**全部冲突候选**拒绝。执行前重新读取并再次验证代数/摘要/编号，防止初始化前后文件变化。每个通过项每次开机调用一次初始化；失败撤销注册项/辅助页并恢复前场景，成功常驻到重启，不热卸载/热重载。签名授权的零环程序不是沙箱，异常仍走内核诊断。

旧SYS/MOD的SKM1格式及外部串口SYSTEM管理入口保留；手工执行CORE/RECOVERY路径拒绝。旧壁纸原字节归档LEGACY/SKM/WALL-V1.SKM，新签名请求编号100，以独立WALL.SKM发布，缺签名时使用已有内核壁纸回退。

## 常驻服务 ABI

module_api_t仍version=1，旧28/36/56B前缀与字段偏移保留。M10表为92B，模块先检查size再读取尾部。新增IRQ注册、服务注册/唤醒及8/16/32位端口读写；完整定义见kernel/module.h与用户SCKERNEL.H。

IRQ注册只接受0..15，回调在已有设备处理后、PIC EOI前执行；仅成功常驻模块的回调可运行。IRQ内禁止注册/分配，设备回调应只确认硬件并投递待办。服务回调在任务0受控上下文执行；每tick最多轮转64个注册项，period=0按投递唤醒，正周期按PIT tick到期。64是每批预算，不是服务数量上限。模块应在成功后的服务中启用硬件，避免初始化未提交期间抢占共享IRQ。可信回调须及时返回，不把操作次数预算称作延迟保证。

正周期从上次回调返回后再等待period个tick，不追赶同步IO期间错过
的周期；回调内新投递的pending事件保留，period=0仍按真实投递运行。
core-06显示常驻服务每次写入目录bank时逐扇flush，执行超过周期后
又立即到期，管理ACK出现长等待；10补完成后计时和ATA调用末一次
flush，权限/编号/初始化次数/常驻资源合同不变，完整矩阵继续重测。

修订：2026-10-07，周期服务以完成后间隔避免追赶阻塞，保留pending；
记录实际管理延迟与ATA批次修正，未把待验新主核称完整PASS。

常驻记录、辅助分配和回调均为动态稳定对象，镜像使用真实PF页；不再以32模块/64分配/固定1MiB常驻区限制系统。一次性MOD不能留下跨返回回调，失败整组回滚；成功常驻资源寿命到重启。

## 用户离线签署交接

tools/mkext.py输出WALL.SKM.msg及编号/用途/摘要JSON，状态UNSIGNED_USER_SIGNATURE_REQUIRED；此正文不能安装为已签扩展。用户在自己的私钥环境签署精确原字节并只交回32B公钥与64B签名。代理不生成、读取、保存或调用私钥，也不代用户签署。

用户可在自己的机器使用OpenSSL Ed25519的pkeyutl -sign -rawin签署，私钥路径由用户自行保管。收到公开结果后，tools/core_signature.py仅用公开DER封装和OpenSSL -verify -rawin独立验签，然后组装正文+签名，输出HOST_SIGNATURE_VERIFIED_RUNTIME_PENDING。宿主验签通过不等于客体装载通过。

用户尚无密钥时，亲自双击根目录`sign-m10a1-yourself.bat`，对话框中
选新建、仓库外保存位置与密码。`tools/user_sign_m10.py`用本机已安装
宿主库创建Ed25519密钥、保存加密PKCS8，核对六份消息SHA256后签完整
原字节。结果目录只含公钥/签名/公开摘要，私钥位置/密码不入结果。
代理不得执行生成/签署界面，也不得因得到工具源码就读取用户私钥。
2026-10-07已收到用户独立生成的公开结果。`receive_m10_signatures.py`
只读32B公钥/六签名/约定正文，用独立OpenSSL对六份精确正文验签，
并实际翻转正文一字节核对拒绝。四份正常格式进入`signed`，两份签名
有效但ABI/重定位故意错误的夹具进入`rejection-fixtures`，正式发布
仍必须经过完整格式边界。`build/m10a1-public-receipt-01/receipt.json`
为SIX_USER_SIGNATURES_HOST_VERIFIED_RUNTIME_PENDING；客体矩阵待验。
用户密码仅用于解锁加密PKCS8私钥，系统验签不需要密码；代理没有
执行签署工具或读取私钥/密码。

待全量实现收齐后，统一验证冷启动、坏主核/恢复、编号逆目录顺序/冲突/篡改/重定位/ABI/初始化失败、IRQ/服务生命周期、历史SKM及真实页计账。公钥/签名缺失时正向链如实待验，其它实现继续。

修订：2026-10-06，定义磁盘MAIN/签名EXT/最小loader/恢复/92B服务和用户独占私钥合同；源码未构建未运行。

修订：2026-10-06，登记两盘MAIN实际启动及六消息待签包，提供仅由
用户亲自操作的离线签署界面；无代理私钥操作/正向验签PASS。

修订：2026-10-07，接收用户公开结果，独立验签六正文及篡改拒绝通过，
明确错误格式夹具与正式发布边界；完整客体启动/回滚矩阵待执行。

2026-10-07原恢复核实际验收：保留的fa9f0d39…必须与其原ELF/bin/sym一致，不能以当前981ee90b…重包装替代后宣称旧恢复可用。独立坏主核QA盘的两来源实际走SYS/RECOVERY/CORE.SKM、handoff恢复位1及原正文3e57a363…，用户已签10/20/30/100按编号初始化、失败30清理、20常驻PIT/服务继续、旧32B模块ABI与禁止手工MAIN/RECOVERY初始化通过；原盘保持只读。实际证据为build/m10a1-preserved-recovery-01，恢复旧核仍不包含当前候选的新增修复。

修订：2026-10-07，补原恢复文件/原ELF符号/真实loader回退与同用户签名服务的双盘证据边界。
