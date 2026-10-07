# M9 自写 Shell 与独立命令

> **2026-10-07 Shell真实回归通过：** shell-run-01退出0，两只读来源盘各31项、共62項stdin回归通过，源盘摘要保持。文件/管道EOF、无末尾LF、连接未结束时exit、跨行引号/循环/heredoc、CRLF/Tab、read及子进程保留后续输入、坏控制/NUL/未结束语法拒绝、旧脚本/-c和显式-i都有精确输出/退出码/错误/日志。此范围不冒充全部Shell或整机验收。publish-03双来源各429文件归档/当前SRC头逐字节核对通过，network-10 PID8632正在完整三profile/双来源/18原生工具重测，未收齐终态。新65会话冷暖资源页账探针已补，尚待执行；Goal active。

> **2026-10-07 M10a1修正，实际重测待收齐：** 新版sh无参数时按TERMINALINFO2判断stdin。TTY/管理串口保持欢迎语、提示符和输入编辑；管道/文件/内存/空流逐行执行脚本，不回显、不打印提示符，不等整条连接EOF才执行exit。未结束的引号/复合块继续收齐，EOF按正式语法报错，NUL/非法控制字节明确失败；不预读孩子/read内建还要消费的stdin。`sh -i`或原`--serial`可显式取交互循环，身份/权限不因该选项改变。宿主定向SCX构建通过；network-08首来源nc-e sh精确7B正文及原Shell退出7通过，双来源完整网络修复重测中；普通管道/EOF/read/子进程消费/语法的独立回归脚本已收齐待运行，M9历史原字节保留。

> 当前验证范围已更新，见末尾“2026-10-05 M9最终验证记录”及[M9验收报告](M9-ACCEPTANCE.md)。早期未验证/待验标记保留阶段背景。

> 当前已进入统一构建/运行：整包构建通过，06首盘实际Shell/文本/压缩/
> 原生编译/nano/scdbg等分项通过；18双盘224项与19自举双盘18项已通过。
> 约80% BusyBox行为目标尚未验收。以下源码边界和早期“未执行”叙述
> 保留阶段背景；最新证据与失败见M9-VERIFICATION.md。

15首盘完整九阶段95项通过，包含真实无窗口s3c、nano和scdbg；次盘缺默认
主题配置失败，不能宣称双盘通过或80%达标。宿主菜单另已核对COM2 IRQ
断点12秒暂停/继续和后续命令，不借客体串口自连获取SYSTEM。

原版M7兼容测试16发现Shell将简单参数也强制包双引号，旧GETARGS按原合同
返回文本，旧程序比较handler时收到带引号字符串。修Shell只对空字段/空白/
引号/反斜杠编码，简单字段原样传递；旧API和128B视图不改，复杂字段仍为
新CLI保留边界。17原版M7三模式双盘18项、18现代参数和旧SCX集成重测通过，
范围之外的历史程序仍需实际验证，不据此宣布全部兼容完成。

各user/m9/*.c有自己的main、独立链接/SCX载荷。复用SandCore专用头文件的
字节流/行读取/路径/正则算法，不复制BusyBox源码，不按argv[0]从同一程序
分派所有命令。M8a历史multicall文件保留，M9发布树覆盖对应的新独立文件。

Shell已写引号/转义/变量、独立环境快照、PATH、1024B参数、管道（最多8段）、
< > >> 2> 2>>、&&/||/!、后台与wait/jobs、子Shell、if/elif/else、while/until、for、
带层数的break/continue、脚本位置参数/shift、交互多行。另已写函数/case/
source/eval、整数算术、命令替换、引号敏感的字段拆分与路径glob；for词列
先展开一次，独立保留给循环体。here-document用内存快照传递，分隔符有
引号时保留正文，无引号时展开变量/算术/命令替换。管道并发作业，前景等待不抢
子程序输入；终端Ctrl-C独立事件，Ctrl-D产生一次EOF。权限、身份、进程代数
由内核检查。已追加`${v:-word}`/`${v:=word}`/`${v:?word}`/`${v:+word}`及
非冒号形式、`${#v}`字节长度、`%/%%/#/##`通配裁剪、`$$`与`~`。
未选中的operand不执行命令替换；嵌套有界，赋值仍受变量255B限制。
IFS只对未引用的展开内容分词，字面文字不被IFS切碎；`"$*"`用IFS首字节，
嵌入的`"$@"`保留空参数并将前后文字接在首尾项，`<<-`只剥原正文的开头Tab。
更多Shell语义/工具及完整行为审阅继续，源码能力不代表行为已验收。

串口开发基础已写：ed行编辑器（a/i/c/d/p/n、范围、字面替换、r/w/q/Q）、
sccc source.c output.scx [--skm]、sandasm、skmrun、capture、系统内执行。
ed的w才发布COW文件，失败保留旧文件/未保存缓冲。宿主:put/:get带SHA256，
不通过客体自己的UART来获取最高权限。账户工具su/login/passwd/adduser/deluser
只调用内核票据与账户API，SYSTEM始终不走普通登录。

用户2026-10-05选择自写nano，不做vi。新增[nano合同](NANO-M9.md)和
[scdbg CLI合同](SCDBG-CLI.md)；M9的s3c/sccc均为无窗口编译器入口，保留
旧图形编译核心与历史产物。CLI参数用引号分词，阶段/诊断走stdout/stderr，
不在管道失败后回退桌面PUTS。scdbg与debug是同一调试核心的两个命令名。
宿主`:raw`让nano接收真实逐键输入，尚未证明串口编辑/编译/调试闭环通过。

文本初版含cat/echo/wc/head/tail/nl/rev/uniq/sort/grep/egrep/fgrep/cut/expand/
cmp/tee/strings/hexdump/seq/yes；文件初版含ls/stat/cp/mv/mkdir/rm/rmdir/touch/
basename/dirname/realpath/find/du/chmod/chown/chgrp；另外有env/printenv、
ps/kill、身份、时间、音频与电源工具。容量与不支持选项明确失败，不输出
截断后伪装完整的结果。排序采用有界内存稳定归并，正则采用Thompson NFA，
禁止用指数回溯把串口Shell拖住。

2026-10-05续写sed、diff、xargs、install、clear/tty/ttysize及独立`[`工具。
sed采用自写NFA地址/替换与模式/保持空间；diff为有界Myers最短编辑路径，
xargs支持有限并发并按作业领取退出码；不冒充已有客体执行证据。
gzip/gunzip/zcat/unzip采用固定MIT miniz低层DEFLATE，容器/路径/事务/CLI
由项目自己写，第三方声明和源码归档见THIRD-PARTY.md。解压默认总上限
16MiB，可用DECOMPRESS_LIMIT设定；ZIP暂不支持ZIP64/分卷/加密/符号链接。

分页/文本续写less/more、split、unexpand、sum、od、dos2unix/unix2dos和
paste。分页器按需读完整页，可回退/字面显示控制码/正则搜索，最多16MiB缓存；
交互一次一个文件，重定向输出可顺序拼多个文件。split按行/字节切分并逐文件
COW提交；newline命令具备精确容量两遍转换和提交失败回滚。paste多个`-`
共享一个stdin游标，不为每列另开预读器。

另写patch单文件unified/-R/-p/--dry-run、length/logname/usleep及pidof/
pgrep/pkill。patch精确核对所有hunk与末行LF后才发布COW正文，拒绝多文件/
fuzz/新增删除文件；输入代数在事务建立后再次检查。usleep按10ms PIT向上
量化。进程名称目前为内核12B短名，不能宣称-f完整命令匹配；无通用Unix
signal语义，pkill只按内核权限结束并原子核对PID+代数，不支持-o/-n排序。

差异必须列入后续可机读矩阵：无x/SUID；chmod的三位0..3分别u/g/o rw，
r=1/w=2；SandFS尚无Unix时间戳/硬软链接，touch目前仅创建缺失文件；
tail当前不支持-f，sort当前-n是有符号32位整数，ed替换为字面而非正则，
grep/sed/expr正则暂不支持反向引用/重复区间；参数替换的operand里嵌套
多字段`"$@"`明确失败，避免把内部字段边界截成第一项；暂无substring展开。不能用
命令名字、宿主编译通过或这些部分实现充当80%达标证据。

[M9-CLI-MATRIX.tsv](M9-CLI-MATRIX.tsv)已冻结官方BusyBox手册Commands集合
310个名字，其中55项标注M10-network，仍完整保留在参考表，不偷偷缩分母。
vi按用户决定单列USER-EXCLUDED，nano列入SandCore扩展清单，不冒充vi通过。
这只是所选手册的参考快照，不称覆盖BusyBox全部配置/版本。每项源码存在
标SOURCE-UNVERIFIED；选项/错误/管线/身份/资源行为仍待补验收用例与统一
运行。约80%目标仍有效，缺项继续实现；名字、编译通过均不计作行为PASS。

续写comm（C字节序/检查输入排序）、fold（原UTF-8标量不拆断，-b按字节）、
catv、killall、watch、top、date/cal、getopt、expr、整数dc、envdir、run-parts、
MD5/SHA-1、uuencode/uudecode和经典ar。所有工具为独立main；hd仅是同一
hexdump能力的独立入口，不按名字重复计行为。top读取完整32任务PIT样本，
首次尚无间隔时显示等待，不把100Hz称帧率；不可见任务不泄露其采样明细。

限制公开：getopt仅精确长名，不支持缩写；expr匹配返回从开头匹配的字节数，
无捕获返回；dc为32位整数RPN子集，无小数、宏或输入基数切换；date只读UTC
RTC，当前年份合同2000..2099，不支持设置时间或文件时间戳；cal为Gregorian。
envdir最多16个内核环境变量、255B值；run-parts筛选无点的字母/数字/_/-名，
按名字排序执行SCX，不猜文件是脚本。ar只支持经典15B成员名及t/p/x/q/r/d，
不支持GNU/BSD长名、thin archive或链接符号索引。uudecode校验完整结束标记
后COW发布，默认16MiB上限，拒绝未指定输出时的绝对/父目录成员路径。
MD5/SHA-1仅供旧文件校验，不替代内核密码/票据的PBKDF2-HMAC-SHA256。

共享CLI读取改为按实际正文倍增分配，上限仍严格探测EOF；避免多个小归档
成员各预留16MiB。未得到性能测量，不称这次源码调整已性能达标。

修订：2026-10-04，自写独立工具和Shell源码初版，记录未收齐/未验证及明确差异。
修订：2026-10-05，同步Shell扩展、sed/diff/xargs等、DEFLATE适配、310项
参考快照及第三方代码归档；全部未构建或运行，不声明80%达标。
修订：2026-10-05，nano替代vi、S3C/scdbg CLI、逐键串口、分页/转换/patch/
进程匹配源码与限制；仍未编译执行，不将命令数量当作行为覆盖率。
修订：2026-10-05，追加参数运算/IFS/嵌入$@/<<-、文本/校验/归档/脚本工具，
只读RTC和完整任务采样；公开整数/格式/选项限制，全部尚未构建或执行。

续写newc/crc cpio创建/列举/提取、sync/fsync、只读hwclock、readlink规范化、
单卷mountpoint、resize查询、pipe_progress、script/scriptreplay。cpio先完整
校验格式/结束标记/CRC/路径再提取，成员依次COW提交；后续权限错误不承诺
整体回滚。-F创建归档用自己的事务，其它管线输出是普通流；只支持普通文件/
目录，无硬软链接/设备节点，不根据归档uid/gid赋予新文件所有者或权限。
成员名63B/最多1024个、归档16MiB，无old binary/odc格式。

sync/fsync发真实ATA flush；SandFS原写路径本来逐扇区flush，新命令不会把
别人的未提交COW文件强行发布。fsync目前要求存在的普通文件参数，无-d选项。
mountpoint只识别实际挂载的SandFS根卷；readlink默认因无符号链接失败，只有
-f/-e/-m做相应路径规范化，不冒充已实现链接。resize只输出当前字符尺寸变量，
没有串口外部尺寸协商。hwclock只读，与date共享完整RTC快照，不支持写时间。

script记录子Shell合并stdout/stderr，同时写原终端；-c执行代码串、-a追加，
-t将PIT间隔/字节数写自身stderr（可重定向为计时文件）。stdin仍是原TTY，
不创建可访问UART的客体设备；输出默认16MiB/SCRIPT_LIMIT可调至64MiB，
IO错误撤销文本事务，完整会话即便子程序非零退出也保存文字并返回其退出码。
scriptreplay严格核对计时与正文完整字节数，十毫秒量化，无PTY/Unix信号仿真。

修订：2026-10-05，追加cpio/记录重放/卷与缓存CLI及0x238，公开边界；未验证。

追加自写SHA-512（双u32表示64位字，16字滚动消息调度）、原子mktemp、
awk整数解释器、bzip2/bunzip2/bzcat、unlzma/lzmacat、man、insmod/lsmod、
crontab/crond、logger/logread及chpasswd。SHA-512算法/常数依RFC6234定义，
未移植其示范C代码；[RFC算法定义](https://www.rfc-editor.org/rfc/rfc6234.html)。
mktemp只有真实无覆盖创建，无-u不创建模式；名称保证碰撞检查，不称密码学令牌。

libbzip2固定1.0.8、LZMA SDK解码器固定26.03，许可与完整源码见THIRD-PARTY。
全部整数运算；私有堆同时在用上限24MiB，单输入/输出16MiB；bzip2支持级别
1..9、-s小内存解码、串联成员与CRC校验。LZMA仅Alone包装，支持已知长度
或带结束标记的未知长度，不支持XZ/7z/编码。-c stdout可以已有部分字节；
命令自己的文件事务只在完整验证后提交，截断失败保留旧目标及原压缩文件。

Shell追加$!/wait PID，jobs只读状态，不提前领取退出码。取消子任务原子核对
PID+代数。平铺sequence/and/or按左链迭代，独立命令的参数与前缀环境缓冲
按调用层缓存，循环复用；case/for短生命周期缓冲独立释放。深度/容量超限
明确失败，嵌套function/source使用独立帧，不共用被覆盖的argv正文。
经典ar在读取每个新成员前核对最终归档容量及新正文累计16MiB上限，
不能积累32份16MiB后才在publish失败；原归档与新正文归属分别明确。

insmod常驻真实SKM，lsmod仅串口SYSTEM列出实际装载名称，Linux.ko不支持。
man输出SYS/MAN已有文本或-w路径；当前有nano/s3c/sccc/scdbg/sh/模块/cron页，
不是空壳help命令；未提供的页明确失败。

crontab自写五字段数字/range/list/step解析，最多32条/32767B；-l/-r/-e/file，
-e用私有临时文件打开nano，完整校验后才更新本人HOME/CRON.TAB。
crond前台按UTC分钟匹配，DOM/DOW限制组合遵循OR规则，最多16个同时作业；
当前分钟不因reload重跑，跳时不追补漏掉的分钟，坏替换保留最后有效表。
作业只继承真实UID/GID，不会得到SYSTEM；--once供有限运行/客体验证。
不支持用户名前置/@关键词/环境行/-u/Unix后台守护；停止回收专属子任务。

logger/logread使用本人HOME/LOG.TXT，首次mode3；logger支持-t tag/-p数字，
argv文字或stdin多行，前缀记录UTC/当前UID/PID/优先级，控制字节转义。
总64KiB明确失败上限，无静默轮转；最多8次并发代数重试，COW失败保留旧文。
这些是普通用户文件，不是内核可信审计，也不冒充Unix/dev/log或syslogd。
chpasswd从不回显stdin读取name:password，逐账户走内核异步改密票据并擦除
暂存；需root或串口SYSTEM，普通用户/SYSTEM普通登录限制仍由内核执行。
不接受已经加密的散列；多个账户逐项提交，后项失败不回滚已成功的前项。

修订：2026-10-05，补压缩/调度/本地日志/手册/改密与Shell作业/栈边界；
源码及规则已接，统一构建、原生编译、行为覆盖率与性能仍未验证。

统一构建前冻结适用性列：完整310行保留，本地171、M10网络55、平台专有82、
用户排除2；每个非本地项带原因，原state=MISSING不改为通过。140个本地
参考名有源码；总表45.2%/本地81.9%均为名字/源码计数，不能等同独立功能
或实际行为覆盖率。本地未实现31行、所有待行为验证行继续列出。
工程分类基于既有系统边界，口径澄清没有新的用户回答；不声称用户批准
缩小分母，最终同时报告总表与适用功能的证据，约80%目标仍待实际验收。

修订：2026-10-05，源码/资源接线收齐后开始统一构建；保留全部缺项与
两种计数/未验证状态，不把工程适用性解释成新用户授权或行为PASS。

## 2026-10-05 M9最终验证记录

用户确认本地140/171（81.9%）口径；310行原表完整保留，31个本地缺项及选项限制公开。BEHAVIOR-SUBSET-PASS映射到双盘实际行为，不表示BusyBox所有选项兼容。核心128项、扩展256项、边界34项及账户专项通过。id支持-u/-g/-G/-un/-r，冲突/未知/组名选项拒绝；旧无参数输出保留。HOME/默认cron/日志/波浪号显式卷根化；od -An取消额外结束空行。

修订：2026-10-05，记录实际范围与证据，待用户验收；前述早期未验证叙述保留为历史。

M10a1源码新增变化：ps/pidof/killall/top与login/su进程追踪接入0x240分页，旧8/32行接口不扩写。top用动态双快照/代数匹配/索引堆排序，批处理输出全部任务；交互页按真实终端高度取行。Shell后台表按实际用户堆增长，去掉16项后强制前台行为；新sessionctl list/show/logout复用会话权限。上述仅源码，M9历史140/171行为统计不自动增加，sessionctl不计18个网络工具，网络仍源码0/构建0/行为0。

修订：2026-10-06，登记动态Shell/全量进程查询与sessionctl，未构建/未运行。

2026-10-06最新M10a1：18独立网络工具初版源码已写入user/net，源码18/18、构建0/18、行为0/18；用户明确追加nc -e。M9原310行/网络55分母及历史PASS保持，M10单独55行矩阵/公开范围见CLI-M10.md和M10-CLI-MATRIX.tsv。所有新代码未构建未运行。

修订：2026-10-07，区分nc-e真实已验子项与新stdin文件/管道回归脚本待验，M9历史原字节继续保留。

修订：2026-10-07，登记Shell stdin双来源62项真实PASS及network-10重测，资源总账和其它完整合同继续。
