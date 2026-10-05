#ifndef SCPAGER_H
#define SCPAGER_H
#include "SCIO.H"
/* 自写分页器公用入口；more/less各自有main和独立SCX，不按argv[0]
 * 分派。终端来自既有连接能力，绝不打开本机UART设备获取权限。 */
static int pager_main(int less_mode);
#endif
