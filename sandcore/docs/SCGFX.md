# SCGFX 1.0 / Canvas2 / Lens2

2026-10-16。`SCGFX.SCL`是真正Ring3可动态装载库，公开`SCGFX.H`、`SCGFXDRAW.H`、`SCUICANVAS.H`。依赖SC32API1.4/v6、SCCLIB、SCLIB和SCUI1.2/v3，方向SCGFX→SCUI。`SCUICANVAS`实际由SCGFX实现；SCUI只提供`SCUIARGB.H`同步1:1裁剪blit，不反向依赖SCGFX。

Surface原生straight ARGB32。宽高各≤1920，总像素≤2073600；1080p旋转为1080×1920而不丢像素。原压缩服务最大1920×1080保留；SCB/BMP原生读取支持同面积portrait。512×320默认Canvas画纸保持655360B；1080p为8294400B。Pixels只是借用只读视图，到下一次修改/Step前有效。引用各自Retain/Release，所有失败候选不发表。

Edit第一次改32×32 tile才备份，Commit才截断Redo；Abort/操作失败精确恢复已改像素。历史动态链默认8MiB、可设4KiB..32MiB，预算依据实际记录字节，超过活事务预算明确失败并回滚。Undo/Redo交换一份tile像素；尺寸变换记录拥有上一幅原生存储并O(1)交换。没有固定撤销条数。保存revision与编辑revision独立，撤销回保存修订会恢复clean；BMP导出不清native dirty。

Operation是实际分批作业。Step预算1..65536像素/细分工作（默认应用16384）；文件批次≤64KiB。长画笔、Flood、清空、路径填充、Undo/Redo与失败回滚都可取消。Close活动对象先取消并PENDING，直到返回0才消费；调用者继续Step、泵输入和Close。文件lease清理尚未完成时对象不丢弃。隐藏停预览生成，已授权文件/解码/编辑后台收拢仍继续。

应用清理若遇持续IO错误，仍保留operation并按25 tick上限重试，继续泵输入；用户要求关闭时仍先确认dirty作品。明确丢弃后可退出，正式内核owner停止链继续收拢本人lease/worker，不把指针丢弃解释为服务已回收。

Path动态存储Move/Line/Quadratic/Cubic/Close，整数坐标-32768..32767，以Q8 de Casteljau自适应细分到≤0.5像素曲率判据或16层；不把路径条数、边数固定。操作期间借入path引用并锁写，Step按预算细分。stroke为软圆笔/整数alpha混合、native半开clip；fill自动闭合子路径，even-odd或nonzero扫描线。逐行将交点计入宽度≤1921的绕组数组，成本O(边×高度+输出像素)，不对每行交点做O(n²)插入排序，也不使用内核绘图/浮点。所有分配有失败回滚。

Canvas视口共用物理clip、捕获、焦点和SCUI事件树。100%为一源像素对应一物理像素；Fit/10..800%按中心Q8和alpha加权整数双线性缓存。缓存默认12MiB，按损伤更新；地图/尺寸/原图变更才重建，OOM用当前损伤最近邻读原图，完整源不降采样回写。调色与UI分离：原DAC100/99/58/66/102/11/22/34是图像内容，UI只用GFX已登记主题角色。

文件Load候选在完整格式/长度/IO验证后Ready，应用此时才询问保存/丢弃旧dirty图；失败保留作品、旧文件身份、Undo/Redo、倍率和平移。Save持有source读锁，目标WriteExclusive、私有candidate、flush和条件Publish；generation与权限由正式文件服务核验。保存成功SCB2才MarkSaved并换身份，BMP24导出以原纸色合成alpha。Cancel如与durable Publish竞争，以真实已提交结果为准。MemoryLoad借用来源至operation terminal，封存Step复用唯一SC32IMAGE合同。

剪贴板格式0x10024：32B头`SCGFXA1\0`，LE width,height,stride,bodybytes,version1,reserved0，随后完整straight ARGB。真实会话broker发布/快照，Copy提交后Cut才清选择；Paste校验精确总长和尺寸并作为可撤销事务。原native像素和alpha不从视口截图推导。

Canvas2与Lens2共用SCUI菜单/工具栏/Status/FileDialog/MessageBox/PropertySheet、动态历史provider、可调splitter和浮动dock。小窗口收起侧栏，菜单动作仍可达。Canvas保留512×320默认、RGB/软笔/连线/Undo/Redo/清空/候选打开，新增多步历史/选择/填充/橡皮/形状/取色/剪贴板。Lens默认真实WELCOME.PNG，保留F1打开、F Fit、1物理100%、C Center、拖动与透明采样，新增缩放/旋转/翻转/保存/导出/属性和撤销。

新入口`/APPS/CANVAS2.SCX [--open] /absolute/image`、`/APPS/LENS2.SCX [--open] /absolute/image`。无参数Canvas新纸、Lens读取原WELCOME.PNG。默认桌面种子只替换这两个入口并标modern，旧源码/SCX保留原字节。SDK安装五个新大写头、SCGFX库及精确manifest。

修订：2026-10-16，新增完整Ring3图像/路径/Canvas控件和两个SCUI应用；实际宿主/guest证据另列，不以源码宣称视觉或性能达标。
