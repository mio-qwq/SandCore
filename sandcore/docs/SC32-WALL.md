# 用户会话程序壁纸接口

2026-10-16。公开头为 `SC32WALL.H`，由 SC32API ABI1.4 / version6 提供两个真实导出。主树定向宿主构建通过，客体权限、会话和持久保存检查待完成。原 SCAPI.H 字节与调用语义保持。

`ScWallpaperSet(mode)` 接受0沙丘、1夜色、2暖沙，其它值返回 RANGE。库私有适配沿原0x18入口，保留本人 WALL.CFG 的可变/W权限、先写文件成功再修改本人会话内存的顺序。旧错误保留 raw，不把失败显示成成功。

`ScWallpaperQuery(out)` 使用独立 SYS300 selector006。输出为32B/version1的 `ScWallpaperInfo`：size/version/mode/session/flags/reserved[3]。只查询调用者实际会话的 view；没有 view 返回 NOT_FOUND，关闭或锁定会话返回 ACCESS，不伪造默认0。完整声明输出范围必须可写，失败不改正文，未知尾部保持。

`SC32_WALL_IMAGE_SELECTED` 仅表示当前主题配置选了图片路径，不证明图片存在、解码完成或已经显示。此接口不替代主题图片异步服务，不接受其它SID或授予SYSTEM权限。

实现：`user/wallapi/` 为库私有桥，`kernel/sc32_wall_query.inc` 为值快照，`kernel/wm_wall_snapshot.inc` 在真实 view 所在TU内访问本人状态。私有WIRE和内核结构不安装进普通SDK。

修订：2026-10-16，公开两函数、错误、会话归属和图片选择边界；宿主接线通过，客体验证待完成。
