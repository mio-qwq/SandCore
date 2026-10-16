#ifndef SANDCORE_SC32WALL_H
#define SANDCORE_SC32WALL_H
#include <SC32API.H>
enum {
    SC32_WALL_DUNES=0,SC32_WALL_NIGHT=1,SC32_WALL_SAND=2,
    SC32_WALL_IMAGE_SELECTED=1,SC32_WALL_MIN_ABI_MINOR=4,SC32_WALL_MIN_LIBRARY_VERSION=6
};
/* 本人登录会话的实际程序背景模式。image-selected只表示主题配置
* 选择了路径，不证明该图片存在、已解码或当前画面正在使用它。 */
typedef struct {ScU32 size,version,mode,session,flags,reserved[3];
} ScWallpaperInfo;
/* mode仅0..2；保原0x18的文件可变/W权限、先持久保存后修改内存语义。 */
ScStatus ScWallpaperSet(ScU32 mode);
/* 无权限升级，不接受外来SID；缺真实view返回NOT_FOUND而不伪造mode0。
* 输出版本1/32B；完整声明RW预验，失败不改正文，未知尾部保持。 */
ScStatus ScWallpaperQuery(ScWallpaperInfo *out);
#endif
