# SC32 瞬态窗口、光标与滚轮

所属版本：SandCore M10a2 — The Application Platform Era。正式公共头 `SC32POPUP.H`，三项新增入口随 SC32API.SCL ABI1.2/version3。真实桥、WM、设备和构建接线已收齐，**本批尚未构建或执行**；原 GUI 证据不覆盖这些新行为。

`ScPopupCreate(desc,&window)` 接收64B、version1的 ScPopupDesc，字段依次为size/version、owner_window/generation、anchor_x/y、anchor_width/height、preferred_width/height、kind/flags、procedure/context、reserved[2]。锚点为owner客户坐标，宽/高为实际像素。新窗必须有真实本人owner且属于当前可见聚焦家族；无标题、无系统边框，整张ARGB画布就是客户区。位置由WM沿owner屏幕原点计算，正常下拉向下/不足时翻上，submenu沿popup右侧/不足时翻左，并裁入实际工作区。

Menu1、Combo2自动NO_ACTIVATE；Tooltip3自动NO_ACTIVATE|HIT_TRANSPARENT；Custom4按有效flags工作。宽高1..1920/1080，锚框宽高0..1920/1080、坐标-32768..32767；未知flags/保留字、跨owner或失效代数明确失败。Menu/Combo不允许transparent。真实CREATE→R3过程→ACTIVATE、拒绝/OOM回滚共用窗口服务；用户procedure只在Ring3调用，新popup过程先经真实CallbackPin保持代码到进程结束。错误不发布半窗口。

Popup只参加合成z序，任务栏仍只列普通窗口。OWNER链固定，普通窗口不能挂到popup，popup不能用SetOwner重绑；多层子菜单沿真实链从叶关闭。owner关闭、隐藏、移动/resize、焦点切家族、会话切换、主题或显示变化撤popup。NO_ACTIVATE将有序客户输入送回owner root，坐标允许落在root画布之外，由SCUI实际popup树处理；transparent不挡下面窗口。Custom可以实际激活，Menu/Tooltip不夺键盘焦点。ScWindowInfo的新state位分别为POPUP1024、NO_ACTIVATE2048、HIT_TRANSPARENT4096，未改老字段或reserved意义。

`ScPaintCancel(paint)` 使用原64B paint值记录，核本人window/gen、geometry及当前claim；仅撤claim，保留原损伤与其间新损伤，真实重新请求PAINT。它不是End的别名，不确认未成功绘制的区域。字体/OOM/user paint失败可显式Cancel，后续按deadline重试，不能因此无限忙绘。

`ScPointerSetCursor(window,shape)` 只接受本人live/eligible/focused窗口，shape0..9为Arrow、Text、ResizeH/V/NWSE/NESW、Hand、Forbidden、DragCopy、DragMove。WM保留原箭头，新增形状独立逐格画在同8×13预算；DPI放大仅用于光标像素形状，不放大客户画布或字体。显示从实际命中/捕获/聚焦状态选择，焦点撤销回原箭头；无资格不会修改别人的全局光标。SCUI在hover转换恢复Arrow，再设置实际控件形状；拖动capture的预览形状保持独立。

真实PS/2 200/100/80采样协商、F2 ID3/4成功后才用第4字节及置WHEEL能力位；ID0保持原3字节设备。半途失败以真实reset/ACK/BAT/ID确认恢复，不能猜包长。等待AUX回复时真实键盘字节仍交旧解码器，未捏造输入。ID3提供竖轮，ID4按实际包解横/竖；旧三个buttons字段保持。

WHEEL消息21是有序设备事实：data0/1为客户x/y、data2为竖detent（正向上）、data3为横detent（正向右）、data4/5为buttons/modifiers。单纯轮包不虚构MOVE，移动/按钮先于同包轮量；轮是合并屏障，具有原真实tick/sequence/input_epoch。菜单与视图读vertical2，不能与Button的changed3混用。GUI/IPC组合睡眠走ScBrokerWait的INPUT|STREAM，没有未生产的Broker GUI消息。

修订：2026-10-15，登记三项真实入口、64B布局、无标题owner/z序/任务栏、claim取消、九形状与真实PS/2有序轮协议，待统一客体验证。
