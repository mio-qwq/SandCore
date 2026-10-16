# M10a2 GUI04 实际审阅与后续修复

2026-10-16，完整 M10a2 目标仍 active。本页区分本轮已经运行的字节、实测失败和修后待验；不是整个版本的验收结论。

## 已结束的真实会话

`build/m10a2-p6-gui-run-04/p6-gui.json` 状态为 `P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING`。exec33941 正常退出0，Windows QEMU16116已关闭。实际运行组合为 HOST11/native05/final05；全部冻结输入、原 M10a1 数据盘保持，changed_input_output_paths为空。

实际恢复原普通 SID1，注销测试 SID4。原 native host 的 PID/代数保持，完整 owner/GC/资源来自同一次暂停的一致快照：PF13948→13948，physical_delta、attributed、unexplained均0，unexpected=[]，所有资源池差额0。GUI01历史205页未归属仍保留，不作豁免。这是本轮资源账通过，不能覆盖业务失败。

最终会话盘 SHA256 为 `8598196f5eab84506746ce693c95c8eb18b5ac09cbe36e8a2cb30bee178e407d`；它是保留本轮普通用户操作的临时盘，原盘未更改。

## 实际新增成功

- Studio普通用户真实生成并持久发布RDSTATS.SCL及四独立TU的RDESK.SCX。两套实际REC的头、五帧CRC、三个DONE/stage3/DURABLE、路径/长度/generation与实际get一致。ResearchDesk实际原生运行，通过动态SCL计算4行、总和40、均值10、最小-4、最大32。
- Studio→Debug2首指令、TF单步、Continue/Pause、7模块/1509验证符号、实际INT3命中1次及撤除、目标正常exit0与Debug/Studio正常退出已查看。详情和完整产物在 `evidence/m10a2-p6-gui04-sdk`。
- 默认Lens实际解码并显示原WELCOME.PNG，1672×941，ARGB32/6293408B；没有用SCB替代。Settings缩放100%预览超时后实际回到150%，状态文字也结清至预览0秒。
- Classic主题实际保存、应用，桌面壁纸、图标、窗口及SCUI控件可见变化。
- Sound实际打开原有 `/TMP/LONG.WAV`，48000Hz/2声道/16bit，首次EOF后CtrlR重播成功；真实界面播放中0:06/0:12。输入4秒并跳转后真实播放中0:08/0:12。QEMU短PCM捕获260063帧/5.417979秒、517958非零样本、peak10080；WAV1040296B，SHA256 `7af3f5e4d8220f0616610a315ad3de8f248950450477703e372532f51084ea21`。录音已经停止，none后端不作宿主可听声明。Sound正常X关闭。
- Files向空桌面拖动实际新生成的RDESK.SCX，确认窗口显示取消、复制到目标、移动到目标和创建快捷方式四个完整按钮，最后一个自动换行；原默认取消行为保留。本项只证明FLOW画面，未重复文件复制移动业务。

终报告、PCM及代表声音/Classic/诊断/FLOW画面保存在 `evidence/m10a2-p6-gui04-final`，其中SNAPSHOT明确保留以下未通过项目。

## 实测问题与源码原因

Settings选择程序页后不重绘；最大化只改变WM外框，客户区仍旧。正常关闭后由Debug2重新拥有启动的Settings在没有预览/换主题的情况下复现。真实ScUiTabSelect入口软件断点表明输入正确：context40056040、tabs5、page8；不是猜坐标。真实DebugMemory读取UiContext错误头为code/cause/raw均-308、operation8(UI_OP_PROVIDER)、node51。

源码确定程序、环境、安装三张私有平表均将CHILD_AT的index误读为item-1，item0下溢返回END；SCUI将paint中的非零结果报告BUSY，旧帧保留。INDEX_OF也误用value.lo，实际合同通过reply.total返回序号；reply.state还缺VISIBLE/ENABLED。修复只改这三张应用表，不放宽SCUI、ABI或权限校验。

Settings/Installer共用message/property创建后立即Release唯一引用，导致OPEN窗口立即取消；error还将ScShellDialog错误当作ScUiDialog。已改应用持有notice，完成时先清指针再Release，关闭应用时先Cancel/Release；错误统一用本应用持有的SCUI消息框，并删除不再调用的旧Shell错误框导入。

Files的四按钮确认点击取消后，来源Files窗口也消失。随后真实ps确认Files进程已不在；没有正常X关闭动作，原因尚未确定，不能记为正常退出或取消业务PASS。没有因终账0而忽略此行为。下一次只针对这个具体现象复现并查看真实退出原因，不扩展拖放矩阵。

## 修后生成状态

`drafts/m10a2-settings-provider-fix/integration.json`记录10个实现/清单的真实前后SHA。新增私有app_provider.inc统一COUNT/CHILD_COUNT/CHILD_AT/ITEM/CELL/INDEX_OF边界；内部HANDLED只用于解析器，回到公开Provider时转换为0。

SCO13真实SCCC宿主IO后端及严格SCLD两项通过，正式两份NATIVE清单由实际SCO导入表提升。HOST12实际exit0，41产物，源码前后变化0，完整报告SHA256 `7e29017e4c20095c0cf8ec2e5bbcd9ae90626fe974d12dfae4596d641548e750`。这不是客体修后业务通过。

当前native06正在使用新 `drafts/m10a2-p6-followup-refresh-v5/driver.py` 明确刷新SETTINGS2、INSTALLER2。产物继承native05，编译运行底座明确复用final04/HOST10/native04；SCCC、SCLD、LOGIN2、Desktop、worker与05摘要相同，冻结八库只装04字节，不把新SCUI装进旧CORE。layout02保留真实编译来源，仅按逐字节相同的五结构头复用；85项副文件继续追溯真实get和完整继承链。主源和当前运行helper冻结至VM退出。

原生生成后再形成对应新CORE/SDK，定向验证设置的程序/环境表、属性框、Installer实际文件安装，以及Files取消异常。既有SDK/Debug/PNG/Sound成功不重复；普通会话回收、最终产物和完整合同交付继续。

修订：2026-10-16，保存GUI04唯一最终owner账、声音重播/seek/FLOW实测、三个Provider与共享对话框修复和Files取消未决行为。
