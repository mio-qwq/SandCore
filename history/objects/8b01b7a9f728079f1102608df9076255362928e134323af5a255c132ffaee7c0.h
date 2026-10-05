#ifndef SANDCORE_FS_H
#define SANDCORE_FS_H

#include "io.h"

/* SandFS v4 可写路径接口 —— 版图与持久化边界见 docs/FS.md。
 * 独立 IDE 数据盘：LBA0 超级块、LBA1..32 目录、LBA33 起文件数据；
 * 目录容量192，每项名字64B（含NUL）+ LBA/大小各4B。超级块24处
 * 声明盘容量，0保持历史8MB；当前发布盘64MB，仍校验实际ATA容量。
 * 旧 v1/v2 单扇区目录可读写，保持其原布局，不把旧数据区当新目录覆盖。
 * 路径前缀投影子目录，另有显式空目录；没有目录inode或磁盘权限位。 */

#define FS_NAME_MAX 63

void fs_init(void);                            /* 校验超级块/目录/范围，失败呈现空盘 */
int  fs_count(void);                           /* 文件数 */
const char *fs_name(int i);                    /* 第 i 个文件名 (只读指针) */
u32  fs_size(int i);                           /* 字节数 */
/* read 最多写 bufsize 字节，不自动 NUL；末扇区中转防止写穿调用者边界。
 * write 只写 size 正文，放得下原扇区则重写，增长则追加，最后写目录。
 * 返回字节数或 -1；失败可能已写部分数据，没有事务回滚/掉电原子性。 */
int  fs_read(const char *name, void *buf, u32 bufsize);
/* offset/容量均以字节计，EOF返回0，越过EOF拒绝；不自动补NUL。 */
int fs_read_at(const char *name,void *buf,u32 capacity,u32 offset);
int  fs_write(const char *name, const u8 *buf, u32 size);
/* M7：磁盘仍平铺完整路径，目录由斜杠前缀推导，start_lba=0 的记录
 * 代表显式空目录。路径允许根斜杠、重复斜杠、./..；查找忽略 ASCII
 * 大小写而保留显示拼写。规范化后再检查保护路径，不能用 .. 绕过。 */
int fs_normalize(const char *path,char *out);
int fs_user_mutable(const char *path,int descendants);
int fs_stat(const char *path,u32 *info); /* [0] 1 文件/2 目录，[1] 字节数 */
int fs_list_dir(const char *path,char *out,u32 capacity); /* D/F 名字 大小 LF */
int fs_mkdir(const char *path);
int fs_remove(const char *path);       /* 仅文件或空目录，非空目录拒绝 */
int fs_rename(const char *oldname,const char *newname); /* 目录整组前缀修改 */

#endif /* SANDCORE_FS_H */
