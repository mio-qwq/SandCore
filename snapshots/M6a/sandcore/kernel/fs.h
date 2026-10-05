#ifndef SANDCORE_FS_H
#define SANDCORE_FS_H

#include "io.h"

/* SandFS 只读文件系统 v1 (M5) —— 版图见 docs/FS.md
 * LBA 65: 超级块   LBA 66: 目录表   LBA 67+: 文件数据 */

#define FS_NAME_MAX 31

void fs_init(void);                            /* 读超级块+目录表, 失败=空盘 */
int  fs_count(void);                           /* 文件数 */
const char *fs_name(int i);                    /* 第 i 个文件名 (只读指针) */
u32  fs_size(int i);                           /* 字节数 */
int  fs_read(const char *name, void *buf, u32 bufsize);  /* 读整文件, 返回字节数, -1=没有 */
int  fs_write(const char *name, const u8 *buf, u32 size); /* 写/建文件 (SandFS v2) */

#endif /* SANDCORE_FS_H */
