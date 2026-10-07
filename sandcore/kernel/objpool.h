#ifndef SANDCORE_OBJPOOL_H
#define SANDCORE_OBJPOOL_H
#include "io.h"

/* 稳定地址的稀疏对象池。32是分配粒度，绝不是对象数量上限。
 * 三层索引覆盖现有带符号31位句柄域；没有依赖kmalloc固定1MiB堆。
 * 调用方必须在IF=0或持有任务渲染锁时串行访问；池内不偷偷开关IRQ。
 * KEEP_IDLE只用于兼容旧PID状态查询的少量历史记录，其余池空块还页。 */
#define OBJPOOL_KEEP_IDLE 1u
typedef struct objpool_node objpool_node_t;
typedef struct {
    objpool_node_t *roots[256];
    u32 full[8],live[8];
    u32 stride,flags,objects,pages,peak_pages,high_water;
} objpool_t;

int objpool_init(objpool_t *pool,u32 bytes,u32 flags);
int objpool_alloc(objpool_t *pool); /* 自动取得最低空编号；失败=-1。 */
int objpool_claim(objpool_t *pool,int id); /* 指定编号；失败不留下半个对象。 */
void objpool_release(objpool_t *pool,int id);
void objpool_remember(objpool_t *pool,int id); /* 成功提交后才保留兼容历史。 */
void *objpool_get(const objpool_t *pool,int id); /* 缺失/未持有返回0。 */
void *objpool_history(const objpool_t *pool,int id); /* 仅KEEP_IDLE允许查空槽历史。 */
int objpool_next(const objpool_t *pool,int after); /* 按编号遍历持有者，结束=-1。 */
void objpool_destroy(objpool_t *pool); /* 仅拥有者已清理对象内资源之后使用。 */

#endif
