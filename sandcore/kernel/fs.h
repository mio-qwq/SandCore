#ifndef SANDCORE_FS_H
#define SANDCORE_FS_H

#include "io.h"

/* SandFS v5权限/事务接口 —— 完整合同见docs/FS.md。
 * 独立IDE盘LBA0超级块，v5目录bank支持192/384/768/1536扇区；每个
 * 96B记录包含UID/GID/rw及持久对象代数，所有父目录显式记录。
 * 历史v1..v4兼容读；三环修改须先迁移副本。不会在旧数据区覆盖
 * 新目录，旧STAT仍8B。新盘COW/回收/CRC检测，不承诺扇区撕裂原子性。 */

#define FS_NAME_MAX 63
#define FS_ACCESS_READ 1u
#define FS_ACCESS_WRITE 2u
#define FS_ACCESS_META 4u
/* 只扩独立接口；旧STAT仍恰好写两个u32。 */
int fs_access_identity(const char *path,int uid,int gid,u32 access);
int fs_access(int pid,const char *path,u32 access);
int fs_access_parent(int pid,const char *path);
int fs_metadata(const char *path,u32 out[8]);
void fs_storage_info2(u32 out[16]);
int fs_permissions(int pid,const char *path,int uid,int gid,u32 mode);
/* M9流式替换：容量提前保留，正文顺序追加，commit才发布新目录。
 * token仅内核保存，owner绑定进程；0仅可信串口接收器内部使用。
 * 文件接收不为整文件分配RAM，任务退出/外部会话断开须abort。 */
int fs_stream_begin(int owner,const char *path,u32 capacity);
int fs_stream_write(int owner,u32 token,const void *data,u32 length);
int fs_stream_commit(int owner,u32 token);
int fs_stream_abort(int owner,u32 token);
int fs_stream_snapshot(int owner,u32 token,u32 out[4]);
void fs_stream_stop(int owner);
int fs_stream_stop_step(int owner);

void fs_init(void);                            /* 校验超级块/目录/范围，失败呈现空盘 */
void fs_storage_info(u32 *out);                 /* 固定32字只读；STORAGE.md字段表 */
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
/* 独立排他创建，整个缺失检查/元数据发布在IF=0陷入中完成；
 * 已有对象=-8，事务占名=-6；初始rw在发布前设置。 */
int fs_create_ex(int owner,const char *path,u32 kind,u32 mode);
int fs_remove(const char *path);       /* 仅文件或空目录，非空目录拒绝 */
int fs_rename(const char *oldname,const char *newname); /* 目录整组前缀修改 */

/* M10a2：独立普通文件对象，不借旧16fd/顺序事务。这里只接收已经
 * 按cwd解析一次的rootless名字；磁盘v5记录与旧generation语义不变。 */
#define FS_NATIVE_READ 1u
#define FS_NATIVE_WRITE 2u
#define FS_NATIVE_CREATE 4u
#define FS_NATIVE_TRUNCATE 8u
#define FS_NATIVE_APPEND 16u
#define FS_NATIVE_DIRTY 1u
#define FS_NATIVE_FAULTED 2u
#define FS_NATIVE_UNCERTAIN 4u
#define FS_NATIVE_GROWING 8u
#define FS_NATIVE_OPEN_OPERATION 1u
#define FS_NATIVE_WRITE_OPERATION 2u
#define FS_NATIVE_TRUNCATE_OPERATION 3u
#define FS_NATIVE_FLUSH_OPERATION 4u
#define FS_N_INVALID (-1)
#define FS_N_NOT_FOUND (-2)
#define FS_N_ACCESS (-3)
#define FS_N_MEMORY (-4)
#define FS_N_NO_SPACE (-5)
#define FS_N_STALE (-6)
#define FS_N_BUSY (-7)
#define FS_N_RANGE (-8)
#define FS_N_IO (-9)
#define FS_N_AGAIN (-10)
#define FS_N_NOT_SUPPORTED (-11)
typedef struct {u32 length,object,revision,flags;i32 uid,gid;u32 mode,reserved;} fs_native_info_t;
typedef struct {char name[64];u32 length,generation;i32 uid,gid;u32 mode,kind;} fs_native_row_t;
/* object是可信in/out：首调用0，AGAIN保留候选供同owner/writer续进。
 * 所有长迁移/缺口清零按128扇区预算，metadata发布按16扇区预算。 */
int fs_native_open(int pid,u32 writer,const char *name,u32 flags,u32 mode,u32 *object);
int fs_native_info(int pid,u32 object,u32 access,fs_native_info_t *out);
int fs_native_read(int pid,u32 object,void *out,u32 bytes,u32 offset,u32 *done);
int fs_native_write(int pid,u32 object,u32 writer,const void *data,u32 bytes,u32 offset,int append,u32 *done,u32 *position);
int fs_native_truncate(int pid,u32 object,u32 writer,u32 length);
int fs_native_flush(int pid,u32 object,u32 writer);
void fs_native_cancel(u32 object,u32 writer);
void fs_native_close(u32 object,u32 writer);
void fs_native_poll(void);
u32 fs_native_directory_epoch(void);
int fs_native_directory_read(int pid,const char *name,u32 epoch,u32 cursor,fs_native_row_t *out,u32 *next);

#endif /* SANDCORE_FS_H */
