# Files 2 原生组件整合入口

当前是**已实现、宿主构建通过、未客体验收**的真实 Ring3 应用。代码只存在于此独立目录；原 `user/files.c`、默认镜像、主 Makefile、内核和其它应用没有修改。设计在 `DESIGN.md`，证据口径在 `STATUS.md`。

## 文件职责

| 文件 | 职责 |
|---|---|
| `FILES2.H` | 完全大写的组件私有头、模型、资源归属与跨模块声明 |
| `FILES2.C` | 模块聚合和事件驱动主循环 |
| `files2_model.inc` | 完整路径/修订身份、分页导航/子树搜索、分步排序过滤、虚拟表格、按需目录树与关联图标缓存 |
| `files2_storage.inc` | 书签配置候选/条件持久发布、新建空文件、租约清理及恢复路径 |
| `files2_actions.inc` | 关联启动、Edit/Debug、共享菜单/文件对话框、剪贴板、受管文件作业、冲突与跨窗拖放 |
| `files2_view.inc` | 标准控件树、菜单/工具栏、键鼠/布局/隐藏处理、目录及作业调度、服务重连、退出资源释放 |
| `FILES2.json` | 按真实 i386 未定义符号生成的精确动态清单 |
| `files2.mk` | 可选定向构建片段；不会自动覆盖旧应用 |
| `check_host.py` / `MODEL_HOST.C` | 宿主对象/容器/导入检查与实际索引步进器的独立 ASan/UBSan 对照 |

## 构建与安装接续

在仓库根运行：

```powershell
wsl.exe sh -lc 'cd /mnt/c/Users/Administrator/Desktop/projectos/sandcore && python3 drafts/m10a2-files2/check_host.py'
```

输出仅为 `build/m10a2-files2-agent-host/` 中的对象、动态 SCX、纯模型检查程序、日志和报告；不会构建内核、启动 QEMU、生成数据盘或覆盖正式包。需要沿既有 Makefile 构建时可以显式添加此片段：

```sh
make -f Makefile -f drafts/m10a2-files2/files2.mk \
  BUILD=build/m10a2-files2-agent-integration m10a2-files2-artifacts
```

动态依赖：SC32API ABI1.3 / library5；SCUI ABI1.1 / version2；SCCLIB、SCLIB、SCSHELL ABI1.0 / version1。全部函数经真实 SCL 导入；无旧 SCAPI、NUI、内联中断、私有 WIRE 或内核结构。

主代理整合时应保留历史 FILES.SCX 原字节，再把此容器安装为新的 `/APPS/FILES.SCX` 默认入口。`FILES2.SCX` 是宿主输出名；只在制作容器时要求它匹配清单。多选目录启动新窗口时使用正式 `/APPS/FILES.SCX` 路由，文件关联仍为 `sandcore.files` / `directory`。新应用同时接受裸路径和 SCSHELL MODERN profile 的一个双引号路径参数；当前 SandFS 自身仍拒绝空格路径。SCL、SCFILEWORK 和 SCUI Unicode/图标资源沿已整合的平台安装规则复用。

## 可使用的工作流

- 目录树展开、单击导航、地址回车、上级、后退/前进和历史菜单；书签在账户配置路径持久保存。
- 多选详情表格、表头排序、拖动列界调整宽度；当前目录实时过滤、可取消子树名称搜索、完整路径区分同名结果。
- Enter / 双击打开；公共目录/文件选择器；Open with 使用共享应用目录；明确文本/源码交 Studio、图片交 Lens、SCX 运行或交 Debug，未知二进制不自动保存为文本。
- Ctrl+C/X/V 使用会话隔离的真实 FILES 剪贴板。F2、Delete 和新建目录使用共享 ShellMenu 的候选事务、进度和冲突处理；新建空文件使用公开租约的条件发布。
- 从详情行拖到桌面、另一 Files 窗口或本窗口目录；目标反馈，复制/移动确认和取消；认证后的 FILES 正文由接收应用拥有副本，真实操作仍重新核权限和修订。
- 共享属性窗口显示真实 kind/bytes/UID/GID/rw/revision 并按现有权限修改 rw；公共进度/冲突窗口分别处理替换、跳过、另名、目录合并、重试和取消。

快捷键还有 Alt+左右、Alt+上、Backspace、F5、Ctrl+L/F、Ctrl+D 编辑、Ctrl+Shift+D 调试、Ctrl+N 新建文件、Ctrl+Shift+N 新建目录、Esc 取消搜索。表格保留单键 D 调试入口。地址/过滤输入框的文本复制仍由标准 TextBox 处理。

## 失效与恢复边界

目录和搜索队列动态增长，没有 512 项截断；一个目录批次24项、一个索引批次1024步。排序过滤不读磁盘，鼠标移动只查询内存命中；树折叠节点不为画箭头而扫描。隐藏窗口停止绘制，文件作业、搜索、保存和服务重连继续；空闲用 BrokerWait 和实际 SCUI 截止时间等待。

当前公开 Directory API 提供快照 epoch/STALE，没有目录变化订阅。外部 CLI 或其它窗口的变更在刷新/导航时重新取批；本窗口文件事务的 durable ACK 会请求刷新，执行选区操作仍复核实际修订。关联/主题/字体/显示变化分别失效图标缓存，不重读整个目录。

只对 `DONE + DURABLE` 文件项宣布持久提交。失败、跳过、取消、来源保留和结果不确定分列；不自动重发不确定的破坏性操作。失败进度保留到用户关闭，恢复记录放在账户的 FILEJOBS 目录。书签配置最多1MiB，坏格式、并发修订冲突或保存失败保留当前内存书签和原磁盘对象。目标不存在只认 `ScPathQuery` 的精确 NOT_FOUND。

Windows 双盘 QEMU 下的真实文件操作、权限、键鼠、拖放、缩放、隐藏和资源回收尚待主代理统一定向验收。此目录及宿主结果不代表 Files 2 或 M10a2 整体完成。
