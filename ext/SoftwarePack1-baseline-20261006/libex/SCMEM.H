#ifndef SANDCORE_MEMORY_SOURCE_LIBRARY_H
#define SANDCORE_MEMORY_SOURCE_LIBRARY_H
#include "SCAPI.H"
/* mio：三环源级批量内存库v1，不是libc，也不追加内核ABI。
 * 所有地址/内存由调用者拥有：正长度时必须有足够的已映射空间，
 * 字节区间不得跨u32地址末端。函数不隐藏申请、不裁短、不发布帧。
 * 零长度不解引用，可用空指针；copy的非空区间不得重叠，move允许。
 * fill32的长度单位是u32项，其余均为字节；fill保留value的低8位。
 * 不改变DF/cdecl：REP入口先CLD，反向重叠使用整数C逆序，不设置STD。
 * M7/SCCC旧版使用相同结果的C分支，新SCCC/GCC用基本i386 REP批处理。
 * 用于预生成材质/静态缓存、像素行、深度清空等，调用方维护缓存失效。 */
#define SCMEM_VERSION 1
static void *sc_mem_copy(void *destination,const void *source,u32 bytes);
static void *sc_mem_move(void *destination,const void *source,u32 bytes);
static void *sc_mem_fill(void *destination,int value,u32 bytes);
static u32 *sc_mem_fill32(u32 *destination,u32 value,u32 words);
#endif
