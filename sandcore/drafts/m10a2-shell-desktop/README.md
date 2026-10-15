# SCSHELL.SCX 桌面正文

2026-10-15，隔离源码正在收齐；未整合、未编译、未运行。完整M10a2目标仍在继续，本目录不是验收包。历史桌面、用户盘和当前P4验证输入未被替换。

实际正文分为模型、绘制/输入、任务栏、动作/对话框和服务主循环。应用只包含正式公开头，调用真正SCSHELL/SCUI/SC32API/SCLIB/SCCLIB动态导入，不含SCAPI、int指令桥、NUI或内核结构。

当前已写内容：

- 可信host PID/gen/SID核对，真实SCUI desktop/taskbar两surface、SESSION listener、Prepare、首帧Pump、Activate、ServiceStep与正常Deactivate清理。
- 动态图标/任务/菜单工作对象；旧LNK读取、真实图标资源和区分目录/文档的主题回退；壁纸继续由既有单份缓存合成，桌面surface透明。图标读取在Paint外每轮4项，只强持当前视口，离屏释放；SCSHELL独立有预算cache，失败保主题回退并记录真实原因。
- 路径与显示名分离，单选/Ctrl/Shift/框选/方向键/Enter/双击，真实capture与取消，拖动候选直到UP提交，96基准自由/网格/自动布局与账号CAS保存。
- 空间hash索引、damage范围查询、所选拖动索引；目录每轮8条候选读取，完整校验/索引成功后才换活动模型，失败保原快照。
- 同SID真实窗口分页与完整候选替换，滚动overflow、focus/restore/minimize/maximize/close-request、cascade/tile；既有角色绑定变化事件唤醒，空闲不反复枚举。
- 多级Start/上下文菜单复用SCSHELL，菜单消失后仍保异步作业寿命；真实剪贴板Copy/Cut/Paste、Drop Copy/Move/创建PATH快捷方式的明确选择、属性编辑和错误/注销确认。实际WM hit/z查询决定跨窗口拖起，Alt可显式FILES拖动；选中LNK传其自身路径，不能把目标当待移动文件。
- Catalog记录的最后确认文件revision用于保存，外部SID改变不能被一次META盲接受而覆盖；坏配置候选保SYSTEM种子并明确日志。

布局冲突给“保留当前画面，暂不保存/重新加载磁盘布局”；不会盲读新META后覆盖另一个登录。水平/竖直视口保原逻辑坐标，方向键自动滚到焦点，Shift滚轮横向；主题/font/display变化排有界重载，隐藏暂停图片IO，IPC/job继续。真实timer/drag下一期限决定混合服务等待，没有额外永久1tick轮询。

直接shortcut创建/改显示名/改参数已有真实属性表，PATH/程序/登记应用类型分开；普通文件rename的已确认结果会迁移布局身份，LNK改名只改它的显示label。主题taskbar高度走真实Workspace候选，legacy文本/图像关联沿已存在的Files路由保留，未知二进制不当文本。同步磁盘保存已替为StoreBegin/Step/Query/Close：每轮短步、真实READY才继续让出；服务/菜单/job也按真正有界队列余量READY继续，不永久高频poll。底栏“未保存”到当前布局的真实durable ACK后才清。

仍需在本批收齐、静态收尾或正式整合时落实：全部权限/显示尺寸/字段/命令/退出路径审阅，以及SCSHELL library最终公开头/manifest和worker的一致闭包。所有键鼠、画面、延迟、回收与持久化仍未验证，不能从源码声称验收。

`make_manifest.py`只做静态调用与公开头映射，生成候选应用manifest和逐文件摘要；不是预处理、编译或运行证据。`DESKTOP.mk`只是未来整合片段，未被当前Makefile包含。最终CORE发行摘要必须消费真正构建出的SCSHELL.SCX，没有默认或全零放行。

Phase5全部实现/资源/规范收齐后统一构建、实际G1编译及最小必要Windows双盘QEMU验收；不逐组件扩大矩阵。
