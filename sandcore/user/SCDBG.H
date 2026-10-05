#ifndef SCDBG_H
#define SCDBG_H
#include "SCIO.H"
/* 三环scdbg仅调试由自己DBGSPAWN2创建的目标；Ring0调试始终属于
 * 外部COM1。作业票据检查代数，目标退出后不再使用旧PID。 */
static int debug_main(void);
#endif
