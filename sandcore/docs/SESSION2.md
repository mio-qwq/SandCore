# SESSION2 / LOGIN2

2026-10-16。真正SCUI应用统一菜单、用户名、Secret、默认SignIn/Cancel/Clear、真实认证进度/错误、动态会话表、切换/注销/锁定、确认和属性、键盘、状态及splitter；登录表单使用标准Scroll。旧user/session.c、SESSION.SCX、LOGIN.SCX不覆盖。普通入口APPS/SESSION2.SCX，固定F12入口SYS/CORE/LOGIN2.SCX，同一真实正文，身份依赖内核事实。

会话分页64项一批、内核256 examined预算，动态候选完整后发表，不把16可见行当总上限。列表按真实SID排序，provider二分查找，切换/注销前重新读取并核SID、UID/GID、kind、generation与closing。注销需明确确认，失败保旧表/会话。普通/root只见内核允许的会话，外部硬件SYSTEM权限沿旧合同。

认证票据等待AUTH事件，显示时才按10 tick DRAW_ONLY timer查询真实进度；隐藏验证/完成/取消继续，60000 tick期限保持。空闲按SCUI真实截止与Broker handle0事件等待，ContextStep只做非阻塞泵，先处理已排队动作，避免消费输入后再次睡到无关输入。口令只在标准Secret专页，应用回调直接借给ScAuthLoginBegin。

固定LOGIN2是AUTH_SERVICE UID=-1、subject=-2、LOCKED SID，绝非串口SYSTEM。内核启动事务核真实LOGIN2 SCX长度/SHA，在同一暂存字节建pd，之后才授私有role；role不继承。普通EXEC同一个文件不获role。LOGIN2仅本人GUI、必要内存/ROOT/模块登记/字体/查询、只读文件、Auth四入口与handle0纯事件Wait可用；不能执行新程序、日志输出、改配置、网络、音频、剪贴板、IPC/Shared/Drop或枚举/控制其它会话。

精确读取名单为受保护SYS/LIB/SCLDR、SC32API、SCCLIB、SCLIB、SCUI与SYS/UI/UAX15_1.SCU。发行header固定各真实文件长度/SHA，按实际内容revision缓存；不信用户同名覆盖或仅迁移SYSTEM元数据。资源失败在启动前拒绝；后续UI构造失败保持失败关闭，不以提权或恢复未认证桌面绕过锁定。Broker身份仍拒LOCKED/subject=-2；现有handle0 Wait无Broker对象即可服务SCUI，未注册drag/drop时ContextStep不调用IPC。

主代理最终native交付必须由实际LOGIN2与同源五库/UAX重生两个issue头并重编CORE；HOST摘要不是native发行授权。旧F12/登录源码与旧成品保留，新固定路径只在本批CORE接线生效。默认启动盘、用户盘和其它应用迁移由Root验证安排，本草稿没有装盘或启动QEMU。

修订：2026-10-16，新增Secret、固定发行登录角色与Session2实现，guest/视觉待验。
