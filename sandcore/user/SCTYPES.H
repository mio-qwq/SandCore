#ifndef SANDCORE_SCTYPES_H
#define SANDCORE_SCTYPES_H

/* 公共整数宽度固定为i386的32位。此纯类型头不依赖运行库或内核。 */
typedef unsigned int ScU32;
typedef int ScI32;
typedef ScI32 ScStatus;
typedef struct { ScU32 lo,hi; } ScWideCounter;

#endif
