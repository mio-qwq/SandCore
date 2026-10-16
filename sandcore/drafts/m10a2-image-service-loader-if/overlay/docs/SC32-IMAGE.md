# SC32IMAGE 1.0

2026-10-16。公开声明在`SC32IMAGE.H`，实际正文在SC32API.SCL ABI1.4/v6。旧SCAPI、IMAGEREQUEST/RESULT/CLAIM/SUBMIT/CANCEL和既有解码器边界不变。新普通应用不包含SCAPI、私有WIRE或软中断。

路径`ScImageDecodeBegin`打开实际本人可读来源；`Query`非消费，返回READY后`Copy`成功才消费，或`Cancel`释放。所有记录version1，未使用flags/reserved为0。票据只属于本库实例与实际调用进程；内核服务另核真实owner及worker PID/generation，不接受应用指定的owner。每个owner沿旧服务只有一个正在服务的压缩请求，BUSY明确失败；SCB校验对象由动态链持有，不引入固定请求条数。

SCB1/2先校验尺寸、格式、精确全长；每个Query读取至多64KiB，SCB1检查全部槽0..104。旧原DAC快照转换，不用UI主题色替换图像色。查询不为壁纸验证申请/拷贝完整ARGB图。压缩/BMP仍由原Ring3 IMAGE.SCX真实解码PNG/JPEG/WebP/BMP，静态WebP与原1920×1080/16MiB源边界保留，alpha完整。支持性由真实解码结果决定，不按后缀冒充验证成功。

PENDING也输出48B Info。MORE_WORK=2说明有可继续的R3 SCB校验，progress/total是真实body字节；SERVICE=4、wait_events=SC32_EVENT_IMAGE说明等待内核服务，不给未知解码进度假数字。READY=1表示实际完成且有真实width/height/stride/bytes；Query保持票据与服务像素。路径每步复核路径generation、文件object/revision/长度；来源变化报STALE。需要从Ready到其它应用动作保持同源时，调用者持有公开READ_STABLE lease；它兼容只读打开，阻止写入。Cancel最终释放服务票据、文件及库页。

内存入口`ScImageDecodeMemoryBegin`只建库对象并探测本人映射，借用`data[bytes]`；不一次复制16MiB、不落盘。调用者保持来源存活且不变到Step输出INPUT_SEALED=8，或请求失败/Cancel。`ScImageDecodeStep(request,1..65536,&info)`最多搬运预算字节，Query本身不搬运内存输入。封存前MORE_WORK/真实源字节进度；封存后只读内核自有页，库忘记借用指针，SERVICE/IMAGE事件等待。来源只有完整收到才入worker队列；上传单独FIFO/30秒期限，半来源不阻塞路径请求。新私有0xA00..A04只供库/IMAGE服务，旧0x7c..0x80仍原样。

内核每块重新检查当前调用者页表，不保存跨调用用户指针；worker只能领取自己被指派的source kind/bytes/path，并读取自己票据的封存字节，不能指定其它owner或内核地址。取消/失败/超时/进程停止撤worker链接并释放输入，异常保留真实暂停诊断卡；提交完整解码后先释放封存输入再申请结果页，降低峰值。服务异常仍按普通Ring3边界处理，SYSTEM外部管理边界不变。

排队的IMAGE worker沿统一SCX装载器启动；装载事务必须在IF=0独占共享stage，render_hold只阻止任务切换，不能代替该IF合同。服务保存进入装载段时的IF，在受委托身份下创建worker，并在同一关闭IRQ区发布owner/worker PID与generation、票据及队列状态；成功和失败都在关联收拢后恢复原IF。PNG/JPEG/WebP/BMP解码仍在独立Ring3 worker，不把解码移入内核，也不放宽loader。旧服务启动负值仍归一为raw -3，公开适配仍映射NOT_SUPPORTED；该返回本身不能证明压缩文件格式不支持，须区分worker未启动与解码器真实拒绝。

Copy先预检完整目的RW区、容量及输出不重叠；SCB按64KiB复核/转换、服务沿旧完整消费语义。IO/来源变化时目的候选可能已有前缀，调用者不得把失败候选发表为当前图。Info与票据在成功消费后只作已完成值，不能再次Query。失败请求仍需Cancel，避免保留诊断资源。

本草稿宿主/客体证据见STATUS.md；源码实现不等于guest验收。

修订：2026-10-16，统一Canvas/Lens/Settings/Sound唯一图片合同，明确分片内存封存和异步等待。

修订：2026-10-16，修复IMAGE服务以IF=1进入统一SCX装载器的冲突；保存/恢复原IF，关联发布后才重新允许IRQ，公开ABI与原盘图片不变。
