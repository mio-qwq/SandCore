#ifndef SCAWK_H
#define SCAWK_H
#include "SCRE.H"
#include "SCLAUNCH.H"
/* 自写整数AWK，独立SCX入口；不复制BusyBox或awk实现。
 * 解析一次、有界AST/NFA/散列表；输入及执行上限明确失败。 */
static int awk_main(void);
#endif
