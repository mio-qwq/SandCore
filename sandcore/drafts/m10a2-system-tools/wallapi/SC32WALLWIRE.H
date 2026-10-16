#ifndef SANDCORE_SC32WALLWIRE_PRIVATE_H
#define SANDCORE_SC32WALLWIRE_PRIVATE_H
#include "../SC32WALL.H"
/* 已核当前core仅使用0x01..0x05；此只读selector避开GUI/FILE/
* Broker/Font/Lease/Host以及本轮Debug0x900、Pipe新增分组。 */
enum {SC32_W_QUERY=0x006,SC32_W_OPERATION=0x1100};
#endif
