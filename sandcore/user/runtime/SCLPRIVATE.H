#ifndef SANDCORE_SCLPRIVATE_H
#define SANDCORE_SCLPRIVATE_H
/* 仅SCLDR引导实现使用。公开应用接口没有软中断或寄存器封装。 */
int ScRuntimePlatformCall(int number,int a,int b,int c,int d,int e,int f);
#endif
