#ifndef SANDCORE_SC32PATHWIRE_H
#define SANDCORE_SC32PATHWIRE_H
#include "../SC32API.H"

/* 私有selector属于既有SC32文件族；不占旧调用号或忽略寄存器。 */
#define SC32_P_QUERY 0x223u
typedef struct {
    ScU32 size,version,path,flags,reserved[4];
} Sc32PathQueryRequest;
typedef char sc32_path_query_request32[(sizeof(Sc32PathQueryRequest)==32)?1:-1];

#endif
