#ifndef SANDCORE_SIMD_H
#define SANDCORE_SIMD_H
#include "io.h"
void simd_init(void);
void simd_spawn(int pid);
void simd_switch(int previous,int next);
void simd_stop(int pid);
void simd_info(u32 out[8]);
int simd_sse2(void);
u32 simd_task_bytes(void);
#endif
