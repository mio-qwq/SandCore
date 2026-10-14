#ifndef SANDCORE_SCL_BOOT_H
#define SANDCORE_SCL_BOOT_H

#include "io.h"

/* 只负责固定、受保护的用户态根装载器；应用依赖与回调由三环处理。
 * IF=0 的本人陷入调用。输出完整32B先验RW，失败不发布半个根映像。 */
#define SCL_BOOT_INFO_SIZE 32u
#define SCL_BOOT_INFO_VERSION 1u
int scl_boot_map(u32 user_output,u32 user_descriptor);

/* 这里只撤销私有缓存，页仍由完整模块释放或退出页目录回收负责。
 * 新任务提交前与原映像释放后清零，避免PID/虚址复用继承旧入口。 */
void scl_boot_reset(int pid);

#endif
