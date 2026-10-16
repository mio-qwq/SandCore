#ifndef SANDCORE_SC32SIMD_H
#define SANDCORE_SC32SIMD_H
#include "SC32API.H"
#define SC32_SIMD_MIN_ABI_MINOR 4u
#define SC32_SIMD_MIN_LIBRARY_VERSION 6u
typedef struct {
    ScU32 size,version,ready,fxsr,sse2,cpuid_features,saved_bytes,switches,reserved[4];
} ScSimdInfo;
/* 只查询内核已启用且按真实任务保存/恢复的现场。ready=0时不能
 * 执行浮点解码；sse2=0不等于x87不可用，不把CPUID位当OS状态。 */
ScStatus ScSimdQuery(ScSimdInfo *out);
#endif
