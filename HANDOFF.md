# HANDOFF.md —— 沙核 OS (SandCore) 交接文档

> 2026-10-07：首份M10a1包默认启动器遗漏stop事件，已修复用户解压目录及仓库/新验收包；实际批处理默认启动、桌面、SYSTEM串口与正常退出通过。镜像和用户会话保持。

> **2026-10-07 开发验收包已提交：** M10a1约定实现与收尾完成，等待用户验收。动态任务/独立会话/隐藏停画、磁盘主核与用户签名扩展、原始凤凰TTF、256MiB盘与编辑器、e1000/IPv4和19个网络工具已接线。新增curl系统内编译及14项定向检查通过，包含最终镜像启动/联网/桌面截图；已有有效双来源证据按实际源码影响复用，停止新增测试。原18对应18/55，curl单列追加；HTTP可用，HTTPS/TLS尚未实现。入口见[M10a1验收说明](sandcore/docs/M10A1-ACCEPTANCE.md)，不创建Release。

以下旧阶段状态与失败记录是历史沿革，不能作为当前待办清单；最新结果以验收说明为准。

> **2026-10-07清理已完成：** 用户授权build除代码外无用产物全删。已删14034份/118.631GiB，14318份源码保留，错误和缺失均0；项目现14.518GiB，sandcore/build6.837GiB。当前候选双盘、MAIN/符号/完整fs、引导与编辑器保留；原盘、正式版本、代码归档和公开签名保留。历史重复镜像/部分截图已明确授权删除，旧路径仅为历史记录；清单/result在build/cleanup-approved-build-20261007-*.json。未运行新QEMU，不恢复已停止的扩展测试队列。

> **2026-10-07用户最新指令，优先执行：** M10a1约定功能实现和资源接线已写齐，用户要求停止画蛇添足的验证。AGENTS第十二节已持久化：复用未受影响组件证据；仅明确故障、实质交付缺项和必要最终检查，不继续扩夹具/组合、复制完整测试盘或因主核摘要变化重跑全部矩阵。当前无QEMU运行；普通mio/root三故障卡双盘24项已通过。目录133.145GiB，sandcore/build125.182GiB，其中1493个镜像113.868GiB；未删除历史或接触私钥。接下来优先整理实际可用交付入口并处理具体问题，下方历次待验清单不是无限追加测试授权。

> **2026-10-07当前：** M10a1完整Goal继续，尚未整体验收。用户公钥/六份签名有效，迟交未导致返工或重签。新MAIN981ee90b…修隐藏故障桌面清理后的模态残留：同字节双盘HMP夹具22项通过；session12双盘各两轮65会话全部通过，冷PID历史页5→11/其余回收，暖PF与记录/旁表页无增长，12张完整工作区逐字节保留。preserved-recovery01两盘真实坏主核回退到原fa9f0d39…核，原ELF/载荷重包装一致、同用户签名初始化与常驻服务通过；非用新核替代恢复核。原盘/全部历史/权限/公开签名保留；新候选其它完整键鼠、多卡/多普通会话异常、真实GUI、设备/存储/字体边界等合同继续待验。当前无运行VM，无私钥访问/commit/push/Release。详细证据和范围见M10A1-VERIFICATION.md；以下旧接力记录保留为历史。

> **2026-10-07最新接力：** dhcp-fix02独立build03退出0，648份构建输入及实际产物冻结；MAIN正文381584B/BSS332840B、完整SHA35084bcd37178b5c68377574caa4ba6b73629dd71e7a6bd36d1cfb7d3f23ff7b，默认T2派生只替换一处表达式，上游原件/全部声明原字节保持。publish03两份256MiB盘退出0，各437份当前文件精确核对，225/167固定归档及实际派生/生成工具齐全，原恢复CORE保持。唯一runner long03/PID18032运行四模式×双来源；第一来源四模式全部18项通过，最大有限默认T1/T2、86400秒、显式无限及有限→无限自动续期/计数清零/6500tick越过旧到期时段保持/实际ping全部通过，源盘摘要保持；第二来源与完整双盘矩阵未收齐。旧short02双来源22项通过保留，不覆盖新MAIN；short03/core10/font06/network13脚本已匹配新CORE/inputs03准备，未启动。公钥/六份签名再次独立验证，无私钥访问或重新签署；完整Goal active，无commit/push/Release。

> **2026-10-07租约边界当前：** dhcp-lifecycle02退出0，两来源全部三策略共22声明检查通过，独立线缆/PIT/CPU/源盘不变审计已保存，实际1920×1080桌面已查看。long01夹具误用不存在的sc_sleep而原生链接失败，已用旧公开timed event wait修正；long02原生编译通过，在真实0xFFFFFFFE租约、省略T1/T2时读到t2=536870910而正确值3758096382，t1_timeout错误归零，完整FAIL已保存。现补固定BSD原件生成的一处无溢出默认T2派生与完整实际源码归档，dhcp-fix02正在准备独立构建，尚未验证。公钥和六份签名再次独立验证通过，无私钥访问；完整Goal active，无commit/push/Release。

> **2026-10-07 DHCP修复候选当前：** 原desktop02的短租约续期实际FAIL已保存。独立构建01因SINGLE_NETIF宏内continue失败，修正后build02退出0，完整643份输入冻结保留；新CORE SHA64da4e2e9615b771a4187b0d89f79f13821e104ca61cf527f63f46a9df421b59。第一次迁移因许可证汇总文档陈旧在运行前拒绝；补完整归档后publish02退出0，两份256MiB来源各434份当前文件逐字节一致，M9/M10固定归档225/167份通过，恢复CORE原字节保留。公钥嵌入主核，WALL100用用户公钥再次验签通过，无私钥访问或重新签署。唯一runner dhcp-lifecycle02/PID8576正在新候选执行续期/重绑定/到期三模式×双来源，尚无终态；旧network12的308项/资源01的16项/其它旧主核证据不覆盖新主核。完整Goal active，无commit/push/Release。

> **2026-10-07资源01终态：** 退出0，双来源16声明/20原生检查全部PASS，创建未绑定UDP388125/388349后真实-12，八失败重试稳定；回收后PF54808/54838各精确回原值，协议页2→2，旧句柄无效，真实ping/零逻辑对象正常。两来源fixture SHA75dc2749122f44640ae1a454a3da904deffcc662fd7b81ab62b4d7552667add3，完整冻结01保留；之后只修未绑定PCB尚不进入udp_pcbs链这一原因注释，无行为变化。network12两来源308声明终态保持。当前唯一runner dhcp-lifecycle01/PID15596已启动原desktop02候选60秒租约/T1=20/T2=40自动续期/丢续期重绑定/完全断应答到期恢复，尚无终态，未改内核粒度。终态审计在sandcore/build/m10a1-network-resources-01/declared-audit.json；完整Goal active，无commit/push/Release。

> **2026-10-07 network12已退出0：** 两来源三profile全部收齐，matrix=DECLARED_NETWORK_CASES_PASS，隔离57/应用93/无网卡4各两份，总308项全部PASS/native_required/complete_profiles/源摘要保持已经审计，入口sandcore/build/m10a1-network-12/declared-audit.json。第二来源应用桌面1920×1080实际查看，字体/图标/原壁纸正常，无SYSTEM管理窗口进入mio桌面。新的唯一runner resources01/PID17616正在同一desktop02/desktop-inputs02双盘验证，两个探针客体编译及静态配置已通过；终态待收齐。DHCP lifecycle01的三份新原创源码/20文件冻结/runner只准备未运行；先观察资源终态，再按单VM顺序实测。完整Goal active，主核/公钥/签名不变，无commit/push/Release。

> **2026-10-07网络资源接线：** 唯一运行runner为network12/PID18348，第一来源三个profile已通过，第二来源正在隔离链路；以最终JSON/exit为准。新M10NRES.C/verify_m10_network_resources.py在当前desktop02候选补跨PID权限、隐藏普通mio实际UDP、未close退出、真正socket耗尽/八次失败回滚/完整关闭后真实PF账与旧句柄失效。夹具失败恢复stdio/票据/管道/孩子；关闭等待内核真实完成，逆序撤销UDP避免测试人为尾链二次方扫描。AST/21文件冻结通过，resources-run01只准备，尚未启动。冻结入口build/m10a1-network-resources01-source-freeze/manifest.json；输入仍desktop-inputs02，签名结果不变。短DHCP租约生命周期待独立实测，完整Goal active，无commit/push/Release。

> **2026-10-07原始TCP终态：** tcp-faults01退出0，两来源各9项声明检查通过。原创隔离对端真实丢首SYN/首数据、0窗口后重新打开；两盘各实见2次SYN、丢段再次发送、零窗口期间1个包、完整16384B上行SHA6c946c0a9501d2a9fb9dc65bb931071233fb28a2820a8be06344e1482f5e6bfc，客体4097B乱序/重复/32位序号回绕全部精确一次接收/EOF/半关闭、拒绝连接RST与建立后RST及64B状态护栏均通过。源盘保持；netstat项仅查询不充物理页回收或全部拥塞/吞吐。当前CORE仍desktop02。font-edges02双盘18启动、Notes03双盘22项、sessions09/10及最终12图均声明通过。network12已启动完整三profile/双盘/18原生工具重测，唯一当前VM；原network11租约前置失败保持。完整Goal active，无commit/push/Release。

> **2026-10-07字体拒绝终态：** font-edges02退出0，两来源各九种原文件/缺12/缺16/双缺/截断12/坏目录/cmap/loca/轮廓16全部实际启动、客体编译与声明ABI/原子激活/页归零/原脸保留通过，18副本及两原来源摘要不变。已实际查看第二来源双缺TTF的桌面：旧ASCII与Start字形仍可辨识。01的FONT8夹具错误失败保留，内核ABI保持1024B/返回1024。Notes03双来源22项声明通过、sessions09/10和12最终像素证据保持；网络11短租约撤地址失败保持，network12准备但未启动。tcp-faults01 PID3256在当前desktop02主核双盘进行原创原始TCP线缆故障实测，未有终态。当前只此VM；Goal active，无commit/push/Release。

> **2026-10-07当前：** desktop02主核不变。sessions-09双来源22项及两轮65桌面声明通过，sessions-10两来源12张完整原尺寸工作区与原同步06逐字节一致。Notes03退出0，两来源各11项原生/六编辑/启动注销恢复通过；GET代数变化仅重开完整下载3/2次，七张实际中文/光标/滚动图已审阅。network11退出1：60000B发送前短租约到期，PIT6012、地址0/状态CHECKING8，无60000B帧、DMA/驱动/ATA/坏释放均未报错；不称分片通过。分片矩阵新增明确静态地址前置，短租约续期/到期仍待单验，network12未启动。font-edges01首原文件八项新脸/旧GLYPH16通过，测试错误把FONT8写成760B/返回0；旧合同及基线均1024B/返回1024，内核保持。已改夹具1026B护栏，font-edges02 PID13904执行九类×双盘。原创有限三连接TCP故障对端/探针已写并冻结，仅静态检查，tcp01尚未运行。Goal active，无commit/push/Release。

> **2026-10-07最终画面已核：** sessions-10退出0，两来源各11阶段/22项通过；desktop-pixels-03的12張1920×1032完整工作区与原同步06逐字节相同。之前09前三张确实过早取到程序壁纸，原证据保持。当前CORE仍desktop-fix-02/完整SHA94e7833c3638d9edf318dbac13862ce7e051c51379db60e89e57e64fb3c9ef4f，原用户公钥/扩展签名保持。Notes03 PID9700首来源原生编译和六编辑保存/注销均通过、GET换代重开3次，第二来源RUNNING；中文/混合字宽/滚动图实际已看。network11/font-edges01 runner准备但未启动；先等Notes终态，单VM诊断。Goal active，无commit/push/Release。

> **2026-10-07当前终态：** sessions-09退出0，两来源各11阶段/22项检查、同进程两轮65个桌面创建/分页/注销全部通过。冷PF各减少6页，精确对应PID历史记录5→11页；暖轮PF与记录页数不再增长，旁表45→45/所有者4→4。此范围不是全部会话/资源验收。原同步sessions-06与09六工作区比较，前3张两来源均不同、后3张一致；目前未宣布旧/新最终画面等价，sessions-10只重测控制器并观察pending归零之后真正完成的新帧（不冒充完整65资源矩阵）。Notes02原生编译及普通mio启动通过，但首次保存期间GET_DATA -8使宿主失败；内核拒绝读混代数据符合合同。仅针对该精确错误重开完整GET的新宿主逻辑尚待Notes03实测。用户公开签名有效且无需重签，无commit/push/Release，Goal active。

> **2026-10-07最新：** desktop-build/publish-02均退出0，正文381584B/BSS332840B，完整CORE SHA94e7833c3638d9edf318dbac13862ce7e051c51379db60e89e57e64fb3c9ef4f；两新256MiB盘1514/1476项，原inputs-03保持。session-run-09 PID14480正在双来源完整重测：首来源11阶段/22项会话检查通过，真实65桌面同进程两轮均全部就绪与注销通过，旁表45页两轮前后相等；冷PF减少6页精确对应已提交PID历史记录5→11页，暖PF55929→55929/历史11→11不再增长。第一来源六个1920×1032完整工作区的候选01/02像素全字节一致，排除的只有48行任务栏时钟；IO批次最大观察由96tick降至4tick，仅此夹具，不代表整机/全部GUI长尾达标。第二来源和完整矩阵仍RUNNING，不提前PASS。网络host脚本新增公开时钟/服务前后观察和超时具名失败记录，原40秒/ACK超时不变，尚未在新主核上执行。Notes02、字体拒绝/缩放/缓存、CORE新体和其它完整合同继续；无commit/push/Release，Goal active。

> **2026-10-07 desktop候选01实际：** build/publish-01均退出0，两副本各431份归档/当前候选字节审计通过，完整CORE SHA7ae416aa3aec53ff429cc10de5f6ea242d7ee116af45d35e6f4e72ff1ee24f4a。sessions-08首来源11阶段实际全部走完、客体22项PASS/session_failures=0/exit0，管理心跳持续；宿主错误要求22个名称都不同，而三普通任务身份检查名称实际重复3次，因此runner仍退出1，65冷暖未执行，原FAIL保留。现按20个明确声明名称及各自真实次数（合计22）核对，不改客体行为。新计数显示最终壁纸透明合成仍一次96tick；现将原整数alpha公式按读完的完整像素同步分批，不显示半幅、不降低像素。独立desktop-build-02 PID15992执行中；下一轮源码/符号/新CORE另冻结，08旧体数据不当新体PASS。完整Goal active，无commit/push/Release。

> **2026-10-07当前终态与修复：** network-10已退出1：第一来源isolated56/applications93/no-nic4声明通过，第二来源controlled-DHCP的40秒宿主标记等待超时，随后实际BOUND且命令退出0，完整矩阵仍FAIL。sessions-07退出1，首来源原生编译/初始隔离通过，initial后管理ACK超时，冷暖页账尚未执行。QMP两次均看到task0在读盘、时钟推进、render_hold=1、UART接收待处理、无ATA失败；读盘LBA精确落在6,293,440B的SYS/WALL/AURORA.SCB。当前将桌面原始图片改为每服务轮16KiB/两PIT tick软预算、候选原子交付/代数与rw复核/隐藏暂停/退出取消；不改图片分辨率和原像素，不放宽协议超时。新增源码尚未构建/实测，旧PASS不覆盖；网络计时问题仍须独立核对。用户签名已接入且有效，晚提交只延后正向验证。完整Goal active，无commit/push/Release，原盘与失败证据保留。

> **2026-10-07 Shell真实回归通过：** shell-run-01退出0，两只读来源盘各31项、共62項stdin回归通过，源盘摘要保持。文件/管道EOF、无末尾LF、连接未结束时exit、跨行引号/循环/heredoc、CRLF/Tab、read及子进程保留后续输入、坏控制/NUL/未结束语法拒绝、旧脚本/-c和显式-i都有精确输出/退出码/错误/日志。此范围不冒充全部Shell或整机验收。publish-03双来源各429文件归档/当前SRC头逐字节核对通过，network-10 PID8632正在完整三profile/双来源/18原生工具重测，未收齐终态。新65会话冷暖资源页账探针已补，尚待执行；Goal active。

> **2026-10-07 network-09终态：** runner退出1。首来源isolated56项通过，新增地址复用/旧选项21项检查全部PASS；客体编译前15工具通过，nc源码第22行unknown identifier失败。只读输入确认SYS/INC/SCNET.H已更新，NETCLI相对包含的SYS/SRC/SCNET.H仍旧，故本次应用未继续，不能算全部网络通过。现补SRC副本并给网络SCX/源码增加双目录三份头的资源依赖，不改候选主核。publish-03新双盘退出0；network-10待当前独立Shell stdin回归结束后运行。shell-run-01 PID3964执行中，正常EOF/引号/循环/heredoc/read等已有分项，完整终态未收齐。全部原失败和原盘保留，Goal active。

> **2026-10-07最新终态：** sessions-06在net-reuse-fix-01新主核/inputs-02两个只读来源完成声明矩阵，runner退出0。两来源各11阶段/22项真实NUI检查通过，65个同UID独立登录会话和真实窗口全部就绪，分页查询含全部65个普通隐藏桌面，全部注销后的原会话ID被拒绝。隐藏窗口无新增客户帧、像素保持且后台推进；回到会话/恢复最小化窗口实际重画。每个输入源摘要不变；65回收后PF页数各少6页，缓存与完整资源总账待验，不能称全部页回收达标。普通密码/F12锁屏、鼠标/拖动/相对输入/故障卡、全部内置应用后台与隐藏生成、其它旁表和历史兼容仍待验。network-09 PID5420正在同一候选上完整三profile/双来源/18原生工具重测，并新增真实bind/listen地址复用与旧选项边界探针。完整Goal active；旧core10/font04证据不当作新主核验收。

> **2026-10-07当前验证状态：** 用户公钥和六份签名已经收到并验证，晚提供仅延后正向验签，不影响其它实现。冻结core10的core-08两来源12项声明profile、font-04全部30160映射记录通过；这些旧证据不作为新主核整体验收。network-08首来源三profile通过、第二来源隔离链路通过；第二来源应用在已完成47项后因nc连续监听返回-98而失败。现场PIT推进、UART/ATA无错误、坏释放记录全零，未复现此前tcp_free重复释放。当前已补socket选项5 REUSEADDR及nc TCP监听接线，保留TIME_WAIT；net-reuse-fix-01构建退出0，主核正文379536B/BSS327432B，正文SHA a33bf9f9cb18f7b82136cc87195ddb86541b195471ca8200bde7fd3601c4c4a5。完整第三方167份固定归档已核对，补最新声明后独立双盘publish-02退出0，两个盘各428份归档/候选文件逐字节核对通过。sessions-05真实NUI的11阶段/22项隔离、输入和隐藏停画检查通过，65任务创建但夹具窗口尺寸不合法而未就绪；已改128×96并要求真实65个ID，session-06 PID1276已在新net-reuse-inputs-02双盘单独重测，未收齐终态。完整Goal active，未commit/push/Release；当前新主核尚未获得整体验收，下方旧运行状态保留沿革。

> **2026-10-07 02:51更新：** 用户晚提供公开签名不影响既有实现，原待验项已用用户结果接入，core-08声明双来源12项已通过，无需用户重签。network-run-05与session-run-03均已退出1，前文“执行中”为沿革。05实际nc-e sh只回7B `M10-SH\n`、原Shell取得退出7、普通root网络子程序权限检查通过，随后等待原作业时INPUT及PING ACK超时，完整网络矩阵失败；03真实会话走到initial阶段后管理ACK超时，完整会话未通过。已增加独立于UART的只读QMP失败现场、保留原异常并单列清理错误。当前只启动session-run-04 PID15136，独立sessions-04；不与网络VM并跑，先取得失败CPU/队列/ATA现场，再安排网络诊断。Goal active，无commit/push/Release；不放宽协议超时、不归因于签名。

> **2026-10-07最新接力：** core-run-08/font-run-04均退出0：两来源各六CORE声明profile完整PASS/失败初始化活资源对照相同/ATA失败超时0；两盘两脸各7540原生映射逐像素相同、源盘不变，当前WSL参考版本2.14.2/lib SHA de0b38b01924302a7fa7bfbaee47afcb9468ad2f8a22b4a8cca56ad54f64569c。formal-build-01及formal-migrate-02均退出0，默认用户公开键与WALL100已接，MAIN和冻结10全字节相同，输出formal-01/02两盘256MiB、1489/1451项，仅正常WALL100。旧原M10盘仍05、M9源保持。network-04首盘isolated完整声明PASS、18原生工具编译/启动与nc-e cat原Shell等待退出PASS；nc-e sh实收58B欢迎语/提示符/回显而非7B正文，整轮失败，保存JSON/pcap。user/m9/sh.c现非终端stdin逐行执行、EOF收齐语法/-i显式交互；定向build/m10a1-shell-fix-01已退出0，新shell-inputs-01双盘迁移退出0，network-run-05 PID3224正在全部三profile×两来源重测。sessions-01缺私有NUI include失败，原样上传NUI/SCMEM/GLYPHS后02原生编译PASS，但夹具错误用29..31描述符/误猜AUTH_PENDING；已按旧0..15域改13..15、AUTH_PENDING=1，不改内核ABI，session-run-03 PID8844/独立sessions-03执行中。真实会话和65同UID完整行为无PASS前不算完成。最新终态查新.log/.exit.txt与矩阵JSON；每项原子持久化。Goal active，无commit/push/Release；Notes/缩放坏字体/磁盘异常/网络完整故障/其它旁表与缓存/历史兼容全部继续，旧段落为沿革。

> **2026-10-07 02:07最新接力：** 宿主LastBootUpTime为01:52:31，core-run-07/font-run-03进程确实消失且无runner退出文件，不能继续等待旧PID。07第一来源六profile、第二来源前四profile的JSON有效且声明PASS；第二来源recovery的9778B报告全零，完整矩阵未收齐，旧文件全部保留。font-03第一来源两脸各7540实际映射/ABI边界及明确无hint FreeType逐像素PASS，第二来源未收齐。公钥32B SHA fc0f26d1b2f3cfea86ca19f3d5d37acd2fea932f683307887ace3bda7200e4f7与已签WALL100完整SHA 4ba998a48e72b623e078ba07de6d699081fc8462dc456bd0a6fe53091ddd7ea4重启后保持；源码文本无NUL。10主核已归档ata-batch-core-01，ATA改每次多扇区调用末尾统一FLUSH、正周期服务从回调返回计时；旧盘仍05，默认公开键/壁纸已接仓库但默认整包重建/发布尚待验。新报告每项完成原子替换并fsync保存RUNNING检查点，不把中断标PASS。已启动全新core-run-08 PID6592与font-run-04 PID5676、独立core-08/font-04重新收齐，两轮均使用冻结10主核与原用户签名；终态查新.log/.exit.txt及矩阵JSON，不凭PID旧记录重开。Goal active，无commit/push/Release，完整其它合同继续。下方旧“正在运行/尚无公钥/默认拒绝”仅保留沿革。

> **2026-10-07更新接力：** after-03完整双盘退出0，两盘16真实HMP键确认15..32毫秒；初始及16键后共34张1920×1032真实工作区逐字节相同，实际截图完成78..141毫秒，数据只代表冻结夹具。用户六份公开签名独立验证完成，core-build-08用户公钥MAIN退出0并归档m10a1-user-key-core-01，两原开发盘仍是05。core-02首盘positive及without-failed-init完整通过，编号/常驻IRQ服务/旧32B边界/无手工热重载和14页实际模块账有证据；core-01周期GET代数变化、02宿主篡改错偏移、03管理心跳超时、04HELLO过早失败全部保留。04现场CPU仍在gfx_argb_pset，COM2收77B未消费、无丢字节/线错、管理last_sequence=0；测试工具现在只读匹配符号boot_stage/wm_frames，正常HMP ESC通过欢迎页，等实际桌面首帧后才HELLO，不改内核权限/租约/正常协议超时。core-run-05隐藏进程9796正在完整重测并只复用core-02两项摘要匹配的实际PASS；字体原生全量字形双来源测试font-run-01也已启动。最新终态查各runner .log/.exit.txt/.pid.txt和矩阵JSON，不能照下方旧进程状态继续。Goal active，无commit/push/Release；其余网络/身份/会话/坏字体/磁盘/编辑器/兼容完整合同继续。

> **2026-10-07当前接力：** 用户已独立保存加密私钥，仅交公开结果`build/m10a1-signing-01/user-signatures-20261006-225411`。public-receipt-01六份原正文/64B签名经独立Git OpenSSL实际验证、六份翻字节拒绝、摘要匹配通过；四正常与两故意坏格式分目录，未读取/执行私钥或签署流程。新verify_m10_core.py/M10CORE.C已写真实编号排序/冲突/篡改/ABI/重定位/失败回滚/常驻IRQ服务/外部COM1池账/恢复矩阵，刚静态检查，尚未运行。core-build-06/07均退出0；after-01双盘有界唤醒对照收齐，p50均0.672秒但首盘最大2.125秒。07修局部壁纸计算和RGB填充/文字精确损伤；after-02首盘16键15..32毫秒、131计算/回收通过，第二盘中断且handle/VM确实消失，证据保留；after-03完整双盘重新测量在独立隐藏PowerShell进程17528运行，路径build/m10a1-scheduling-run-03.log/.exit.txt/.pid.txt，不要同时启动其它QEMU/重构建影响性能。两精确输入盘在m10a1-scheduling-inputs-02，仅MAIN追加尾部，其它内容/身份/位置保持。待after-03终态后做1920×1032真实工作区字节比较，再归档07 CORE/符号并用M10_PUBLIC_KEY构建用户公钥主核，执行双盘签名/恢复、网络原Shell wait重测及完整剩余合同。Goal active，无commit/push/Release；以下旧“尚无公钥/调度未优化/无活跃VM”仅沿革。

> **同轮测量收齐：** scheduling-before-03退出0、两来源各16真实HMP键/17原生截图、131计算任务/私有管道/作业均正确，任务5→136→5、原输入不变；创建17752/17535tick，公平6153/6151tick，每孩子分派35..36。按键确认p95/最大2.203/2.156秒（含宿主注入/UART观察），全项239.438/237.203秒，PF各少13页/记录各多12页，冷缓存完整总账未验；不得称性能达标。第二盘实际截图已查看。修后没有活跃VM/terminal session，产品调度器尚未优化；下一步用此冻结夹具做有界唤醒响应/公平调度定位与匹配前后测量，网络应用需重测原Shell wait修复，完整身份/会话/TTF/恢复/编辑器/历史兼容等继续。人类签署步骤已清楚给出，尚无公开结果；只允许读其公开结果目录，不扫描私钥。Goal active，无commit/push/Release。

> **2026-10-06本轮接力：** network-03首盘isolated全部声明用例通过，applications首盘18工具实际S3C编译/启动通过，TCP/UDP/端口扫描及nc -e cat实传16402B通过；随后等待错误跨Shell造成127，新增m10_guest_jobs在原交互Shell wait，修后退出/身份/HTTP/TFTP待重测。02 DNS错误为宿主event(name)参数与详情name冲突，已改event_name；失败/抓包保留。scheduling-before-01真实131计算/16键像素均有证据但脚本提前关GUI破坏任务基点；02窗口输出与Shell标记逐段交错超时，现已修为整行提交并保持GUI到负载回收后关闭，before-03正在双来源重测。无调度产品性能改动/达标声明。用户“还没有，需要操作步骤”，已明确双击sign-m10a1-yourself.bat→已有私钥选否→仓库外保存/设置密码→粘贴公开结果目录；代理未运行签署、不读取私钥。无commit/push/Release，Goal active，以下旧运行状态属沿革。

> **同日重测接力：** build-05已退出0，MAIN正文377808B/BSS327272B，两迁移盘更新且旧候选/日志/测试副本保留。network-diagnostic-03真实state2/link1/error0/DMA130、累计3146B报文；3秒DHCP诊断等待超时后ACD完成为BOUND10/10.23.0.2，不能将该短命令标PASS。已启动network-02完整20秒DHCP/双来源三网络profile矩阵，结果待收齐。network-diagnostic-01新增探针误用sc_write_fd导致原生链接失败，改既有cli_write后02/03实际编译通过，失败截图保留。签署工具已向用户说明4步，尚未收到公开结果目录；不读取任何私钥位置，不代跑。Goal active，以下旧“05执行中”现已过时。

> **2026-10-06最新接力：** lifecycle-01两来源盘各9项顶层用例/合计18项已收齐通过，原输入未变，真实耗尽分别1134/1135孩子后失败并回滚/再创建；18窗口、9截图68,702,760B、12事务、131并发/复用与旧票据检查有日志/HMP截图。计算公平分派22..23/23..24，但整项234.109/222.656秒，性能尚未验收。network-01首DHCP超时/双方无帧；diagnostic-02公开状态FAILED3/error5/DMA0确认启动复位先于字体/欢迎页、首次服务超过25tick，已补START_PENDING5延后实际提交及先读完成位再判超时，build-05正在WSL整包重建，修后未验。用户回答尚无Ed25519私钥，需要步骤；已提供sign-m10a1-yourself.bat/tools/user_sign_m10.py，仅用户亲自双击生成加密私钥并签六份消息，私钥只能仓库外保存，不联网、不记录其位置/密码，代理只静态检查，未代跑。待用户提供公开结果目录再独立验签；不得扫描用户私钥目录。Goal active，无commit/push/Release。最新实测入口M10A1-VERIFICATION.md，旧段落属沿革。

> **2026-10-06最新实测：** 统一build-04退出0；MAIN正文377680B/BSS327272B，loader7008B，18网络SCX全部打包；两个不同只读M9来源迁移为256MiB盘，原内容/权限/代数回读及源摘要保持，编辑器独立EXE构建通过且原EXE摘要不变。build/m10a1-lifecycle-01正在TCG/Windows QEMU双盘运行；首VM已真实CORE START/S3C编译，18窗口/超过64MiB截图、12事务、131并发任务作业私有管道、回滚/复用/再创建分项PASS。公平/真实耗尽/第二盘与其它完整矩阵未收齐，仍不得宣布完成；见M10A1-VERIFICATION.md/IMPLEMENTATION。无commit/push/Release，用户签名正向仍待输入，Goal active。下方早期构建状态为沿革。

> **2026-10-06统一构建实况：** 已启动WSL整包make m10a1，独立build/m10a1-work，源盘选固定发布M9基线。build-01/02失败日志与退出码保留；已修网络回调与lwIP宏重名、DHCP状态头文件、内核私有SYS_DEBUG名字冲突及Monocypher包含路径，build-03正在执行。尚无整包成功或客体运行PASS，用户原盘/旧编辑器EXE未覆盖；完整Goal继续active，不commit/push/Release。后续按实际构建结果修复并进入Windows双来源盘验收。

> **2026-10-06当前阶段：** 已按M10A1-DESIGN核对全产品实现、资源、规则与规范入口，进入已有授权覆盖的统一构建/客体编译/Windows双来源盘验收，不另请阶段审批。核对表sandcore/docs/M10A1-READINESS.md；首次构建还无成功证据。编辑器build.bat独立输出build/m10a1/sanddata_editor.exe，保留原M9 EXE。签名正向仍待用户公钥/签名，继续其它独立工作；完整Goal active。下方“禁止构建/未收齐”属于实现阶段沿革，不能作为继续停留静态阶段的理由，也不能把转阶段当成完成。

> **2026-10-06本次静态接力：** Monitor八任务列表改动态PROCESSPAGE并保留旧CPU/MONITOR缓冲；失败/Freeze/Resume基点核对。新增M10LIFE.C与双盘生命周期脚本：131任务/作业/私有管道、真实EOF退出/回收/复用、创建失败/耗尽/公平测量；M10RES.C补18原生窗口、超过8份/64MiB截图、12事务及显式PF回收。只是待执行源码。nc -e身份探针改按公开终端端点类型检查管道及无UART能力。全轮仍未构建未运行，继续完整实现收齐；可见性始终由内核决定，用户态仅据结果停画。见sandcore/docs/M10-LIFECYCLE.md、MONITOR.md、M10A1-IMPLEMENTATION.md。

> **2026-10-06当前任务：执行完整M10a1，Codex详细goal保持active，无额外token预算。** 基线24797e0，分支codex/m10a1；先全量源码/资源/规范，禁止提前构建/项目生成器/夹具/QEMU。动态任务/旁表/关联流作业/事务/窗口/截图、就绪/事件等待、分页工具、独立会话/隐藏停画、端点订阅和预算化退出已有源码；SKM2磁盘MAIN/只读loader/恢复、固定Monocypher公开验签与编号排序/92B动态常驻服务、256MiB内核/流式迁移/编辑器共享正文/独立构建链已写，均未构建未运行。初始恢复是待验候选，旧EXE/旧PASS不覆盖。原TTF/Notes及自写e1000/lwIP IPv4/动态socket/0x250..263已接线，18独立网络工具初版源码18/18，构建/行为0/18；用户追加nc -e已补两管道/程序作业与不继承UART授权的新入口。继续全轮静态审阅、启动/错误/验收接线，NETWORK.md/CLI-M10.md及新55行表记录精确范围。只收用户公钥/签名，不接触私钥，无用户键全部CORE扩展拒绝。无Release/未push/未commit，M9产物/M8封存及原盘保留。精确状态M10A1-IMPLEMENTATION.md、格式CORE.md；不能因本段源码进展宣布完成。

> **同日最新源码补充：** 原凤凰12/16 TTF原样归third_party/vonwaon与M10摘要表，自写整数加载/完整映射/直线轮廓扫描线、0x248/249与版本缓存已接线；旧GLYPH16既有字节、FONTINFO四字计数保留，新GUI/内核文字用统一TTF基线，Notes列宽/原Tab展开使用同源宽度。FONT.md定义实际支持的字体配置（这份原字体全部直线/无复合，不宣称任意TTF），font_coverage.py只写未执行。每脸原7543glyph静态事实不能代替客体覆盖。网络及18工具初版源码已补；独立run-m10a1.bat/private QMP/UART、双来源Ethernet/user/无网卡网络验收、原创对端服务及M10NET公开ABI探针已写。nc -e包括破管保留尾部、等待真实作业退出码；迁移INDEX自归档和编辑器目录插入失败提示已修。全轮仍未构建未运行，完整TCP故障/耗尽/身份/性能及其它矩阵待补；最新状态见首段。

> 2026-10-06最新：用户明确通过内存封面补丁，要求只commit、暂不创建Release，随M10大版本一起发布；本轮只做本地提交，不push。FLAC PICTURE及METADATA_BLOCK_PICTURE直接内存解码，16MiB编码正文上限；MP3 APIC复用内存解码，旁置图片原服务保留，Ogg-FLAC封面未加入。仅封面源码、构建接线及同步记录，内核/SCAPI不变；代理只构建，运行验收由用户完成。temp_miotest/整体忽略，用户音乐与试玩镜像不纳入Git，原M9发布包保持。

> 2026-10-06最新：按用户要求将宿主镜像编辑器全部迁入根目录sanddata_editor/，分为src、resources、docs、tests及忽略上传的build输出。入口sanddata_editor/sanddata_editor.exe，独立构建sanddata_editor/build.bat；旧根目录散文件已移动，源码与功能保持，原编译与验证输出一并保留。

> 2026-10-05最新：用户明确“M9验收通过”。M9正式发布，验收记录M9-ACCEPTANCE.md、发布M9-RELEASE.md；后文候选/待验为历史。按用户新要求在根目录新增独立自写C/Win32 SandFS镜像编辑器sanddata_editor.exe：浏览、多选、双向拖放、目录导入/导出、删除、F2改名、保存/另存、同目录原盘备份及外部改动拒绝覆盖；支持v1..v5并保留原格式/UID/GID/rw。原1022项独立Python解析逐字节/元数据核对通过，实际导入/改名/删除/Unicode/空文件/导出和原盘备份通过。该工具为宿主工具，不改变M9客体镜像，网络/GPU及原M8冻结合同继续。

> 2026-10-05备份复现最终：私有GitHub已保留完整当前/历史源码、字体输入、历史build/试玩源码及影片工程输入，并只保留M6/M7/M8a/M9每版一套精简镜像（共19.913MiB）。根build.bat默认M9，build-version.bat重建M6a/M6/M7/M8a独立快照。五版干净克隆宿主构建通过，M9启动盘与验收盘相同，串口17项、客体SCCC编译执行及HMP截图通过；旧快照逐文件摘要不变。仓库内精简基线读取替代本机原M7/M8a大ZIP依赖；四个可见Releases只链接Git构建包，0附件。范围与宿主/原生发布区别见SOURCE-BACKUP.md和REBUILD-VERIFICATION.md。保留GitHub用户删除.zcodeignore的提交。M9仍待用户验收，M8未完目标继续封存。

> 2026-10-05最终：M9本轮Windows QEMU验证通过，待用户验收。CLI按用户确认本地140/171（81.9%）公开支持子集，完整310原表/31本地缺项保留。build31内核213240B；最后内核92项、播放器92项、40个连续生命周期/82项、随包工具17项通过。报告sandcore/docs/M9-ACCEPTANCE.md；独立入口temp miotest/run-m9-acceptance.bat。下文旧“当前/待验”均是阶段记录。

CORE同色区段优化前后两盘仅CORE.SKM变化，完整1920×1032/1024×736工作区像素相同；实测空闲单核心CPU 7.42%/11.33%降为3.52%/6.25%，仅本机4秒样本。证据为build/m9-core-equivalence-01.json和m9-performance-20261005-02/03。08单次心跳超时根因未证实，09和最后40周期未复现。原试玩基线/会话、失败及历史证据保留，M8封存、M10网络/最终冻结和GPU安排保持。

修订：2026-10-05，记录M9最终实际回归/性能与待验收状态。GitHub为mio-qwq/SandCore私有仓库；用户最新明确只保存完整代码/资源/文档/许可与每版本一份构建产物。原历史工作区去重方案误收测试盘/渲染/视频，2.88GiB仅在本机，未上传；此前Release草稿及4个附件已删除。当前按精简范围整理，见sandcore/docs/SOURCE-BACKUP.md，不再上传历史数据/附件。

> 2026-10-05当前：新增FLAC/大封面/左下角模式与传输进度条已完成本轮分项验证；完整M9仍在验收。build24通过；player07两个不同来源盘各44项（合计88）通过，native PCM/seek/CRC/截断/内嵌和目录封面、两主题真实大屏/迷你/Open取消/恢复/全屏、无损串口截图和实际页回收均有证据。nofpu01双盘20项、20身份/GUI/原版M7回归双盘54项、progress01发布宿主菜单17项通过；sound03两来源有/无AC97共4VM、开关机/六音效16段波形连续及真实关机通过。新增225份第三方源码归档，dr_flac固定快照选择MIT-0。独立试玩入口temp miotest/run-m9-flac.bat，原M9镜像/用户会话保留。
> **2026-10-05当前：新增FLAC/大封面/左下角模式源接线待构建验证。** 原18双盘224项、19自举18项、fallback01 68项、FS05 26VM/136项通过；sound01真实开机声断续整体FAIL，build17修复待重录。试玩基线和用户会话保留，不覆盖。完整M9/80%CLI行为与剩余矩阵未完成；下文旧状态为沿革。


> **2026-10-05最新：试玩副本已交付；M9完整矩阵继续。** temp miotest/M9含独立镜像、run-m9.bat入口在上一层，启动/串口握手/退出检查通过，原有用户.c文件保留。默认继承最近试玩盘，所有会话保留，基线不写；开发测试另用副本。14零环/GUI两阶段两盘各12项PASS，宿主菜单02在真实COM2 IRQ断点停12秒后继续ACK/后续echo通过，修完整行STOP识别。15首盘全部九阶段95项PASS，但次盘缺CLASSIC.CFG，整批FAIL。build-13从固定M8a ZIP只读复用45份主题资源，未重启M8生成/渲染；build-12补16份固定M7原SCX到M9树。静态03核对M7的40API/29宏与M8a的76API/59宏、task_t及两盘16份LEGACY原字节通过，运行语义仍另验。16正在直接跑M7旧SCX；完整双盘/CLI行为覆盖/坏盘/音频故障/性能仍未完成。证据入口sandcore/docs/M9-VERIFICATION.md，旧进行中段落为沿革。

> **2026-10-05最新：build-08通过，06双盘集成重测进行中。** 05实际Windows QEMU已有66项PASS，包括串口完整性、Shell/AWK/压缩/散列、主题API切换及系统内s3c编译；随后IODENY编译缺AL/DX支持失败。现补AT&T端口指令明确宽度编码，真实三环GP仍待06执行。用户最新要求Shell用原有两主题图标，三份SCB从原盘逐字节恢复，@SHELL主题接线保留；Sound保留新两主题。05的新Shell画面为历史方案，06重新截图。源码/原盘/全部失败保留，后续nano/scdbg/身份/音频/SIMD/零环与截图闭环、完整兼容安全性能仍未验收。当前证据入口sandcore/docs/M9-VERIFICATION.md；下文状态为阶段沿革。

> **2026-10-05 11:55后最新接力：M9首轮整包构建通过，开始Windows QEMU调试。** `build/m9-work/build-05.log`记录make m9退出0，内核text211591B/data64B/BSS827796B、BSS末端0x1CA194，947项v5/64MiB数据盘和221份第三方源码/声明归档已实际生成。第一次低端BSS越界已改SCX暂存为懒分配64页；cron大结构赋值改有效项复制，M9 GCC使用-minline-all-stringops，不引入libc；音频token命名冲突已修。第二测试盘由M8-start源只读迁移成830项v5，双盘证据用build/m9-verify-20261005-*，失败不删除。01在客体放行前因ACL文本GA/FA、管理员LA别名比较误判停止，已改逐ACE核对SID/掩码/顺序及受保护DACL；02正在调试。尚无客体运行PASS或M9完成声明。下文“无构建成功”为阶段历史，M8a冻结/原盘保留及完整目标继续。

> **2026-10-05最新接力：M9开始统一构建。** 本地功能源码/资源/许可归档/客体夹具和Windows双盘脚本收齐到构建入口，用户已授权自动进入构建测试，无新审批。140个本地参考名有源码；原310表/本地171/网络55/平台82/用户排除2全部保留，口径问题未获新答复，最终两种统计和实际证据都要提交，不称80%行为达标。新v5输出2048项/双384bank/data769并兼容小v5和v1..v4，只迁移副本；六个直接代码组件含bzip2/LZMA原文与固定摘要归档SYS/LICENSE。现在没有构建成功或M9运行PASS；此前禁止运行段落是第一阶段沿革。后续先WSL make m9新build/m9-work，再修构建、实际native编译/Windows双盘/完整兼容安全音频性能矩阵。M8a/M8封存不重启，不覆盖原盘，不清历史。

> **2026-10-05当前接力：M9全量实现阶段，覆盖下面M8历史进行中描述。** 用户要求写齐代码/资源接线/文档后统一构建调试，目前没有M9构建、编译、工具执行或QEMU运行。身份/SandFS v5、双UART/宿主串口、Ring0/SKM、SIMD、窗口截图、音频、Shell和独立CLI源码已接，约80% BusyBox行为目标未达成，nano与scdbg已接，Shell参数/IFS/嵌入$@/<<-、分页/校验/归档/脚本/进程工具继续补齐；完整行为审阅和80%仍待达成。0x227..22F及0x237新快照/内存脚本/终端/时钟/32任务采样见SYSCALL.md；源状态见M9-IMPLEMENTATION.md。用户新规写入AGENTS第十节：仅署名/保留声明类宽松许可或更宽松的第三方代码，完整源码与版权/许可归档`/SYS/LICENSE`；清单THIRD-PARTY.md。M8a/原M8封存不重启，旧PASS不覆盖M9。

> **2026-10-04 最新状态：当前成果正式命名并发布 M8a。** 用户认可主体成果已经够用且很优秀，真彩色/组件保留；原完整M8未达成的游戏/影片及最终统一验收等部分暂时封存。本次部分收束、更名和跳过均由用户明确批准，以后未经许可不得擅自重复。发布只打包现有成果，不重启开发/优化/测试/渲染；下文旧进行中状态为历史。见[M8a发布说明](sandcore/docs/M8A-RELEASE.md)、[封存](sandcore/docs/M8-FROZEN.md)、[复盘](sandcore/docs/M8-RETROSPECTIVE.md)。

> **2026-10-04 18:15最新接力，覆盖较早运行状态**：用户允许几乎肉眼不可见差异且保持原生尺寸，旧GPU07固定512队列18:08核身份后停止，46好帧保留，新采样另批。自适应01三张真实4K试帧完成114.141/61.250/91.781秒，平均样本74.55/58.72/110.77仍慢；02新完整区间归约试帧执行。GPU几何05与已证noinline参考32768字段/独立参考/护栏/释放PASS，早期失败保留。build-29/native-apps-13真实G2 Race109005B，阴影修复后实际WHPX590×375为2.45绘制提交调用/秒、2.05合成/秒、error0/退出回收；仍不可称丝滑/60FPS。影片新整片/播放器及完整M8未完成，见MATH-FILM.md/M8-PHASE2.md。

> **2026-10-04 18:00续批**：正式自研GPU进程22200实际路径/精确创建时间再核相同，43/2520帧完整登记，第44帧继续，平均175.733秒，剩余线性约5.04天（非交付承诺）。Race04定位17次近地平线阴影越界；保守物件/太阳线段粗筛修复后05三轮旧新全客户像素与真实旧图相等、错误零/回收PASS，中位draw314/242 PIT，非最终FPS。影片新粗筛01 GPU几何对照失败，冻源07未改；须定位独立参考/求交偏差再接新算法，当前出帧不能当最终母版验收。完整M8仍继续。

> **2026-10-04 15:59最新接力**：用户改为自写数学渲染器/自选API，实际OpenCL GPU工程已实现。正式math-master-20261004-07于15:51独立后台PID22200以4K/512样本/all启动，15:59核实2/2520帧完整校验，第3帧继续；前两帧172.703/183.609秒。旧Blender队列与渲染进程已按身份核对后停止，源码/帧/日志保留，旧状态JSON仅历史。新合同/冻源/证据见sandcore/docs/MATH-FILM.md，影片/播放器/M8未完成。游戏数学库03/04通过，但race-equivalence-03实际OLD渲染累计17个此前reset掩盖的几何错误，须修复，不称画面/性能PASS。

> 以下14:31/15:20段落保留为当时观察；后台实例的当前身份以上述最新记录为准。

> **2026-10-04 14:31当前补充**：整个M8第二阶段持续。WHPX/TCG六模式已验证；同一World持续输入约1.25→11.9绘制提交/秒，非最终60FPS。native-09 G2/G3均61e48dfa，SCWIDE完整数学通过。scene-exact-03两代各1024射线/22528值、30生命周期、实际4096物体/护栏/回收PASS；scene-equivalence-12旧默认六图全部相等。Race已接精确几何，build-27/native-apps-12真实G2编译、640实际运行/回收通过，但可玩速度/现代画质未达标。4K终幕2221已完成4266.282秒，PNG/构图已查；14:31整片后台队列实际--frames all启动，已校验1/2520。完整影片/播放器/M8未完成，证据与限制见M8-PHASE2.md/FILM.md。

> **2026-10-04 15:20续批**：build-28，SCWIDE.inc e52a7384/SCENE.inc 243fd70a；宽向量融合与精确重心必要条件提前排除已由scwide-03/scene-exact-04两代原生完整值、边界、4096容量及严格回收验证。Race源码未改，三赛道590×375固定真实渲染对照进行中；赛车夹具01重复include失败已保留，02路径错误未启动QEMU，03修正测试注入/路径后实际执行。新算法不能沿用native-apps-12证明当前游戏性能。整片队列20672与Blender10432在15:12实际存活，1/2520完整校验，电影/播放/M8未完成。

> **2026-10-03最新授权：整个M8第二阶段已开始。** 用户明确“继续 第二阶段”，并补充“我的意思是整个m8的第二阶段开始”。统一进行完整构建、系统内编译、双盘无头QEMU测试、性能优化、失败修复与重测；优先定位legacy游戏和快速鼠标移动的CPU占用/卡慢。历史证据只对应其冻结输入，当前完整M8仍未验收。

> **给接手的 agent**：进目录先读这份 + [AGENTS.md](AGENTS.md)，再动手。
> 更新于2026-10-03：M7已验收；M8和游戏第一阶段源码已收齐。用户现明确批准整个M8第二阶段，开始完整构建/原生编译/QEMU测试、性能优化与修复。作者/Owner：**mio**。
> **接力先看**：最新整个M8第二阶段输入冻结于sandcore/build/m8-phase2-resume-20261003-01；前次524项快照是历史输入。最新授权覆盖游戏与整个M8；各PASS仍只对应其精确源码/内核/产物，不能代替完整M8验收。
> 当前实现表sandcore/docs/M8-IMPLEMENTATION.md；完整目标M8_GOAL.md，顶级视觉要求不缩减。
> 最新第二阶段证据/CPU修复/编译器与渲染库绑定见sandcore/docs/M8-PHASE2.md；开发盘已重建，完整M8尚未验收。
> **重要长期规则**：用户2026-10-03要求本项目代码性能优秀、效率高；复杂热路不能保留明显重复/全量/忙轮询。根AGENTS.md§六及PERFORMANCE.md定义同画质实测、正确失效和兼容门槛，接手必须执行。

---

## 0. 一分钟了解这个项目

- **是什么**：32 位 x86 裸机图形操作系统，从引导扇区到窗口系统**全部手写**
  （不用 GRUB / FAT，不用 ELF 作为系统可执行格式），在 QEMU 里运行；字体统一采用用户规定的凤凰点阵体。
- **在哪**：项目根 = `C:\Users\Administrator\Desktop\projectos`，系统源码在 `sandcore/`，
  规范文档在 `sandcore/docs/`，硬约束在根目录 `AGENTS.md`。
- **署名与魔数**：作者署名 **mio**；所有自定义格式魔数带 **MIO** 尾缀：
  可执行 `SCX1MIO`、文件系统 `SANDFSMIO`、字体 `SCF1MIO`。
- **已达成**：M0 点亮 → M1 中断/键盘/控制台 → M2 内存/Shell → M3 中文/双缓冲 →
  M4 鼠标/窗口 → M5 文件系统 + ring3 用户态 + 系统调用（`run hello` 实证全链路）。
  **M6a 清单已按序落实，M6 原生汇编器和应用矩阵已上机实测；见 §2/§3。**

## 1. 环境与铁律（违反 = 白干）

| 事项 | 规矩 |
|---|---|
| QEMU | 用 **Windows 版** `C:\Program Files\qemu`（用户要看窗口） |
| 挂盘 | **双盘**：`sandcore.img` 挂软盘 **必须 `if=floppy`**（铁律，见 docs/BOOT.md §0）；`sanddata.img` 挂 **IDE**（SandFS 数据盘，ATA 驱动够不着软盘） |
| 构建 | Windows：`cd sandcore && build.bat`；或 WSL：`wsl -e bash -c "cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore && make"`（WSL 有正统 ELF 工具链 + nasm） |
| 字体 | `kernel/font16.txt` **归用户 mio 所有**（统一凤凰点阵体/CC0，原生 16px 提取）。补字只能用 `python tools/import_font.py 汉字`，**不要手画字形**；改完 `python tools/font_preview.py` 预览。字体已上盘为 `sys/font.scf`，运行时加载，内置编译表只是兜底 |
| 验证文化 | 每步改动用**无头 QEMU + 截图**验证（`tools/keys.py` 键盘 / `tools/mouse.py` 鼠标 / `tools/snap.py` 截图）。**没有截图不得宣布完成**（AGENTS.md §二）。图形/审美类效果用户会亲自测试，agent 负责自动化验证 |
| 内核纪律 | 禁浮点、禁 libc；全中文注释讲"为什么"；`.bat` 纯 ASCII；调色板新色先入 `palette.h` 槽位表（配 docs/GFX.md） |
| WSL sudo | 如需装包向用户要密码（用户此前提供过，不要写入文件） |

## 2. 当前状态（实测版，2026-10-02）

### M7 接力状态（2026-10-02 用户验收通过）

本小节是已验收 M7 包的快照。M8 工作区已继续构建，当前源码/开发盘不能再直接套用
下列 M7 内核字节数、51 文件或 G2 原生发布结论；M7 ZIP 保持原样，M8 状态见下节。

- 引导容量同步到 256 扇区/128KB，当前内核原始 52164B，补齐为 102 扇区；BSS 移至 1MB 保留区，末端 0x1EF8E0。
- 三环未处理异常暂停并弹诊断卡片，关闭才结束任务；注册回调用独立异常栈恢复。
- 真实 INT3/TF 调试、整帧提交/连续键/让出、SandFS v4 子目录与文件操作均已上机实测。
- CORE.SKM 已从 SYS/CORE 自动加载并实际绘制壁纸；SYS/MOD 为用户零环模块目录。
- SYS/CORE 由内核在三环写入/删除/重命名入口强制保护，别名与祖先移动无法绕过。
- 底座十图、原生编译二十图、图形矩阵三十五图、模块两图均通过；SCAPI.H 完全大写。SCCC 18 组语义、四类失败诊断、G1/G2/G3 逐字节收敛均通过。
- G2 已实际生成 Files/Studio/Debug/Lumen/Race/World/Probe 并安装到默认盘；八份 .map 同盘。默认盘 51 文件，来源见 build/m7-native-runtime.json。
- 图形 IDE 任意位置编辑/C 与 ASM 编译、真实断点/单步/暂停/内存、赛车输入、沙盒破坏/建造/存档重读、42 秒六幕短片暂停/重播均通过。
- 统一凤凰 ASCII/Han，GLYPH16/UTF8TEXT/FONTINFO 可供三环使用；36 字逐行对照、缺字/错误编码/地址/owner 与坏 SCF 同源兜底通过。
- 正式原生盘已完成 M6 回归 35 图；原生底座与全部应用退出检查资源计数。总计 102 张本轮成功截图，各目录 results.json。
- 联合指南 docs/TESTING.md，交付清单 docs/PACKAGE.md；验收包 build/SandCore-M7-2026-10-02.zip。M7 用户已明确验收通过；M6 的独立用户验收未另行确认。
- M7 短片中文片名通过 import_font.py 补入九字，当前 36 字；原 27 字点阵逐项不变，哈希见 GFX.md。

### M8 当前接力状态（2026-10-02，实施中）

- **2026-10-03接力优先项**：用户要求十分丝滑、Monitor CPU分析与磁盘/
  卷命令、API/ABI只增不减。长期约束已写AGENTS.md§四及M8_GOAL.md。
  M7的40个头文件入口/29个宏/原调用号/task_t字段静态兼容通过；
  原GETKEY/POINTER忽略寄存器与消费行为也在实际原生探针通过。
  peek使用新0x77/78，CPU/存储是0x79/7A独立快照，不扩旧MONITOR。
- CPU/卷信息和512项目录已有本阶段PASS：G2实际生成Monitor/探针，
  新80扇区512项与历史32扇区192项均通过真实填满/拒绝/同盘冷启动/
  list/整组rename/严格临时页回收。证据sandcore/build/m8-system/
  results.json，CPU.md/STORAGE.md/FS.md公开精度和实际整盘卷边界。
- 新Shell与19个独立CLI已由真实G2编译运行；实际cp安装、输出授权/
  PID复用/异常卡/父终端取消130/用户名环境同盘冷启动/终端重排与
  严格物理页回收通过，build/m8-userspace/results.json。Settings六页
  草稿/通用UTF-8编辑/新0x7B纯配置检查，本轮G2原生/鼠标/坏值/
  主题壁纸/菜单快捷方式/640及200%预览/VGA/冷启动/回收通过，
  build/m8-settings/results.json，见CONFIG.md。全显示矩阵、
  全组件、全部旧游戏写实高分辨率真彩重做、真实实时真彩光追与
  完整交付未完成。用户2026-10-03再次明确游戏和动画的真实性要求。
- 延迟已取得对照：1024鼠标约303ms→31ms、Settings548ms→58ms，
  首轮拖动303ms→87ms。1080拖动仍需优化；背景预采样缓存与不透明
  窗口去重复基底已实测；1080拖动合成由281tick/29帧降到97/27帧，
  宿主该轮95.51ms/P95 124.18ms，仍未达全部丝滑。RGB三轮/VGA/
  32MB部分OOM回归通过，build/m8-system/rgb-regression。上述
  是含HMP观测开销的本机TCG结果，不能称全部模式/全部应用已丝滑。

- **2026-10-03整帧阶段核**：FRAME/FRAME32固定可信当前任务/CR3后
  STI复制，返回前CLI解除；旧接口/结构不变。真实同一G2探针
  ARGB/索引复制内PIT110/12，输入/完整帧/正常及活动关闭回收
  通过。四套RGB/VGA/32MB、CPU新512旧192、Settings、19CLI
  同核回归PASS，build/m8-frame-irq/regressions.json。当前核
  120660B/236扇区、BSS0x1F01AC；静态兼容40入口/29宏与字体
  不变通过。旧设置核已冻结M8-settings-evidence.zip（SHA256
  d663e572baffb835b02b99ffa95e4545eefc1cacb7de6ec2d4905e638fd00e71）。
  全组件逐项表M8-COMPONENTS.md。该阶段已冻结325项源码/双盘/
  原生证据为M8-frame-irq-evidence.zip，整包摘要
  25ad98e580302a1c6527b4aff5669e7b76bdff133e6220a72af7610cfebd3235。

- **2026-10-03上一冻结核**：任务0无工作时沿原陷入出口把空量子交回
  本tick未YIELD的忙任务，全已让出时仍HLT；task_t/SCAPI不变。
  同一真实G2 SCX实测用户样本48.5%→97%、每200tick计算批数
  中位361.19→990，旧YIELD仍93%空闲、双忙任务/键鼠/关闭全页
  回收通过，build/m8-idle-yield/optimized/results.json。当前核
  120980B/237扇区、BSS0x1F01EC，静态/40入口兼容通过；RGB/
  VGA/32MB、CPU新旧盘、Settings/19CLI四套新核回归已PASS，
  m8-idle-yield/regressions.json；大帧PIT168/42、输入/字节/活动
  关闭回收与1024/1080延迟补测PASS。1080拖动本轮106.54ms/
  P95 150.85ms、120tick/27帧，含HMP观测下限46.31ms，仍须局部
  合成，不把计算提升当作GUI全流畅。
  该冻结阶段尚无损伤合成；当前局部合成见下一项。显式等待、
  全部组件/图片/写实真彩游戏/实时光追及整个
  M8仍未完成，不能用计算吞吐证明全部GUI已丝滑。

- **当前正在验证的新改动**：局部合成首版已构建，原始124184B/
  243扇区、BSS0x1F022C。统一后台写视口覆盖点/行/背景拷贝，
  FRAME32记录首末改变行、拖动并集含投影、时钟只损伤任务栏，
  未知范围及VGA仍完整合成。真实G2 damageprobe比较局部与相同
  几何/画布的完整重画PCI像素；首组1024/100% Aurora的两层窗口/
  alpha/圆角/拖动/光标等价与全页回收PASS，静态/40入口兼容也过。
  当前m8-damage/results.json；四套相关回归/大帧/1024与1080延迟
  已PASS，不沿用上一237扇区核证据。大帧复制内PIT788/36，输入/
  字节/活动关闭回收通过；同一HOST工具1080拖动95.14ms/P95
  126.55ms、47tick/27帧，观测下限48.82ms，仍不能称全GUI丝滑。
  十五模式缩放Aurora及三组Classic的同一G2合成等价矩阵全部PASS，
  鼠标保持按下时先比较，避免释放后的全屏重画掩盖残影。18组
  601次局部合成/144张成功截图；Shell基线等待真实提示符，补跑
  实测4页终端初始化差值，终值严格相等，首轮失败资料保留history。
  下一批Canvas合同docs/CANVAS.md；全部组件布局/PNG-JPG-WebP/
  写实真彩游戏/实时光追/最终M8未完成。上一完整通过阶段已
  冻结M8-idle-yield-evidence.zip，
  360项整包9ffdc24a9da74c81cb32b205be166fed1b1a9d27581f30d68a40a28c1279a23a。

- 用户已同意原生五档高分辨率（最高1080p）、100%/150%/200%缩放和 VGA 回退；
  程序先复制资源再通过配置注册。另授权窗口调整大小/最小化、彩色图标/右键菜单、
  三环 Settings 及 Lens/Canvas/Monitor 三个新程序、原工具鼠标适配。
- 已保存 `sandcore/build/M8-start-*` 源码/双盘基线。显示、窗口、配置和新工具底座
  已实施；`build/m8/foundation.json` 记录 std 1024×768/cirrus 320×200 与 BSS 边界
  实测通过。其它模式/缩放/持久化/鼠标矩阵/原生发布仍待完成，不宣布 M8 完成。
- 用户认为现有图标不好看，明确要求先比较多套设计语言，再依选择重写全部组件。
  已提供砂岩、深海、纸墨、极光、工程舱、软糖六套交互原型，已选白色极光；
  `sandcore/docs/M8-DESIGN.md` 记录原型路径、截图、字体来源和统一重写范围。
- 设计候选的浏览器截图在 `build/m8-design/`，不冒充系统 QEMU 截图。后续选型后
  先登记 GFX 颜色/组件规范，再改系统源码并完成双盘无头 QEMU 验证。
- M8 进一步授权真实 RGB/透明合成、开放主题配置/API、Classic 经典主题、SCX
  标志位内置图标与可指定快捷方式图标；Lens/壁纸必须支持 SCB/BMP/PNG/JPG/WebP。
  版本主题 Welcome to true color。完整范围见 M8.md，不能缩减为槽位转32bpp。
- 最终Logo选现代平面AURORA-DUNE-V2、现代轻立体AURORA-SATIN-V3、仿古CLASSIC-FALSE-V3。
  数学SVG Logo稿被否定，不能安装为默认；选择与提示在assets/wallpapers/selection.json。
  仿古已实际处理CLASSIC-LOW320.png（320×180/P模式/15使用色），6倍最近邻预览。
  用户要求Classic控件/窗口/菜单全套真正复古；细沙摄影TIDAL-FINE-V4为新候选。
- 真RGB后台/ARGB窗口/64MB私有图形堆已实施；主题底座本轮内核原始100304B，盘内补齐100352B/196扇区，BSS末端0x001F7D40。
  高分辨率合成不再整帧cli，IRQ继续记录键鼠和计时、任务切换暂缓；队列保留点击边沿。
  真彩色底座已通过std三轮/VGA回退/32MB OOM/长参数/资源回收，实际RGB区56977色；
  用户帧/画布/后台/PCI显存逐层一致。已验收M7 G2实际生成新探针也通过。
  初版证据归档build/M8-truecolor-foundation-evidence.zip，主题后的当前回归继续。
- 新增THEMEINFO/LOAD/PATH与STHEME1MIO配置，详细协议sandcore/docs/THEME.md；
  白色Aurora/Classic窗口、菜单、任务栏与NUI ARGB控件已实施。G2实际生成探针和Settings，
  配置拒绝/有效快照、鼠标切两主题和同盘冷启动恢复均通过，证据build/m8-theme/results.json。
  完整M8仍未验收。
  图片解码/壁纸和高像素图标资源、SCX内置图标、旧工具全适配/显示完整矩阵继续。
- 用户已关闭原来正在运行的 M7 QEMU，可继续正常构建；不结束任何无关虚拟机。
- 以下 M6 数据是已交付验收包的历史快照，当前运行盘已随 M7 升级，不可混用尺寸/格式。

- **M0–M5 全部完成并经用户验收**；M6a/M6 已构建与自动化验证，**用户界面验收尚未确认**，不能把两种状态混写。
- `build.bat` 实测走 WSL ELF 工具链；引导软盘 1.44MB、IDE 数据盘 8MB，预装 20 个文件。
  内核补齐后 63 扇区，`KERNEL_SECTS=64` 未改；BSS 末端 `0x8b100`，链接断言保护 `0x90000` 引导栈。
- M6a：磁盘字体 27 字、文件图标、无自动 Shell、ring3 分页并发、独占画布、稳定窗口句柄、聚焦输入、退出回收均有行为断言及截图。
- M6：SandAsm 在系统内读 `.asm`、生成 SCX 并实际运行；Files、Notes、Palette、Calculator、Mines 均为普通 ring3 应用。
  SandFS v3 扩至 96 项；新增 GETARGS/WALLPAPER/WININFO，EXEC 带参数；夜色壁纸断言重启恢复。
- 证据：`sandcore/build/m6a/` 与 `sandcore/build/m6/` 的 PNG、results.json、qemu-command.json；验收说明 `docs/M6A.md`、`docs/M6.md`。
- 用户字库未改，SHA256：`1ef3876e7adbf0f42dc432dbdf21c927282a7e28bb076613c6cb50fbf0de9b55`。

## 3. M6a 原顺序清单（十项均已落实）

保留原编号作为交接证据，避免后人误以为这些还是待办。

1. **构建依赖**：旧 `build/files/hello.scx` 已替换为 `fs/bin/shell.scx` 与 `fs/apps/hello.scx`，全构建通过。
2. **字体路径**：Makefile 独立生成 `build/fs/sys/font.scf`，mkfs 只遍历资源树；同时修复 SCF 魔数少一字节导致的头错位。
3. **文件快捷方式**：Shell 的三行 .lnk 与 SCF 图标已上盘，随后扩至 Files/Notes/Palette 四个图标；路径解析、CRLF 与资源长度均有边界检查。
4. **启动器双盘**：Makefile/run.bat/run.sh 均为软盘引导 + IDE 数据盘，WSL run 仍调用 Windows QEMU。
5. **画布模型**：SYS_TXT/FILL 写本人窗口独占 canvas，合成时统一 blit；置顶/关闭只重排窗口，句柄与画布地址保持关联。
6. **SYS_PUTS**：路由到调用者最近创建的窗口，按窗口维护文字游标，支持折行、滚动、整格退格与跨折行退格；旧 term 不参与内核链接。
7. **退出回收**：EXIT/红叉关闭 owner 全部窗口并结束任务，后续调度安全释放私有页/页表/内核栈；10 轮启动退出物理空闲页数不变。
8. **联调**：Shell/hello 实际运行；修复源码误封 SCX、CR0/CR3 写寄存器方向、ring3 数据段与用户地址转换。
9. **验证链**：双盘→开机→桌面→图标→Shell→并发 hello→回收→真实除零红屏均有截图；输入 owner、页表权限、画布摘要也做内存断言。
10. **验收与文档**：M6A.md/M6.md、README/ROADMAP/TESTING 和 ABI/格式/子系统文档已同步，当前仅待用户界面验收。

### 3.1 接下来从哪里接手

- 先检查 `docs/M6.md` 的截图与边界，再依据用户界面反馈打磨；不要重复执行已经修复的十项清单。
- Notes 当前仅 ASCII 末尾编辑；SandAsm 是明示的 NASM 子集，不含宏/完整寻址/自编译。
  M7 的 SCCC（s3c）和单头文件内联汇编 **SCAPI.H（完全大写）** 已系统内三代收敛并发布原生产物，详见 docs/M7.md/C.md。
- **内核引导容量接近上限**：后续扩内核前检查 BOOT.md 同步清单；不要只改打包器上限。优先把业务放用户态。
- 数据盘在运行时保存文件；M8 mkfs 对合法 v4 盘合并发布资源，保留 HOME/DESK、
  用户配置与额外文件，显式 --factory 才建立出厂盘。构建前仍保存整盘基线，详见 FS.md。
- 本次接手基线为 `sandcore/build/m6a-source-baseline.zip`（第 1 项 Makefile 修复之后保存）；验收包另含当前源码、可运行双盘和全部证据。

## 4. M6 / M7 规格（用户原意，执行时别跑偏）

### M6「可用化 + 桌面化」（历史规格，已自动化通过）
1. **目录规整化与 Shell 改造**：
   (a) 目录规整化——`/sys` 系统文件、`/bin` 可直接执行的命令（现为文件名路径前缀的
   伪目录：`sys/`、`bin/`、`apps/`、`desk/`）；(b) **开机不再弹 Shell 窗**，Shell 打包成
   普通程序；(c) 桌面**快捷方式 + 图标**：图标不写死、走文件系统读取；其他程序也可有
   图标，**图标信息可以写在可执行文件里**（如 SCX 头扩展字段）；(d) Shell 提示符要
   干净——程序运行完提示符不应残留旧痕迹。
2. **自举第一步：汇编器**：写成 **ring3 程序放进文件系统**；CLI 或 GUI 均可
   （CLI：输入源文件 → 输出 SCX 可执行；GUI：打开/编辑/汇编）。有了它就能在沙核里
   造沙核程序。
3. **用户态组件与桌面打磨**：记事本、调色板、**文件管理器**（重要）、壁纸可修改、
   计算器、扫雷等——**不限于这些，自由发挥多加**，逐步打磨。
4. **系统调用完善与全文档化**：为 M7 准备，功能要全，全部落 docs/SYSCALL.md。

### M7 规格（已按用户授权落实并自动化验证）
- 写自己的 C 编译器 **SCCC（命令 `s3c`）**，系统内组件；
- 只有一个头文件 `SCAPI.H`（文件名完全大写）：用**内联汇编**把函数定义为系统调用的封装；
- 基本 C 特性与宏定义必须支持；SCCC 要在系统内编译自身，再编译 M7 应用。
- 三环异常按处理器注册情况处理；未处理异常暂停并弹窗，关闭弹窗才杀掉程序。
- 逐帧高级自定义图形、图形 IDE/文件管理器/调试器、赛车、原创沙盒与 3D 动画。
- M7 完成时同步 M6/M7 联合自测指南；M6 用户界面验收尚未确认。
- 很多内核组件**下沉为用户态 ring3 程序**；系统更可自定义、更用户态。

## 5. 推荐阅读顺序（接手后按序读）

1. `AGENTS.md` —— 铁律（审美约束/里程碑流程/环境约定）
2. 本文件
3. `sandcore/README.md` —— 项目地图与里程碑表
4. `sandcore/docs/BOOT.md` —— 启动契约（§0 if=floppy 铁律、E820 契约、逐扇读盘）
5. `sandcore/docs/BUILD.md` —— 双工具链与全部踩坑实录
6. `sandcore/docs/SYSCALL.md` —— **int 0x7C API 全表（v0.7，ABI 权威）**
7. `sandcore/docs/FS.md` —— SandFS / SCF1MIO / SCX1MIO 格式（魔数 MIO）
8. `sandcore/docs/WM.md` + `INTR.md` + `MEM.md` + `GFX.md` —— 各子系统规范
   （GFX.md 含调色板槽位表与字体工作流）
9. 源码（建议顺序）：`boot/boot.asm` → `kernel/entry.asm` → `kernel/main.c` →
   `kernel/idt.asm` + `interrupts.c` → `paging.c` + `task.c` → `wm.c` →
   `fs.c` + `ata.c` → `term.c` / `gfx.c`
10. `sandcore/docs/ROADMAP.md`、`TESTING.md`、`ASM.md`、`APPS.md`、`M6A.md`、`M6.md` —— 当前路线、原生汇编器、应用边界和验收证据。
11. `sandcore/docs/M8.md`、`M8-DESIGN.md` —— M8 范围、当前证据与视觉选型，收到用户选择后继续。
12. `sandcore/user/SCAPI.H` → `start.asm` → `user/*.c` / `*.asm` + `sandcore/tools/*.py`
    （mkimg/mkfs/mkscx/mkscf/mkfont/import_font/font_preview/keys/mouse/snap/verify_m6/verify_apps）。
    本次新增代码使用详细中文注释，重点是两遍地址、容量边界、调度/画布归属与生命周期。

## 6. 调试文化（前人踩坑索引，动手前扫一眼）

| 坑 | 文档位置 |
|---|---|
| QEMU 缺省把 raw 镜像挂成 IDE 硬盘 → CHS 读错磁头 → 内核后半全零 | docs/BOOT.md §0 |
| 跨磁道成块读不可靠 → 一次一扇；`pop cx;inc cx` 会把 CH(柱面) 冲成 0 | docs/BOOT.md §2、boot.asm 注释 |
| E820 条目 20B 本就是 24B 布局前缀，"好心"重排会错位 | docs/BOOT.md §4.5 |
| Windows PE 工具链四连坑（下划线/段稀疏/.reloc/警告）；PowerShell 无 uname 导致环境误判 | docs/BUILD.md |
| TSS 描述符字节摆位反了 / iretd 进 ring3 缺 RPL3 / SCX 入口把头长算两次 | docs/ROADMAP v0.6、task.c 注释 |
| 控制台三连坑：滚动源偏移只挪 1px、光标整格擦留洞、空格字形把字符码当位图 | docs/GFX.md §3.6 |
| 方法论：`pmemsave` 显存/内存逐字节对质、`-d int` 中断日志、`-no-reboot`、
  HMP `sendkey`/QMP 鼠标注入、gdbstub（Windows gdb 可用） | 各里程碑验收记录 |

## 7. 工具速查

```sh
cd sandcore
build.bat                 # Windows 一键构建（含字形表编译）
python tools\font_preview.py      # 不开机看全部中文字形
python tools\import_font.py 汉字  # 按统一字体策略补字
# 无头验证（另一个终端）:
wsl -e bash -c "cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore && make run-headless"
python tools\keys.py h e l p ret          # 注入按键
python tools\mouse.py move -100 20 / down / up / click   # 注入鼠标（y 已按屏幕方向）
python tools\snap.py build\shot.png       # 截图（验收证据）
# 自动验收（不要同时运行另一台占用 4444/4445 的 QEMU）：
python tools\verify_m6.py all             # 当前 M6 基础回归，三环异常为暂停卡片
python tools\verify_apps.py               # M6 汇编器/应用/错误路径/壁纸重启
```

—— 祝顺利。改完记得回来更新这份文件的 §2/§3（接力棒交给下一位时同样适用）。

## 8. 修订记录

2026-10-04 15:59：自写OpenCL宿主GPU影片正式4K/512/all真实后台，前两帧完整校验；旧Blender核身份后停止且历史保留。Race全客户夹具揭示17次旧几何错误，待定位；完整影片/播放器/游戏可玩与M8未完成。

| 日期 | 变更 |
|---|---|
| 2026-10-02 | 接手原清单按序完成并上机实测；M6 原生汇编器/应用矩阵/ABI/格式与证据同步，保留 M7 方向与用户界面待验收状态 |
| 2026-10-02 | M7 用户授权开工，扩展需求落 M7.md；公共头文件统一 SCAPI.H，大写名称/宏/基本 C 特性与内部自编译明确为验收项 |

| 2026-10-02 | SCCC 三代系统内编译与 18 组语义通过；完整命令截图和吞键修复同步，后续继续图形应用 |

2026-10-02：M7 新增统一凤凰字体与三环 UTF-8/Unicode API；汉字只取 font16.txt，ASCII 同源提取，接口/编码/上机验证流程见 docs/FONT.md（sandcore 内为 FONT.md）。

2026-10-02：M7 实现、原生发布、102 张成功证据、M6 联合回归与验收包同步；后续从用户界面反馈接手，不重复覆盖历史 M6 包。

2026-10-02：用户明确验收 M7 总体达成预期；M8 已同意原生高分辨率/VGA 回退/三档缩放和配置注册程序，完整规格见 M8.md。历史验收包字节保持原样。

2026-10-02：M8 底座与开发盘状态独立于已验收 M7 包；用户要求先选设计语言，六套交互原型已提供，统一组件视觉重写等待选择，证据与接力位置同步。

2026-10-02：选型已定白色极光，新增 Classic/真彩色/主题API/内置图标/多格式图片，壁纸按摄影与两套Logo方向重做；M8仍实施中。

2026-10-02：三款Logo最终选择、Classic真实低像素处理/经典组件与细沙摄影V4同步。
RGB/高端图形堆与不中断IRQ的合成保护已构建；本轮实测及完整M8矩阵继续。

2026-10-02：M8完整目标见M8_GOAL.md：全部组件/LEGACY/极高现代游戏/真实光追动画、新Shell/用户名/环境与Settings全配置。默认目录统一/BIN和/APPS。SCB2/壁纸/高清透明图标/64MB数据盘/SCX bit1/LEGACY已构建，主题及冷启动回归通过，图片与原生图标矩阵继续。当前开发盘非完整M8交付盘。

2026-10-03：同步最新CPU/卷/目录/兼容阶段PASS与性能对照，Shell19个
独立CLI原生/取消/冷启动验证已通过；Settings六页与配置纯检查推进，
全M8目标保持实施中，不覆盖M7包。

2026-10-03：Settings六页、纯配置检查、完整UTF-8编辑和真实
G2/鼠标/坏值/菜单壁纸/640×480及200%预览/VGA/同盘冷启动阶段
PASS；修复按钮帧先取键丢首字的焦点顺序。用户明确所有旧游戏写实/
高分辨率/真彩与动画真实实时真彩光追。大帧输入对照与整体M8继续。

2026-10-03：当前局部合成18组合、相关四套/大帧/延迟全部PASS，601次局部合成；基线修订及失败历史保留，正式阶段包待封存。下一批Canvas与共用路径输入框，整体M8及全部极高写实游戏/真实实时光追仍继续。

2026-10-03：局部合成阶段包M8-damage-evidence.zip已实际封存并重读CRC/560项SHA，整包15d34f56a440bef7905eeaa673d20b96a5c9048f4fb6556d1a91d70e4a9ff390。包含243扇区精确核/源码/默认HOST双盘、真实G2原生产物、18组合和四套/大帧/延迟成功证据及独立测试盘；旧验证器快照保存，失败历史不计入成功。完整M8与下一批Canvas/NUI适配继续。

2026-10-03：局部合成560项冻结包已完成；下一批Canvas/NUI代码改为SCB2、SCB1兼容/失败回滚/响应画纸和UTF-8路径紧凑输入框。当前尚未构建/实际验证；SCAPI.H和字体未改，旧包为精确通过参考。

2026-10-03：Canvas/NUI已构建，真实G2 Canvas/Lens/路径探针与
1024/100%功能首组PASS，详见docs/CANVAS.md及build/m8-canvas/
results.json：完整作品/Undo回滚、SCB2保存/Lens读回、SCB1、中文/
退格与严格回收。十九显示组合/窗口操作和共用Settings回归继续；
当前243扇区内核/SCAPI/字体未改，不能宣称完整M8通过。

2026-10-03：Canvas矩阵640/150%定位NUI余数列漏铺，所有工具共用
整页底图已改按实际物理尺寸覆盖；旧功能PASS/640100窗口PASS及
640150失败原证据移到m8-canvas-history/before-physical-remainder-fix。
新源重新构建/G2/功能/十九组合/Settings回归进行中，不沿用旧NUI证据。

2026-10-03：Canvas/NUI本轮重建，真实历史G2生成Canvas b826bf8e、
Lens0cd0b9bf和路径探针f7894e7f；软边RGB/完整Undo、SCB2精确
保存与读回、旧SCB1、坏文件/取消完整回滚、核心保护、中文路径
和严格回收已重新PASS。NUI32dc1ec5修复物理余数行列、紧凑路径
一条文件行；Canvas按下沿绘首点及极小窗口放大已接线。十九组合
继续实际验证，共享Settings将重新G2回归；SCAPI/字体与243扇区
局部合成核不变。完整M8、全部组件/写实真彩游戏/实时光追继续。

2026-10-03：当前同一b826bf8e Canvas完成十九显示/缩放/主题组合，
241张成功截图；真实窗口/极小放大/完整客户帧/余数边界/末页与
严格回收PASS。NUI32dc1ec5对应的G2 Settings629496f3六页完整
回归、640200预览/回退、冷启动及VGA通过。动作帧首字专测只读
观察PID2、EIP0040A901（draw_paper）、ui_action=1/ui_modal=0、
窗口队首104(h)，随后指定SCB2完整读取和回收PASS；前三种验证
设置失败保留历史，未误算应用验收。填色成本对照正在运行；
完整M8和后续Files/全组件/写实真彩游戏/实时光追目标仍未完成。

2026-10-03：NUI32dc1ec5/Canvas b826bf8e阶段核心、十九显示组合
（241成功图）、新G2 Settings629496f3完整既有回归及只读已排队
首字专测全部PASS。同一G2/核/974×663全客户区填色+FRAME32
交替三轮，标准化每200tick帧数中位45.3202→48，吞吐1.0591倍；
每轮完整像素与页回收一致。该合成负载收益约5.9%，不代表所有
GUI60Hz。核/SCAPI/字体均不变，当前准备独立冻结阶段包，随后
Files列表/浮层/路径身份与全组件适配继续，完整M8未完成。

2026-10-03：Canvas/NUI阶段包M8-canvas-evidence.zip已实际冻结并
重读CRC/627项SHA，整包a1750271f80c01631693c5ea54b6342bba2462bd56504fcff226555b4dae7d87。
保留243扇区精确核/当前源码/HOST开发双盘、真实G2 Canvas/Lens/
路径/Settings、十九布局/241图、已排队首字及三轮成本成功证据与
独立盘。它是Canvas阶段验收包，完整M8/所有组件/写实真彩游戏/
实时光追未完成；随后Files新代码与原生验证继续，后续开发盘和
源码不能冒充此包精确输入。

2026-10-03：Files新布局由真实历史G2生成be06508c，旧7944c240核
1024/100%核心PASS，含78项直接子项/中文真名/鼠标分页与菜单、
文件修改/取消/超长完整拒绝/核心保护、同批G2 Studio/Lens/独立
SCX/Debug实际分派和严格回收。十九布局第四例640200失败，独立
只读复现确认右下抓取区原地点击缩窗；旧核心/前三组PASS和失败
输入保留m8-files-history/before-resize-anchor-fix。共用拖框已
改为保留抓取偏移，当前新核7a03638a/124184B/243扇区、BSS
0x1F022C，静态/40入口29宏兼容及字体不变通过；新核核心与
十九组合重测中，不能沿用旧核PASS。Lens下一批合同LENS.md
已登记，完整M8/全部组件/写实游戏/实时真彩光追继续。

2026-10-03：修复拖框抓取偏移后的7a03638a核，真实G2 Files
be06508c核心及十九显示组合已重新全部PASS，222张矩阵成功图。
覆盖十五Aurora/三Classic/VGA、整幅提交帧/物理边界/末页护栏、
目录分页/浮层不穿透/菜单重排、鼠标窗口操作/极小放大、原地
点击几何不变/实际位移与尺寸增量一致、严格页回收和零溢出。
512项GUI专测及RGB/CPU/Settings/19CLI同核四套回归继续，未
沿用旧核PASS；详细合同及手工步骤FILES.md/TESTING.md。Lens
完整原图高端缓存/过滤Fit/失败回滚/物理1px平移草稿已作宿主
无浮点编译检查，尚未激活默认源码或宣称原生通过。全部M8继续。

2026-10-03：同一真实G2 Files be06508c的512直接子项容量专测
已PASS，四张成功图。满512条底层记录先投影511行，实际删除
重复SYS来源再在根下创建才得到真实512行；鼠标到索引511、
删除取消/确认/空槽复用、508份作品完整内容、满盘拒绝及严格
回收/队列零溢出全部通过，m8-files/capacity/results.json。独立
测试盘只为容量验收，默认开发盘未改。新核RGB三轮/VGA/32MB
OOM回归已PASS；CPU/存储、Settings六页、Shell19CLI继续，
完整M8、后续Lens/全部组件/写实真彩游戏与真实实时光追未完成。

2026-10-03：Files阶段在7a03638a/243扇区核完成真实G2核心、十九
组合/222成功图、512实际直接子项及空槽再用。新核串行RGB（三轮/
VGA/32MB）、CPU新旧目录盘、Settings六页/冷启动、Shell与19CLI
四套回归全部PASS；SCAPI.H/36字用户字体/旧布局保持原合同。
package_m8_files_stage.py按精确输入、原生产物、测试规则和上一
不可变Canvas包核对后封存，默认盘仍HOST开发产物，G2及各副本
盘单独标明。Lens全尺寸解码/整数双线性缓存草稿仅宿主语法检查，
未激活、未称运行通过；完整M8及其余全部组件/写实游戏/光追继续。

2026-10-03：Files阶段M8-files-evidence.zip已实际冻结并重读CRC/
653项文件SHA，整包0db50bbbd44ca4e642560b563523eaf7f1049cbcd172558570cf535a5794fc9d。
包内7a03638a核/双盘、真实G2五应用、十九组合/222图、512项真实
GUI与四套同核回归分别绑定输入；默认盘仍HOST开发盘。旧Canvas
包及Files修复前的精确失败历史保持原字节。Lens草稿验证另存，
不改变此包的已冻结源码、产物和结论，完整M8继续。

2026-10-03：Lens完整原图高端缓存/整数alpha双线性Fit/物理100%/
原子候选回滚与响应工具条已接入974ac054源码，build.bat通过。
7a03638a核不变，默认HOST数据盘现49718852；当前G2实际生成
Lens d0e41de8/map13eb42f6，m8-lens/results.json核心PASS：整图
8294400B、整幅Fit与无限精度参考、实际60×40平移画面、坏尾槽/
flags/短尾/DIB/缺文件/超长路径/取消完整回滚、透明蓝色不渗边、
细长1像素图、BMP24/32/旧SCB1与严格页回收。十九组合/150%
物理1px及后续联动独立验证继续。初轮Ctrl-A测试误用已保留完整
失败输入；真实历史打开快捷键为F1，不修改键盘或旧ABI补假结果。
PNG/JPG/WebP、全组件/写实游戏/实时光追与最终M8继续。

2026-10-03：Lens十九布局第一轮13组PASS之后，1080从极小放大
的运行中布局读取见viewport暂为0，完整失败现场EIP0040287d/
ui_physical_rgb表明正在绘制下一帧，实际照片已经正常上屏。
精确旧验证器、13组成功及1080未完成现场全部封存在
m8-lens-history/before-layout-snapshot；不修改旧结果或称19组已过。
新测试在同一暂停快照核对几何/布局与完整私有帧=画布，恢复后
最多15秒重新取完整结果，不放宽像素/布局边界、不写客体状态。
源974ac054/G2 d0e41de8/map13eb42f6/7a03638a核及49718852盘
保持原字节，重新执行全部十九组合。真实OOM/首字/Canvas与Files
联动由verify_lens_followups.py串行门控，矩阵失败不启动后续。
package_m8_lens_stage.py仅在全部精确PASS时允许封存，尚未生成
Lens阶段ZIP；Files冻结包与用户字体/API保留，完整M8继续。

2026-10-03：Lens矩阵继续，下一批Studio合同docs/STUDIO.md已登记：
响应编辑区/紧凑右键/不穿透与排队首字、128B候选完整拒绝、私有
行索引/长文档性能、编译提交版本与输出身份/PID代数防复用。
当前user/ide.c仍旧实现，合同/草稿不算已发布或原生PASS；保留
M7任意编辑/C/ASM/运行/真实调试路径，字体/API不改，全部M8继续。

2026-10-03：Studio独立草稿build/m8-next/ide.c已实现私有65536
行索引/UTF-8编辑、手动鼠标滚动、响应右键/不穿透/完整路径、
编译源码版本及输出身份、STATUS前后CPU代数防PID复用；WSL
GCC严格语法检查通过，仅为宿主证据。独立verify_studio.py已
准备实际G2编译Studio/SandAsm/Debugger及真实编辑/65535B/
CORE/C/ASM/Run/Debug验收，尚未运行。正式user/ide.c与默认盘
保留给Lens十九组合及四项联动；不宣称新版Studio已发布或PASS。

2026-10-03：Lens同一正式源/实际G2/7a03638a核/49718852盘
十九显示组合完整重测PASS，203图，6组150%逻辑取整不变的
物理1px平移与整幅Fit参考/100%原像素/完整帧/全部窗口/严格
回收/队列零溢出。门控已启动实际候选OOM、首字与Canvas/Files
联动，阶段ZIP尚未生成；压缩图像、其它组件和整个M8继续。

2026-10-03：Lens同一正式源/实际G2/7a03638a核/49718852盘
核心、十九组合203图、真实候选OOM/排队首字/Canvas与Files
四项串行联动全部PASS，正在由package_m8_lens_stage.py封存。
默认双盘仍HOST开发盘，实际G2产物/符号/测试盘另列。Studio
独立草稿bd433ea4由历史G2生成f230d4be/mapdd9fba7e，1024/100
核心11图PASS：UTF-8/索引/鼠标滚动、完整失败回滚/65535B/CORE、
内部C/ASM与生成程序Run/Debug/严格回收；正式user/ide.c尚未
替换，全矩阵/编译竞态/首字/成本继续，整个M8保持原目标。

2026-10-03：Lens阶段包已冻结M8-lens-evidence.zip，653文件/
353439474B，CRC及逐文件SHA重读PASS，整包
53ffe88731b80610615fa4fec14df1a138c0daded61ce7b6d40aae68db575970。
核心/十九组合203图/真实候选OOM/首字/Canvas与Files联动，精确
核/默认HOST双盘及实际G2产物/测试副本均分开保存；整个M8继续。
Studio草稿PID复用/动作帧首字专测实际PASS，正式源码尚未替换。
并发编辑初轮未抓到编译未完成窗口，完整失败保存于
m8-studio-history/revision-before-delayed-driver；测试驱动取完整
源快照、实际G2生成、普通YIELD延迟返回1200tick作事务资格专测，
不修改应用/任务/时钟，也不把延迟作为编译或性能结论。

2026-10-03：Studio独立草稿提交版本/输出身份专测实际PASS，
原源31414B快照由真正G2编译，普通三环事务等待1200tick才返回；
实际编辑又保存dirty0仍拒绝旧产物，切换文件读取原提交日志，
全回收/队列零溢出。十九显示组合正在执行，成本门控等待矩阵。
下一批调试器合同docs/DEBUGGER.md与build/m8-next/debugger.c
草稿同步：双面板/窄视图/一行/浮层/完整地址/身份/节流/解码
真实性，宿主严格语法PASS，仅草稿未原生运行；正式源码未替换。
整个M8的游戏/光追/压缩图像/全部组件及最终原生发布保持目标。

2026-10-03：Studio十九矩阵首轮13组PASS后在1920/100输入观察失败，
该轮完整保留，后续已被门控阻止；成本与调试器核心均以
真正报告PASS串行门控，尚不称全矩阵或原生调试器通过。
调试器草稿补齐运行寄存器LAST PAUSE、未映射页可鼠标返回、
F7/FF真实分组长度/名称；20项G2数据解码探针、真实自然退出/
PID复用、动作帧首字与十九布局验证器准备完成，宿主语法通过。
Studio阶段封存器只接受核心/竞态/十九/成本同批精确输入及全文件
CRC/SHA，根默认HOST盘与候选分开，当前尚未生成该阶段ZIP。
正式预装仍保留，整个M8游戏/压缩图像/实时光追/最新三代编译器/
所有组件及最终验收目标继续，SCAPI.H/核/用户字体不改。

2026-10-03：Studio输入观察问题已由独立真实G2只读取证确认：
完整已提交帧35行/下箭头起点802，draw生产中的rows0让逐项
读取构造y112；不是已提交窗口只有0行。原轮13PASS/失败截图/
副本盘/源码/原验证器已保存before-consistent-layout。应用源和
f230d4be产物不改；矩阵改用同次整帧/布局快照算鼠标，真实短
点击/first_line1/全部边界仍严格，重新从头十九组，成本/Debugger
串行等待。观察PASS只证明测量问题，不能作为组件或M8完成。

2026-10-03：Monitor独立草稿build/m8-next/monitor.c与公开合同
MONITOR.md已同步，响应工具条/八槽和全部卷字段分页、CPU细分/
两类历史/鼠标浮层与极小放大、有效性和32位防溢出比例已实现。
宿主严格语法PASS，尚未真实G2生成或原生验收；正式预装保留。
其原生交互/算术边界/十九组合准备中，与Studio/Debugger串行使用
独立双盘QEMU，当前核/SCAPI.H/用户字体不改；完整M8继续。

2026-10-03：Studio同一bd433ea4草稿/真正G2 f230d4be/mapdd9fba7e
在7a03638a核和49718852默认HOST输入下，重新从头十九组合全
PASS，222张矩阵成功图，零键鼠溢出与严格页回收。VGA、十五
Aurora和三Classic均检查完整私有帧=画布、物理余数/末页、UTF-8
鼠标定位、手动滚动、浮层不穿透与全部窗口操作；原失败历史保留。
成本对照已由实际PASS门控启动，尚无成本结果/Studio阶段ZIP；
正式user/ide.c和默认预装仍未替换，后续Debugger/Monitor及全M8
继续，不能将十九布局PASS扩写成全部用户空间或最终发布。

2026-10-03：Studio同一G2/核的65000B长文档整帧成本十二次实际
对照已PASS，多行标准化帧数中位20.1923→31.2195（1.5461倍），
无LF长行20→37.8109（1.8905倍）；完整源/索引/ARGB帧/末页/
严格回收与队列零溢出。样式也有变化，范围仅完整draw+FRAME32，
不等于全GUI输入延迟或60Hz。核心/竞态/首字/十九/成本已齐，
独立Studio阶段准备冻结，正式源码/默认HOST预装尚未替换。
Debugger首轮跑过真实TF/INT3恢复撤除，128B内存选择跨尾页停住；
全部原轮保存before-memory-range-test。独立同G2产物只读取证
PASS：真实00401FBD处4B已映射，128B末页PTE=0；00401F80
整128B实际成功并含原对象。应用源053424de/DEBUG ABI不改，
测试保留跨页拒绝及整128B核对、重跑真正核心，后续仍由PASS
门控；该观察不算组件完成，整个M8及Monitor/其它组件继续。

2026-10-03：Studio独立阶段M8-studio-evidence.zip已实际冻结，
663文件/240061726B，CRC及逐文件SHA重读PASS，整包
 e64ef60373dd755eb2cef97fa358a2f863eb6416c9f493f5f779f968a54e5c75。
核心/真实PID复用/排队首字/提交版本/十九组合222图/十二次成本
均绑定同一bd433ea4源/真正G2 f230d4be/7a03638a核/49718852
HOST输入；候选源/产物/副本盘与根正式旧Studio明确分开。
包内Debug/Monitor仅封存时的REVIEW_DRAFT_ONLY，不是其运行
结论。用户追加db XX及右下栈后，Debug旧批核心/20解码/退出复用
与首字设置失败完整保存before-db-hex-and-stack，新源3d01b490
已由实际G2生成9c46083c/map9b60d0f0，1024/100核心PASS：真实
现场/TF/INT3/内存/暂停/重启、每次暂停32槽映射及值、运行中
保留暂停栈/全部页回收，215tick仅2次刷新。新21项db原hex/长度
及32B文本护栏也PASS，其生命周期/首字/十九布局继续，不沿用
旧批结论；正式预装未替换，完整M8及后续Monitor继续。

2026-10-03：TESTING.md新增自动化具体方法，明确真正Windows双盘
QEMU、历史G2客体编译/运行、HMP/QMP真实键鼠、只读页表/帧/
现场、截图与资源/边界、同程序PIT性能对照及摘要/失败留存。
同一新版Debug 3d01b490/G2 9c46083c的自然退出23/PID代数复用
及1920/100动作帧首字完整轮也已PASS；外层draw/main未开模态
时队首4，后续00010得到00400010，目标128B一致，真实Esc结束
和拥有页严格回收、队列零溢出。测试规则的早读队列与合法暂停
误判失败均留history，应用/核/API/字体不改。新十九布局已启动，
Monitor门控等待本批全部成功，尚未原生通过；正式预装与全M8
继续。Studio阶段独立冻结完成，组件表已移除重复的封存待办。

2026-10-03：用户明确改为两阶段。当前先写齐全部M8代码/资源/
构建接线和文档，停止自动构建/原生编译/QEMU验证；全部写完后
必须停下报告并申请第二阶段，用户同意后才完整测试、修复和交付。
自身调试器矩阵/Monitor等待与QEMU已停止，当前未结束矩阵标为
用户改阶段中止。既有报告保留，第一阶段新代码不套用旧PASS。
详细清单docs/M8-IMPLEMENTATION.md（根目标中为sandcore/docs）。


2026-10-03：第一阶段正式源/通用库/压缩服务/原生欢迎与异常/终端/
三款游戏/六幕实时光追、384容量/构建资源/原生发布与验收包接线和
规范收齐，完整表M8-IMPLEMENTATION.md。停下等待用户批准第二阶段；
当前未构建/运行新源，不用历史PASS宣布顶级视觉/性能或整个M8完成。

2026-10-03最新接力：先完成游戏专项第一阶段，停止新构建/客体编译/QEMU，完成报告后必须再次获用户同意才验证游戏。新增WORLDGAME/world_game/world_entities/world_panels与RACEGAME/race_game已连接正式主程序，原始相对鼠标0x81/82追加SCAPI.H/内核，API/ABI不减，字体未动。详细玩法/格式/性能门槛sandcore/docs/M8-GAMES.md。用户试玩独立目录“temp miotest”仅有双盘及run.bat，按当时开发盘复制并SHA一致；不能自动覆盖该副本或用其个人存档冒充测试。最新源码不在这份旧产物中。

2026-10-03游戏第一阶段源码收齐：真实积分排序/禁止结算重复加分、
矮窗口/可见鼠标菜单/窄制作材料行与失败反馈、成功读档释放旧输入与暂停、
legacy中键和回营受伤事实均已收尾。停止并申请游戏第二阶段；仅静态阅读，
未执行本批构建/原生编译/QEMU。可审阅输入在
sandcore/build/m8-games-phase1-20261003-01/，源快照与摘要标SOURCE_ONLY_UNBUILT。
此前整体第二阶段native-01/results.json的欢迎/输入/三代G2-G3字节相等
是独立历史证据；本轮新API/游戏不能沿用它。第一轮boot-01错误欢迎结论
仍保留撤回记录，不能从该旧报告提取新的欢迎PASS。整体M8保持未完成。

2026-10-03第二阶段续接：用户明确整个M8进入第二阶段，授权全部测试、优化和修复。优先实测空桌面/快速鼠标/legacy/新版游戏的CPU、帧耗时与输入延迟；保持同画质对照，用户独立temp miotest副本不参与测试。阶段输入冻结于sandcore/build/m8-phase2-resume-20261003-01；本记录仅确认授权，不代表任何新PASS。

2026-10-04：同步最新彩虹/多色/三棱镜PNG及性能目标；build-08成功，
native-03真实G2/G3为ea1c097b开头，codegen-02整数/C宏/错误事务通过，
scene-equivalence-05六图及缓存失效/护栏/严格页回收通过。完整游戏
和默认PNG仍独立运行核对；Q8乘积下一优化批不沿用该旧证据。整个
M8和最终原生双盘/验收包尚未完成，用户独立试玩副本保持原样。

2026-10-04续：默认PNG首轮真实请求失败后修复路径身份，Lens/
Settings用新增root源助手，桌面owner0内部根规范化，公开SCAPI
cwd合同不改。static-11及新G2六组件编译通过，Lens第二轮1672×941
全部BGRA/Fit/100%实际像素及严格回收PASS。World DDA/零漫射
优化三轮仍运行；更新后的开方/零发光贡献新源又待独立验证，
所有当前状态与输入以M8-PHASE2.md为准。高清速度和条纹画质继续修。

2026-10-04：world-light-02已由同一native-03 G2实际编译旧/新World，三轮交替六张128×96完整着色像素全部相等（9dfc23c8开头），严格页回收；DDA增量地址与零漫反射遮挡跳过的六图总PIT中位301→272（约9.6%）。仅夹具收益，完整游戏帧率仍未达标。最新整数Newton及光照≥240发光体扫描跳过尚待独立回归。

2026-10-04：scene-equivalence-07通过567个floor(sqrt(u32))独立参考、519乘积/356比值、六图全部像素与工作区/回收边界；新SCENE为2c93e3c8。prism-wallpaper-03实际owner0 PNG解码/完整壁纸与cover缓存/PCI纯背景区域/同盘两次冷启动PASS，pf_used均5865；01/02验证器符号/图标区域错误完整保留，不算成功。

2026-10-04：world-light-03最新World a1ec2583（DDA/零漫反射/光照≥240跳过）与b631a1eb基线在相同已验证Newton库2c93e3c8下，由native-03 G2真正编译。三轮六图全部像素9dfc23c8开头及回收一致；夹具PIT总中位392→170。宿主负载/时钟速率跨轮有变化，这不是可直接套用的完整游戏FPS提升。native-apps-07真实G2生成Race/World/Lumen/Mines已PASS，games-640-04正在以590×375完整客户区测帧并额外只读PC采样（计费窗外），尚未判性能达标。

2026-10-04下一批：完整Race/World只读PC样本主要落在scn_fraction_ratio及向量/体素查询；最新SCENE固定8/16位入口增加幅值有证明的直接有符号IDIV，超界/零分母仍原饱和路径。SCCC后端增加固定EBP标量读写/前后缀生成，保留窄转换、原求值顺序、volatile真实访问和地址别名；build-14/15构建通过，但新后端自编译/原生语义及新库等价/完整游戏仍须验证。

2026-10-04：games-640-04（native-apps-07，590×375客户区/World真实590×241视口）完整20秒计费仍明显未达标：World 0.250应用帧/宿主秒，Race/Lumen该计费窗0次新提交（实际启动帧存在，不能把任务栏1秒钟重画当FPS）。正常开始/暂停、World原始相对鼠标与Esc释放、标题关闭/严格页回收/零队列溢出均成功；游戏运行期CPU主要三环真实计算。PC只读样本支持定点比值/向量及体素查询热路；新直接比值与新SCCC固定帧后端尚待自编译/语义与同条件游戏重测。暂停/静态菜单CPU已低，不用其代替可玩帧率。

2026-10-04：native-04固定栈标量后端67dc0cb5开头已完成历史M7编译器→G1→G2→G3真正客体链，G2/G3逐字节收敛；报告新增实际IDE传递源码/include的逐文件摘要与核/符号副本。收敛只证明本scope；14,022栈标量/地址别名/volatile参考、完整语言/整数与新库画面继续逐项核对，不代替游戏FPS。

2026-10-04：frames-01已通过，native-03 G2 ea1c097b与native-04 G2 5d0ed67e在同核真正生成并执行同一FOPT1MIO夹具；14,022项独立参考/492次once副作用/末尾护栏/严格页回收一致。新SCX 13,608B（旧15,092B）；PIT该小夹具旧1/new0只说明10ms量化不足，不能称零成本或据此报告帧率提升。static-15当前26 ELF/217文件、旧40API/29宏、字体/LEGACY/无FPU-libc与引导边界PASS。完整游戏与新固定比值另验。

2026-10-04：scene-equivalence-08由native-04新G2 5d0ed67e真正生成两程序，新固定Q8/Q16有符号短路径与通用函数、356独立比值、519乘积、567整数开方、1,280缓存/未缓存/不同起点查询及六图完整像素均一致；SHA602c6568开头，完整私有帧=画布，严格页回收。一次旧六图77/57/57/68/82/57、新14/17/22/17/18/20 PIT；仍是96×64数值夹具，不能将其改善当作实际游戏FPS/60FPS达标。codegen-03完整新后端30,996整数/C宏/错误事务继续。

2026-10-04：codegen-03已验证native-04新G2：30,996独立整数结果、13次单求值副作用、18组C/宏语义、四类错误保留旧输出与严格回收全部PASS。除零/INT_MIN除-1仍有原IDIV机器码，该项只核编码不冒充实际异常运行；实际异常在整机矩阵另验。新版固定栈后端/SCENE八轮精确夹具通过后，native-apps-08开始由同G2重新生成全部16正式应用，不以宿主产物替原生失败。

2026-10-04：native-apps-08已PASS，固定栈后端收敛G2 5d0ed67e真正生成全部16正式组件，每份SCX/map/log及传递源码SHA保留；游戏共用固定Q8/Q16短路径当前源。这里只是原生编译证据，games-640-05同尺寸游戏/短片成本及新Lens默认PNG运行另测，开发盘仍HOST，未做正式原生发布。

2026-10-04：games-640-05完成native-apps-08新G2/固定比值全窗口实测：World真实590×241视口0.35应用帧/宿主秒、该帧190PIT；Race590×375该20秒窗0次新提交/上一实际帧2444PIT；Lumen590×375约0.05FPS/上一帧1168PIT。运行成本仍主要三环，暂停短片实际busy0.49%；正常退出/严格页回收/World真实相对鼠标与Esc释放成功，未达可玩/顶级视觉。赛车玻璃仍有明显条纹数值伪影，须修整，不能只算性能PASS。下一批SCENE复用精确ray·normal、仅diffuse/spec全零才跳过遮挡；SCCC简单右值直接载入ECX和全局直接读省栈往返，build-16/17通过，新原生回归另记。

2026-10-04：native-05只因观察器在高分辨率已设置后固定等3秒就检查欢迎页而FAIL；失败只读EIP在ata_read_sectors、截图仍初始化黑帧且无三环异常。观察器改为真实boot_stage=2后再等待3秒确认按键门槛，native-06相同输入真正三代链已PASS，G2/G3均1eef13ea开头。新简单右值ECX/全局绝对读后端尚须frames-02 15,498项、codegen语言与新SCENE六图核对；历史失败保留，不改称PASS。

2026-10-04：build-18/native-07最新后端da17cbb8已由历史M7真正生成G1、G2、G3，G2/G3均26080212开头。frames-02的15,498值先通过；frames-03扩充16,482独立值/492副作用与显式asm/普通callee的EBX/ESI/EDI保存，严格页回收通过。codegen-04的30,996整数/13副作用/18 C宏/四错误保留事务也PASS。小夹具PIT不作为FPS收益；当前新后端游戏和最新SCENE另验。用户要求有性能预期，已登记整帧16.67ms/60FPS及先跨33.33ms/稳定30FPS，当前未得可靠最优预测；同源GCC-O2客体诊断正在测代码生成差距，不代替正式SCCC。完整M8继续。


2026-10-04：用户明确将Welcome to true color.改为本机全部核心、高像素/充分去噪离线影片，SandCore内播放；播放器/解码器允许宿主编译与有状态/回退合同的SIMD。这项新合同覆盖旧实时光追要求，游戏实时与顶级画质保持。必须有彩虹/丰富光谱，与Lens默认艺术三棱镜PNG呼应但机位不同，专业运镜持续打磨，完成后报告成片完整位置。AGENTS.md§七及docs/FILM.md/FILM-STORYBOARD.md登记；Blender官方便携4.5.3校验通过，CPU12线程/ONEAPI空，已实际渲染构图草图，正式母版和播放器尚未完成。用户授权只读学习color/windows的批次几何/行级并行/连续SIMD与缩放缓存，不把其低分辨率摘要插值用于4K逐帧母版。

native-08/build-19后端0b50ea3b与SCMEM能力宏经真正历史M7→G1→G2→G3，G2/G3均b62ad234开头。scmem-01/02分别通过1880内存用例/72裁剪矩形/1521440B全数据与返回值/护栏/严格回收；scene-equivalence-09六图/519乘积/567开方/356比值/1280查询也PASS，限定夹具不冒充高清游戏FPS。GCC-O2整客户区诊断World2.35FPS/Race0.05/Lumen0.10，HOST不作正式原生交付。缓存成本首轮copy后header清全画布而被相同内容优化跳过，保留cache-floor-640-01并撤销其传输FPS解释；修正第二轮真实不同图案590×375，20秒560应用提交/568 WM帧，分别28.0/28.4帧每宿主秒，严格回收/零溢出。当前固定管线也须优化，不能把28.0称游戏最优上限，或称已有60FPS。完整M8继续。


2026-10-04：缓存固定成本同核/同原生framebench再次复测cache-floor-640-03-baseline，590×375、20.015秒1305应用提交/1312 WM帧，分别65.201/65.551帧每宿主秒；客体PIT2001（约100tick/秒），宿主一核心94.38%，严格页回收/零溢出。旧cache-floor-640-02为28.0且PIT1151/20秒，当前内核/原生产物均未改，因此不是软件优化收益，不能把28.0或65.201称固定上限，亦不能称游戏已有60FPS。继续采用负载接近的同源、同画质交替对照；高清游戏上次实测仍未达可玩。

2026-10-04：film_scene.py修正白光/35谱体积与对应聚光灯使用同一连续淡入包络；Glare改4.5节点输入与明确8%叠加，字幕完整不透明、署名避让光束，中景样片落地，字体与默认PNG摘要加入工程身份。scout-06第2221帧1280×720/64样本实际完成208.969秒，SHA256 201e6ebbcfdee1916b225d9c866ece1ded9ca3f25ef8e5c28e76c65bac6cbda5；已视觉检查。可见谱采用高斯径向/沿程衰减的预定义发射体积光场，摄影机仍真实体积积分/反射/玻璃透射，对应聚光灯真实照明；不宣称完整物理色散。此为构图样帧，六幕/连续运镜与正式4K2520帧母版、编码视频、客体播放器均未验收。

2026-10-04：scout-06六幕中点180/570/990/1410/1830/2221帧的构图复核已实际启动，CPU12线程；第2221帧同工程已完成并复用，另外五帧在渲染。请求与日志留build/film/20261004-scout-06；尚不是连续运镜检查或4K母版，进程退出后按真实输出复核。

2026-10-04本轮接力：SCVOX通用查询库两代编译器/完整命中/25份工作区通过，World两轮三轮六图均保持精确像素；8格块实验成本反而增加，已撤回默认游戏接入，保留代码与失败成本。build-22恢复文件旧时间戳导致安装未刷新，build-23及native-apps-11才逐源码/132866B原生产物确认恢复；验证器新增开机前全源一致性与冻结overlay。游戏可玩性能仍未达标，见SCVOX.md/PERFORMANCE.md。短片六幕720p试帧完成，scout-07改连续柔边光谱/彩虹与条形灯箱，正式4K成片和播放器仍未完成。

2026-10-04最新恢复实测：build-23/native-apps-11与同G2重编基线World产物完全一致。games-640-07在640x480/100%、实际590x241视口，20秒21应用帧=1.05FPS，PIT2001、QEMU宿主一核心96.41%、客体运行busy99.55%；静态/暂停busy约1%，真实相对鼠标/Esc释放/关闭与strict pages及零溢出通过。回到此前较快基线，不是新增优化收益，仍不可玩。正式3840x2160/16位/512样本/CPU12线程终幕试帧2221已启动，build/film/20261004-master-08；仅请求一帧，完整2520帧母版/视频/播放器未完成。


2026-10-04：本轮状态核对：build-26完成，SCENE精确几何测试仍待；4K单帧由独立Blender工作进程继续，尚无完整影片/播放器。最新剩余六组工作持久化至sandcore/docs/M8-PHASE2.md，不将历史PASS扩写为最终M8完成。


2026-10-04：scene-exact-02两代原生各1,024组/22,528完整几何值、26生命周期及护栏/严格回收PASS；当前最大4096容量、旧默认六图和赛车接入另测。未把影片同时渲染期间的夹具计时用作性能结论。


2026-10-04 14:31：精确几何03实际4096容量/两代完整参考、旧默认六图12均PASS；Race已build-27/native-apps-12原生接入，实际运行/回收但仍慢和待顶级画质。4K终幕2221已正常完成4266.282秒并完整校验/检查，1/2520；后台整片队列实际--frames all开始。队列/进程/续渲合同和16边界见FILM.md及queue-boundaries-05。用户最新多贴图/算法优化目标持续，GPU接口为后续候选；编码/客体播放器/完整M8交付未完成。

2026-10-04封存修订：按用户最新决定暂时结束并跳过M8未完成内容；现有主体成果够用且获认可，真彩色/组件与全部已写实现及证据保留。停止自动开发/优化/测试/渲染，封存待办和后续先评估、设计/排版及用户批准取舍规则见M8-FROZEN.md/M8-RETROSPECTIVE.md、根AGENTS.md§八。

2026-10-04发布修订：用户明确将当前成果命名并发布M8a；范围/未达成预期/现有镜像来源见M8A-RELEASE.md。本次提前收束获用户明确许可，以后未经许可不得重复；完整M8余项继续封存。

2026-10-04 M9开工：用户逐项确认UID/GID六位rw、执行由r控制、root仅绕过普通权限、进程固定创建者身份、内核票据无SUID、升级SandFS、仅外部串口SYSTEM写MOD且客体不可自连、双串口、零环一次性/常驻但无热卸载、SCCC CLI/自写约80% BusyBox独立SCX、SSE2整数、窗口截图/串口开发闭环和本机连接工具。音频含开关机/多种提示声/MP3 WAV，先QEMU；复杂组件普遍允许宿主编译，第三方解码仅宽松自由许可并保留许可证。M9不加标准C运行库，GPU暂缓，网络M10；M10完成时基础内核仅安全更新，功能走CORE SKM。用户已授权写文档并开工；见sandcore/docs/M9-DESIGN.md/M9-IMPLEMENTATION.md/SERIAL.md。当前仅设计与新底座开工，不是M9完成；M8a/历史封存不动。

2026-10-04串口澄清：用户指双机硬件调试，真实机仅从机外物理串口由另一台机器接入；当前QEMU客体=被调试机，宿主工具=外部调试机/模拟线缆，等价硬件access。系统内虚拟端点、网络转发、客体自连均不算授权来源。设计/串口规范/AGENTS已同步；新UART底座只收发硬件字节，尚无SYSTEM登录或零环命令解析，不可称安全模型已完成。

2026-10-04 M9阶段顺序：用户最新要求先写齐全部代码，再统一调试。目前只有serial.c/h双UART队列/IRQ/main/Makefile接线、tools/scserial.py本机双串口/QMP工具初版和M9文档；没有执行构建、编译、Python工具或QEMU。后续继续身份/SandFS权限/CLI/零环调试与打包/传输截图/SSE2音频全部实现，源码仅静态审阅。实现收齐后进入统一调试，用户未要求另加审批；不继承M8旧PASS。

2026-10-04 M9第一阶段持续实现：现已写auth/强熵/PBKDF2内核票据与固定UID、SandFS v5权限/COW双bank/流式事务/持久对象序号/mkfs_m9、32任务和保留旧8行快照、新字节流/管道/长参数/作业/终端Ctrl-C及EOF、COW环境、COM2管理帧/CRC32/SHA256/host put/get/会话撤销、COM1真实INT3/TF调试、SCCC CLI/SKM生成与即时/常驻模块/辅助页回收、SCCC与SandAsm整数SSE2及FXSAVE/FNSAVE任务状态、窗口原生快照/BMP、AC97整数混音/重采样/PCM/开关机音效生成源码及固定dr_mp3许可的GUI/CLI MP3/WAV播放器。user/m9里自写独立文件/文本/账户/开发工具与AST Shell持续增加，ed可串口编辑源码；80%行为覆盖尚未达成，不能宣称全量实现收齐。m9.mk独立构建链已写且不触发冻结游戏/影片，320KiB内核容量三处+BOOT同步。状态/合同已同步M9-IMPLEMENTATION、CLI-M9、AUDIO、SIMD、FS、SERIAL、SYSCALL等文档。**仍没有任何M9构建、编译、Python工具执行、音效资源生成、QEMU或测试**。继续补齐命令矩阵/完整CLI行为、Shell扩展、构建源接线与全路径安全静态审阅后，才统一调试；不增加用户未要求的审批门槛。
## 2026-10-06 M10a1 已批准开工

用户已批准完整规划并要求按规矩开工；最高优先级取消固定多任务上限，隐藏会话停止图形绘制。基线24797e0，分支codex/m10a1。合同与状态见sandcore/docs/M10A1-DESIGN.md、M10A1-IMPLEMENTATION.md；先写齐全部实现/资源/规范，再统一构建/客体编译/Windows双盘无头QEMU，授权已覆盖验证，不另加阶段审批。正式私钥仅用户持有；代理只收公钥/签名，正向签名测试等用户结果，不能设开发信任入口。

M10a1开发版可commit无Release；e1000+IPv4+18/55工具、原始完整凤凰TTF、256MiB盘、会话隔离、最小loader+磁盘CORE主内核、有序验签扩展均保留。M9正式产物/M8封存保留，不能擅自减目标。当前已开始动态任务源码实现，只做静态审阅，没有新构建/QEMU；开工前既有未跟踪.zcodeignore不纳入代理提交。

修订：2026-10-07，记录network-08真实终态、TCP地址复用构建和65会话夹具修正；区分旧主核通过与新主核待验。

修订：2026-10-07，记录sessions-06双来源全部声明用例通过和65真实桌面范围；保留6页差额及完整其它合同待验。

修订：2026-10-07，记录network-09新增选项实际PASS子项与客体nc编译失败；补SRC/INC双处头文件的定向资源依赖。

修订：2026-10-07，登记Shell stdin双来源62项真实PASS及network-10重测，资源总账和其它完整合同继续。

2026-10-07：curl与必要收尾验证完成，19工具开发验收包提交；停止新增测试，等待用户验收。
