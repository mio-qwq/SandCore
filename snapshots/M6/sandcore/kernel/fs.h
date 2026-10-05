#ifndef SANDCORE_FS_H
#define SANDCORE_FS_H

#include "io.h"

/* SandFS v3 可写伪目录接口 —— 版图与持久化边界见 docs/FS.md。
 * 独立 IDE 数据盘：LBA0 超级块、LBA1..8 目录、LBA9 起文件数据；
 * 目录容量 96，每项名字 32B（含 NUL）+ LBA/大小各 4B。
 * 旧 v1/v2 单扇区目录可读写，保持其原布局，不把旧数据区当新目录覆盖。
 * 内核只提供整文件接口；sys/ 等是文件名中的前缀，不存在目录 inode。 */

#define FS_NAME_MAX 31

void fs_init(void);                            /* 校验超级块/目录/范围，失败呈现空盘 */
int  fs_count(void);                           /* 文件数 */
const char *fs_name(int i);                    /* 第 i 个文件名 (只读指针) */
u32  fs_size(int i);                           /* 字节数 */
/* read 最多写 bufsize 字节，不自动 NUL；末扇区中转防止写穿调用者边界。
 * write 只写 size 正文，放得下原扇区则重写，增长则追加，最后写目录。
 * 返回字节数或 -1；失败可能已写部分数据，没有事务回滚/掉电原子性。 */
int  fs_read(const char *name, void *buf, u32 bufsize);
int  fs_write(const char *name, const u8 *buf, u32 size);

#endif /* SANDCORE_FS_H */
