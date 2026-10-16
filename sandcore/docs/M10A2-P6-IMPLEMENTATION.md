# M10a2 主要应用与工具链实施状态

2026-10-16。完整目标继续 active。下列是实际主源整合与证据边界；没有宣告完整 Phase6、Phase5、SDK 或 M10a2 验收完成。

## 已接入主源

| 组件 | 实际实现 | 当前证据 |
|---|---|---|
| Files2 | 分页目录树/虚拟详情表、增量排序/搜索、历史/书签、关联/文件菜单、剪贴板与跨窗Drop、受管文件事务/冲突/属性 | 独立模型定向检查通过；主树HOST SCX 75076B，168导入；客体流程待验 |
| Notes2 | 共享Document/Edit、菜单、查找替换、Undo/Redo、文本剪贴板、稳定读取、私有候选条件保存及未保存确认 | 主代理真实SCCC宿主后端43238B；主树HOST SCX 24356B，79导入；客体流程待验 |
| Debug2 | 真实TF/INT3动态断点、反汇编/寄存器/内存/栈、模块与符号/断点表、分割/浮动面板；真实源映像/重定位/摘要登记 | 主树HOST SCX 62765B，102导入；解码器定向检查通过；新CORE/ROOT/SC32联合客体待验 |
| Calculator2 | 32位有符号表达式/优先级/括号/十六进制/错误守卫、动态历史、ANS、编辑与结果复制 | 主树HOST SCX 19684B，67导入；同源数学检查通过；客体待验 |
| Palette2 | 实际256槽/24角色、登记边界、排序/过滤/色样、HEX/RGB复制、程序壁纸 | 主树HOST SCX 18276B，66导入；新增壁纸查询/持久化与权限待客体 |
| Monitor2 | 动态分页任务、增量排序过滤、PID+代数操作、CPU/内存/卷、120点有效历史、五视图/冻结与隐藏策略 | 主树HOST SCX 32932B，80导入；客体负载/交互/回收待验 |
| SCO/SCLD | 真正独立TU对象、符号/重定位/静态隔离/聚合ABI、响应文件、SCX/SCL封装、map/SCDB调试sidecar | 同源宿主8项及真实i386执行通过；主树HOST SCLD 47268B/22导入；真实客体工具链待验 |

所有新应用只使用公开系统库。历史源码、旧SCX和默认验收盘保留；本批没有把HOST产物覆盖正式客体入口，也没有借旧程序的证据宣称重制应用已通过。

`m10a2-apps.mk` 提供定向应用目标，`m10a2.mk` 接入对象工具和调试/壁纸增量；不触发冻结游戏或完整历史构建。整合收据在 `build/m10a2-{applications,debug2,system-tools,object}-integration-01/`。Debug既有文件按13份精确原像SHA应用，新增文件另列收据；对象编译器四份原像同样固定。SCLD仅拆开18个误导缩进的复合行，C token流逐项相同，未改算法。

## 系统库与调试底座

SC32API ABI1.4/version6：原154入口保留，追加10个调试函数和2个壁纸函数，共166导出。ROOT ABI1.2/version3共12导出。主树HOST字节分别136110B和20361B；SCSHELL96、SCUI162、SCCLIB94、SCLIB163导出仍保留。SCAPI.H原SHA仍为 `81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1`。

调试入口使用独立900..91F私有selector族；现代断点动态拥有，不占旧八槽。可信SCX加载事务登记主映像；ROOT必须在首条用户指令之前由scl_boot登记，因为Adopt时其BSS已真实使用。其余SCL在init前核源文件/段/重定位，init后发布READY，释放/异常/退出撤销。ROOT通过自己私有桥登记，不反向依赖SC32形成环。合同见SC32-DEBUG.md。

公开SC32WALL.H的两个函数已接入主源。Set沿旧0x18保持权限与先写后改；Query使用新selector006取得本人真实view，无view返回NOT_FOUND。image-selected只表示配置路径，不证明解码或显示。合同见SC32-WALL.md。尚未将未来Capture、配置007或图片API声明当作现有166导出。

## 构建证据

`build/m10a2-main-components-01/host-build.json` 固定16个实际输出的摘要、389份构建依赖与清单。成功日志为 `m10a2-main-components-01-retry4.log` 和 `m10a2-main-components-01-compiler-wm.log`；早期缺包含目录、格式/缩进失败日志保留。SCCC宿主引导容器139409B/load139373B。它是HOST_BOOTSTRAP，不是新客体G1；对象代理的实际后端容量254319B及Studio include增量容量另有同源宿主后端证据，不混淆两者。

本轮按明确目标构建六个应用、链接器、六库及process/task/sc32_debug/sc32_core/scl_boot/wm相关对象；尚未把新调试底座的CORE链接/客体运行计作通过。不要将389份构建后输入pin冒充构建前后冻结检查。完整原生生成仍需实际来源链。

## 仍在实施

Studio2主功能已收齐：真实逐TU SCO/SCLD、工程输入资格、现代stdout/stderr捕获、取消/回收、多文档、着色、停靠、调试和条件发布，最后定向检查/冻结交付中。仅在其专属draft，尚未整合主树。

Canvas2/Lens2由图像工具草稿负责真正R3 SCGFX及共享Canvas控件，保原尺寸/图片格式/透明/异步解码和旧画笔；Settings2/Installer2保六页配置/预览回退/权限及真实包预检/收据/失败恢复。图片API唯一由图像代理维护，避免两份互不兼容封装。用户指定代理均为GPT-6.1 Sol/max，文件所有权分离。

终端还需要真实双向交互端点/PTY模型，不能用Capture的stdin EOF冒充交互终端；音频需保MP3/WAV/FLAC、封面/跳转/音量/迷你模式及许可证；登录/会话、完整SDK与复杂第三方原生示例、P5实际跨Files拖放/关联/窗口操作/会话回归及最终交付继续。没有删减完整合同。

修订：2026-10-16，六应用/对象工具/调试与壁纸底座主源整合及定向HOST构建，明确真实原生/客体验收缺项。
