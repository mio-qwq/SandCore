# 程序壁纸增量接口接线

本目录是**已做组合语法检查、未进入主库/客体**的独立草稿。根代理已同意SYS300 selector `0x006`；当前core使用0x01–0x05，原GUI/FILE/Broker/Font/Lease/Host分组保留，避开Debug2的0x900及Studio CaptureSpawn组。ErrorInfo的operation使用0x1100/0x1101，它们不是selector。主版本由Root统一升级为SC32API ABI1.4/v6，本目录没有修改主SC32API.H或JSON。

1. 将 `../SC32WALL.H` 原字节安装到公开user/SDK头目录；普通应用只包含它。
2. 将 `SC32WALLWIRE.H` 和 `sc32_wall.c` 放在私有 `user/wallapi/`，在 `SC32API.C` 聚合包含 `../wallapi/sc32_wall.c`。WIRE不进入普通SDK。
3. 在 `kernel/wm.c` 的真实view定义之后包含 `wm_wall_snapshot.inc`，同批在 `wm.h` 声明 `int wm_wallpaper_snapshot(int pid,u32 *mode,u32 *session);`。该函数按 `session_task(pid)`查本人实际view，不借active_view或缺失默认值。
4. 在 `sc32_core.c` 包含wm.h、theme.h、公开SC32WALL.H和私有WIRE；在switch之前包含 `sc32_wall_query.inc`，添加 `case SC32_W_QUERY:return wall_mode_query(pid,request,output);`。现有task.c的SYS300 default已经进core，无需新增业务分发组。
5. 将 `exports.pending.json` 的两个函数并入同一SC32API ABI1.4/v6导出与Root当前构建规则。此JSON不是完整主库清单，不能用它覆盖原清单。Palette2精确manifest已声明这一依赖；当前v5缺导出不能误称已能运行。

`ScWallpaperSet`只接受0沙丘、1夜色、2暖沙；无效mode返回SC32_E_RANGE。有效值复用旧SYS0x18，保留task.c中本人WALL.CFG的fs_user_mutable和W权限检查，wm_set_wallpaper先真实fs_write一个ASCII模式字节成功才改本人会话内存；旧-5映射ACCESS，旧-1映射FAILED并保留raw。SCAPI.H及被忽略寄存器原义完全不变。

`ScWallpaperQuery`是request=NULL、32B版本1只读输出。core验证可信当前PID、IF=0、完整声明输出区RW和version，失败不改out，较大未知尾部保持。无本人view返回NOT_FOUND；锁定/关闭会话返回ACCESS；不规范mode或主题路径查询失败返回FAILED。成功mode和session来自真实本人view，image-selected只表示本人主题配置有图片路径，**不表示图片存在、已解码或正显示**。不提供外来SID参数，也不取得SYSTEM权限。

组合检查使用完整实际SC32API.C、wm.c和sc32_core.c加这些片段，只输出到专用agent-host目录。旧WM本身存在缩进/缺省初始化warning，该旧TU检查允许它们保持warning；三个应用仍用-Wall/-Wextra/-Werror，只有共用可选辅助函数的unused-function作例外。`pinned-sources.json`记录接线依据的实际文件SHA256；Root整合前核对变化，再做必要实际权限/每会话/保存失败回归，不把语法检查称客体通过。
