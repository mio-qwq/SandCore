# IMAGE worker装载IF冲突

GUI03截图 `p6-gui3-notes-default.png` 的实际错误是Lens压缩图片服务启动失败。Lens无参数默认源为 `user/imageapps/lifecycle.c:227` 的 `/SYS/PICTURES/WELCOME.PNG`；不是 `/HOME/WELCOME.SCB`。

实际链路均与HOST09输入摘要一致：

1. `kernel/main.c:314,321` 关闭IRQ后调用image_service_poll。
2. `kernel/image_service.c:245` 原启动行却先sti，再调用auth_service_exec。
3. `kernel/task.c:1315` 的task_loader_stage明确拒绝IF=1；parts在1355行因此固定返回-4，尚未读IMAGE程序或进入解码器。
4. `kernel/image_service.c:246` 把worker启动负值归一为raw -3；`user/imageapi/sc32_image.c:35` 映射为SC32_E_NOT_SUPPORTED=-112。SCGFX保留该cause，Lens显示Image operation failed (-112)。codec_decode自身只用-4/-5拒绝格式/内存，不会产生这条-3。

原盘两源均只读检查，没有改图：WELCOME.SCB为640×360 SCB1、230424B、精确长度、最大槽74，属SCGFX完整直接读取路径；WELCOME.PNG为1672×941、8bit RGB、1715161B，在正式尺寸/源长度范围内，完整压缩流解码尚未执行。GUI03 session.json的实际data_preparation记录IMAGE.SCX已安装为97886B、SHA `07369c3bcb3bf58a9ca0c16e5c653aaefdd28cac78fa9105e33d5ef8cb5ea0a3`、metadata[-1,-1,23]，与HOST09精确输出一致。因此不改默认路径，不用SCB绕过PNG，也不修改loader或decoder。

`image-service-loader-if.patch` 仅改变worker装载/发布段。先保存进入时IF并cli，沿原render_hold和受委托身份执行统一SCX装载；失败收拢或成功发布worker/owner PID、generation、ticket及队列状态之后才恢复原IF。编译器memory屏障保证关联写入不会越过重新允许IRQ的边界。原IF=0保持关闭，原IF=1在两个出口都恢复；其它早退路径不变。解码仍为独立Ring3任务，公开ABI、错误映射、超时、取消与SYSTEM边界不变。

`sc32-image-spec.patch` 只在SC32-IMAGE规范补上述装载/IF生命周期及修订记录。两个文件的固定before/result SHA见PINS.json；overlay供回读，合并用局部patch，不整树覆盖。主树、当前SDK/CORE、旧报告及GUI03 VM均未触碰。

Root正常结束当前GUI后再合入并最小重建CORE。该代码不改变任何用户目标、公开头或SCO imports，不要求重生12用户目标。对应新HOST/CORE/SDK来源按Root现有严格流程记录，旧HOST09与GUI03失败证据保留。必要回归只需当前真实PNG进入worker并完成显示、正常关闭与原资源账；本稿没有构建、VM调用、完整PNG解码或修后PASS。

修订：2026-10-16，定位GUI03真实-112并准备单处kernel修复及SC32-IMAGE规范增量，保持原IF保存/恢复。
