# 用户态PCM与执行现场能力

2026-10-16。公开 `SC32AUDIO.H`、`SC32SIMD.H`，由SC32API ABI1.4/version6提供。实现位于库私有 `user/audioapi/sc32_audio.c`，复用原音频/现场内核入口，不改SCAPI.H、不增加syscall selector。此页不声称SandAudio重制或新接口客体验收完成。

## PCM设备和声音

`ScAudioDeviceQuery`返回64B/version1的设备快照：available、设备采样率/声道/位数、master音量、真实累计提交/完成/欠载/裁剪/中断和DMA排队。设备缺失时available=0是有效快照，不能当可播放；累计值是32位，调用方用无符号差分，不补造高位。

`ScAudioOpen`接32B/version1描述，8000..192000Hz、1/2声道，flags/reserved必须0。创建属于真实当前任务的声音，输出单个ScAudioVoice。沿原内核owner/任务代数和UID声道配额；同UID当前最多两路，混音槽/资源忙映射BUSY，不能称任意多音频流。旧数字错误保存在ScErrorInfo.raw。

`ScAudioSubmit`接1..4096帧、有符号16位交错PCM（每帧声道数来自该真实声音）。内核在同一陷入检查owner/声道和完整只读缓冲，再复制到自己的队列；不借用户指针作异步DMA。成功可部分提交，out_frames是真正消费帧数，后续必须保留未消费尾部；AGAIN和失败不改out_frames。不能假定一次提交全部成功。

`ScAudioQuery`返回64B/version1的声音快照：voice/rate/channels/queued/free/paused/finished/consumed/starved/volume。`ScAudioControl`支持STOP0、PAUSE1、VOLUME2、CLEAR3、DRAIN4；PAUSE的value0/1，音量0..256，其余value0。CLEAR丢弃排队并重设消费基点；DRAIN表示输入结束，不是已经播放完成，必须观察排队和实际状态。STOP以后原token不能继续使用。

`ScAudioEffect`沿现有六种系统音效及节流。`ScAudioMasterVolume`是独立管理操作，沿原auth_can_manage检查；普通用户不会因调用此函数取得管理权限。流音量仍由自己的Control设置。

所有版本化输出先验证完整声明的可写范围；成功只写规范前缀、失败不写、未知尾部保持。设备/声音各64B，Open32B。现代错误映射：旧-1 INVALID、-2 NOT_SUPPORTED、-4 BUSY、-5 ACCESS、-6 AGAIN，其它FAILED；ErrorInfo operation为1500..1507，与新Debug/Capture分组独立。

## 浮点与SIMD条件

`ScSimdQuery`返回48B/version1：ready、fxsr、sse2、CPUID特征、保存字节数与真实切换计数。ready说明内核已经启用x87及对应任务现场，不能只看CPU宣称的CPUID位就执行解码。sse2=0不意味着x87不可用。没有能力的分支必须在执行需要该能力的代码之前返回不支持；不得在查询函数本身使用SIMD。此接口没有改变内核无浮点规则。

当前MP3/FLAC旧解码源、标量核心及许可证继续保留。后续共享用户态音频库和Sound界面必须通过此公开层使用PCM/能力，不复制SCAPI桥；逐帧流读、候选换曲、文件修订、封面/标题、迷你布局、真实播放/隐藏/资源回收各自接线验收。

修订：2026-10-16，真实PCM/执行能力typed入口、布局/错误/配额/部分提交合同；新接口客体与应用重制待验。
