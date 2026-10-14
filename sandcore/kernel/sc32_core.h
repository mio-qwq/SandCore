#ifndef SANDCORE_SC32_CORE_H
#define SANDCORE_SC32_CORE_H

#include "io.h"

/* 私有新版gate只由可信陷入分发传真实pid，公开应用不处理寄存器。
 * 所有SC32返回值来自独立-101错误域，不能混用旧syscall或SCL负值。 */
#define SC32_CORE_SYSCALL 0x300u
int sc32_core_call(int pid,u32 operation,u32 request,u32 output);

/* 给同一gate的其它内核服务共用。输入是精确size/version1的固定
 * 记录；先验完整本人RO范围，再复制到可信局部缓冲，不借用户头
 * 作内核结构。输出只预检，调用者仍负责发布已知版本前缀及检查
 * 复制结果；声明的大输出整区须RW，未知尾部不能被顺手清零。
 * bytes是服务端真实缓冲宽度，不能从未校验的用户字段直接传入。 */
int sc32_core_input(int pid,u32 address,void *local,u32 bytes);
int sc32_core_output(int pid,u32 address,u32 bytes);

#endif
