# 共享 FileDialog：类型显示与新文件保存最小补丁

2026-10-16，准备后冻结，待 Root 在当前 VM 结束后整合。只读实际 Studio 截图和冻结源；没有修改 main/其它草稿、应用补丁、构建或启动 VM。

`file-dialog.patch` 一次修改3个文件：ui_dialog.c、SCUI.json、SCUI.NATIVE.JSON。`pins.json` 是精确原始字节 SHA，`result-pins.json` 是正式 pinned 工具保留原行尾规则下的结果 SHA；不能再额外重复应用 Save Query 修复。`prepare_patch.py` 只在本目录生成补丁和小凭证，固定原像不符即拒绝，不自动重定基；PREPARATION.json 记录已做内存补丁回放核对，并不表示编译/运行通过。

类型列只改私有 provider 的 field2：返回借用静态 UTF8 字符串“文件夹”或“文件”，列 format 同步为已存在的 SC_UI_FORMAT_TEXT。没有改 ScDirectoryEntry.kind、UiDialogRow、provider revision、稳定行 ID 或公共 API。`ui_dialog_row_compare` 仍直接用原始 kind 做目录优先，再按原文件名排序；过滤、进入目录和权限/选中文件逻辑同样读原始 kind。字段只供 FileDialog 自己的表视图，不是公开目录数据合同；静态字串生命周期覆盖 provider 借用期，不引入分配或回收。

同一补丁纳入已确证的 Save 修复：包含公开 SC32PATH.H，保存确认用 ScPathQuery 区分精确 NOT_FOUND，保留其它错误/非文件失败分支；两份严格 manifest 只将唯一 ScPathMetadata import 换成 ScPathQuery。不得把旧 Metadata 的 FAILED/ACCESS 普遍当作不存在，不改旧 SCAPI/PathMetadata、Notes或文件提交权限。现有 SC32API 已导出 Query，未新增 API 或改变 SCUI ABI/版本。

从 projectos 由 Root 应用时，可使用现有严格工具（receipt 路径必须是 Root 选择的全新 build 文件）：

```text
python -B sandcore/tools/apply_pinned_m10a2_patch.py --patch sandcore/drafts/m10a2-file-dialog-display/file-dialog.patch --pins sandcore/drafts/m10a2-file-dialog-display/pins.json --pin-root sandcore --receipt sandcore/build/ROOT-NEW-FILE-DIALOG-APPLICATION.json
```

本目录没有执行该命令。原像或版本发生变化时先审查差异，不模糊应用。Root 后续只需重生成受影响的 SCUI HOST/真正原生库、按实际 SCO核manifest，再更新依赖其字节的 SDK 来源与 LOGIN 发行表/CORE；未改应用原生成证据可复用。修后实际结束条件是同一共享 Open 表显示文件/文件夹且导航/排序保持，新文件 Save As 能交给原 Notes 条件保存链。客体、视觉和保存修复结果都尚未验证。
