# 关联打开的单一路径参数

GUI02 的 Notes2 关联打开失败与真实源码一致：SCSHELL 的 MODERN profile 用 `sh_argument` 包裹 document 的一对双引号，`ScProcessArguments` 返回的仍是原始参数串；`NOTES2.C` 原入口把带双引号的整串直接交给 `n_load`。截图确实显示 `-103` 和 `"/HOME/P6GUI/NOTE.TXT"`。这没有证明文件本身不可读；同一文件的 durable 保存和正文读回已经由 Root 另行取得。

五个正式目标入口已经逐个审阅。**本补丁只改 Notes2 一个文件**；其余四个入口/实际 consumer 的 before/result 摘要仍相同，全部列在 `PINS.json`。

| 组件 | 实际启动消费者 | 审阅结论 |
|---|---|---|
| Files2 | `files2_view.inc::f2_start_argument` | 已解析引号与单一路径；不改 |
| Notes2 | `NOTES2.C::main` | 原始参数直接 `n_load`，本次修复 |
| Lens2 | `imageapps/lifecycle.c::image_main` | 已处理可选 `--open` 和一对外引号；正常 MODERN 关联不需要此修复 |
| Sound2 | `SOUND2.C::sound_arguments` | 已严格读取一个路径和两种转义，拒绝尾部额外参数；不改 |
| Canvas2 | 与 Lens2 共用 `lifecycle.c` | 已处理外引号；不改 |

当前公开 SCLIB.H 没有 argv/原始命令尾解析函数。这里没有冒用 ConfigParse/PathNormalize 充当参数解析，也没有扩成新参数库。现有 Files2/ImageApps 的其它语法未在本轮重写或宣称全部正确；本轮目标是实际裸串消费者与 MODERN 关联的错接。

Notes2 新增一个私有、原地压缩的单路径解析函数：外层空格/制表符只作参数分隔；接受一条裸路径，或严格成对的双引号路径。引号内仅解码 SCSHELL 真正生成的 `\"` 和 `\\`；不拼接多个 token，不任意删除内部引号，不执行命令。`"/HOME/P6GUI/NOTE.TXT"` 解成原路径，`"/one" "/two"` 或未闭合引号返回 INVALID。最终文件名、UTF-8、长度和权限仍由原 `n_path/ScPathResolve/ScPathQuery` 路径负责；不放宽文件权限。SandFS 当前仍拒绝路径分量里的空白、反斜杠和冒号，本补丁不改变它。

无参数仍沿原 `/HOME/notes.txt` 可选新文档行为。参数 API 失败或语法错误时只显示明确错误，不对半解码的字符串调用 `n_load`。原来的 UI、候选读取、保存、lease 和退出流程都保持。

交接文件为 `notes-single-path.patch`、`overlay/user/notes2/NOTES2.C`、`PINS.json`。`prepare.py` 从固定原像生成这两份文本并核对当前 GUI02 源 pins；它只写本目录，不改主树、不构建、不启动 VM。

没有新增公开函数或动态导入，HOST/NATIVE manifest 都不改；`strlen` 本来就在 Notes2 的实际导入清单中。Root 整合后只按真实闭包刷新 NOTES2 的 SCO、SCX、map、SCDB 和 SDK 对应源；不把旧 map/SCDB 配给新字节，也不重跑其它四个应用或系统库。客体结束条件是用正常 Files2 关联打开已保存的同一份 NOTE.TXT，显示并读取原正文；本草稿没有声称修后运行成功。

本次只完成源码审阅、固定原像/GUI02 摘要核验、文本补丁和 Python 语法核对；未编译、未运行 parser 测试或 VM。主树与当前 GUI02 的输入保持原样。
