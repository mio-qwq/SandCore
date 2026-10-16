# GUI01 共享 Save As 阻断：只读定位

2026-10-16。只读源码、run03实际原生库/来源与 GUI01 截图，不修改冻结 main/helpers，不构建或启动 VM。截图中上方 /HOME/P6GUI、下方 NOTE.TXT，FileDialog 显示“操作未完成，请检查错误”且仍在；底层 Notes 仍为20B dirty文档。Root提供普通UID1000/SID4、目录已实际创建/可枚举和目标 NOT_FOUND 的事实。输入 SHA 见 INPUTS.json。

**根因是共享 SCUI Save 确认分支误用旧查询 API。** `user/scui/ui_dialog.c:812` 调用 ScPathMetadata，下一行却仅将精确 SC32_E_NOT_FOUND 当合法新文件。旧 ScPathMetadata 并不提供这个错误：

1. 新叶文件 /HOME/P6GUI/NOTE.TXT 不存在，`kernel/fs.c:433` 的 fs_stat 返回-1；fs_metadata 在第669行直接返回-1。祖先可遍历时 `fs_access_identity:646` 允许 META 查询缺失名字，因此不是目录权限拒绝。
2. `kernel/task.c:1094` 原 SYS_FSMETA 返回该-1，`user/sc32/sc32_path.c:23` 经 sc32_legacy；`sc32_support.c:69` 将非-5旧错误统一映射 SC32_E_FAILED=-116，保留 raw=-1，无法给出 NOT_FOUND=-104。
3. ui_dialog.c:813 的 NOT_FOUND 特判不命中，路径不进入选中结果，也不会执行第894行 ui_dialog_finish；第1251行统一错误文案正是截图。Notes completion 因而没有执行，其 n_save/lease 写入尚未开始，目标查询仍 NOT_FOUND 符合此路径。

**Notes和路径不是本次故障。** `notes_actions.c:54` 的 SAVE|OVERWRITE=18 是合法模式；OVERWRITE只影响已有文件确认，本次新目标不进入此分支。Notes 的 `notes_io.c:81-85` 已用 ScPathQuery，明确允许精确 NOT_FOUND，再走 ABSENT/EXACT 条件发布。`ScPathResolve` 只规范化路径，不要求最后名字已存在；/HOME/P6GUI/NOTE.TXT 只有20个ASCII字节，不触发路径上限。上方 Enter 的目录导航也已成功完成，因此不能用“创建目录或放宽权限”修这个保存错误。

**最小建议，供 Root 解冻修复时执行：**

- ui_dialog.c 明确包含 `../SC32PATH.H`，仅将保存确认处的 ScPathMetadata(full,&metadata) 改为 ScPathQuery(full,&metadata)，保留现有 NOT_FOUND、其它错误和非文件分支。
- 当前 SCUI 没有别处引用 ScPathMetadata；真实 run03 SCUI.SCL 的导入也只有旧 Metadata、没有 Query。因此 HOST/NATIVE 两份严格 manifest 应将该唯一 import 换成 ScPathQuery（function/size1/SC32API），最终以实际 SCO 引用再核验，不能保留 unused Metadata。
- 不修改旧 ScPathMetadata/SCAPI 语义，也不将所有 FAILED 或 ACCESS 当“不存在”。新 Query 在检查祖先 META 资格后返回精确 NOT_FOUND，ACCESS/IO 等继续真实失败，最终创建/覆盖仍由 Notes 的条件 lease 提交裁决。

run03 SCUI.SCL SHA为 `975e4dcb1ec4336486a8d8a8ee550183ab1895a0e7b6736c1f62cfa9a5d9c991`，它的 ui_dialog.c 生成输入 SHA 与当前源相同；真实 SC32API.SCL 已导出 ScPathQuery，公共依赖最低版本已满足。无需新增内核能力或修改 Notes/其它应用正文。SCUI ABI可保持；新SCUI字节生成后须更新对应 SDK来源及 LOGIN发行资源摘要/CORE，未改应用可复用原生成证据。

本结论只定位当前确定阻断，没有执行建议、重编或验证修后保存。原GUI失败记录和run03已成功原生产物保持；由Root安排必要的定向修复与实际保存出口，不扩新矩阵。
