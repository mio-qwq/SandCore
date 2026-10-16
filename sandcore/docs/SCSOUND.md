# SCSOUND 用户态流式解码库

2026-10-16。新增真正Ring3 `SCSOUND.SCL`，公开头为 `SCSOUND.H`。五个真实导出为Open/Read/Seek/Query/Close，依赖SC32API ABI1.4/v6和SCCLIB。主树已实现并通过HOST容器构建，尚未客体/播放验收，不能把它称为Sound2界面已完成。

## 格式、权限与内存

沿原 `user/audio/decoder.c`、`flac.c` 的完整WAV、dr_mp3与dr_flac标量核心，旧源原字节保留，私有适配记录在 `user/scsound/ADAPTATION.json`。不另引第三方实现；原厂商头、完整许可和归档继续沿THIRD-PARTY。新库不包含SCAPI或私有寄存器桥，实际19个导入均来自SC32API/SCCLIB；分配、复制和释放复用标准库。

Open先用精确revision取得READ_STABLE和真实现代FILE，再让header解析与全部解码回调使用同一固定来源，没有stat后重新打开的换源窗口。长曲流式读取，不复制整曲。每对象持有16KiB scratch供WAV/mono转换，避免把16KiB临时数组压入普通应用栈。多个文件动态拥有；耗尽明确失败，不设全局固定文件数量。

MP3和32位浮点WAV在需要能力前查询内核已启用的x87状态；编译禁用SSE/MMX，成熟核心仍保NO_SIMD分支，不把CPUID宣称当OS已保存现场。解码器沿既有用户授权宿主编译特例，普通GUI/worker仍用公开C接口。内核继续无浮点/libc，没有把该特例扩成标准库浮点声明或SCCC已支持全部64位运算。

## 公开合同

OpenDesc为32B/version1：path、flags0、initial_seconds、reserved[3]。成功才输出对象；初始定位在候选解析后、发布对象之前执行，失败不触碰旧对象。initial_seconds是本次未发行新合同的明确字段，不借旧SC32忽略寄存器扩展行为。

Read每次1..2048帧，总是双声道有符号16位PCM，需要frames×4B。成功返回实际帧数；0是明确EOF。文件/权限/CRC/解码错误不是正常曲末，失败不改out_frames。源声道、位数、真实文件大小/revision、PCM位置/总帧与状态由132B FileInfo查询；MP3未知长度和无总帧FLAC不伪报已知时长。已取得的来源快照不是“当前磁盘又查询过”的断言。

Seek使用另一个固定来源的解码候选。成功才交换解码器/FILE/lease并重绑上游回调，失败保留原位置/曲目；不复制整曲。此操作可能扫描真实编码帧，应放在受管worker，不能在GUI回调里同步等待。Open(initial_seconds)避免新worker先Open再重复创建seek候选。

Close成功消耗对象；PENDING保留对象供下一次Close，失败不丢现代资源。内部失败候选由明确垃圾链保留，每次公开操作最多推进一个关闭，不无界轮询。正常fini先关解码器/FILE/lease再释放scratch；解码断言/私有宽除零已标记损坏时不调用可能破坏的析构，交独立ROOT退出栈和内核owner链回收，不假称执行完了正常fini。

## 应用接续与验证

Sound2已写入主树 `user/sound2`：SCUI菜单/工具栏、真实PCM峰值记录、音量、按秒跳转、迷你和最大化/还原；受管worker的当前/候选角色各自为普通进程。候选打开/定位不阻塞原播放和GUI，成功后旧声音先STOP并ACK，新声音才恢复。取消按真实job票据处理，Wait成功消费票据后不再Close旧ID。PCM部分提交保留尾部；EOF后仍等设备真实排队耗尽。MP3/FLAC标题/内嵌与旁置封面的编码提取仍在独立草稿接续，尚未形成完整播放器产物。

私有IPC固定820B状态记录、最多4096B封面分块，不传跨进程指针；endpoint使用本人PID/代数/SID/nonce，真实sender与受管job核对。封面内存分配或解码失败不停止音频。内嵌解码失败只请求一次旁置回退；压缩图片交同源SCGFX分步解码，不落临时文件。暂停/结束后无固定画面刷新定时器，隐藏只停GUI帧，音频/作业继续。GUI与worker现仅通过严格i386宿主对象编译，尚未证明客体播放、视觉或回收。

HOST结果只证明构建与严格容器/导入成立，不能证明真实PCM、设备播放、同曲seek、取消、封面或最终回收。后续针对新FILE适配/候选交换做一次代表性三格式与失败/回收检查，再纳入同批客体流程；不重启旧M8或扩大无关矩阵。

修订：2026-10-16，共享流式库、固定来源/候选seek/堆scratch/退出合同；新增SCUI播放器与普通worker状态机，封面整合及真实客体验证仍未完成。
