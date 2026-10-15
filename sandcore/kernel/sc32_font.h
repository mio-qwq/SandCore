#ifndef SANDCORE_SC32_FONT_H
#define SANDCORE_SC32_FONT_H
#include "io.h"

/* 只由当前任务可信syscall分发调用；无需单独owner对象或清理池。 */
int sc32_font_call(int pid,u32 operation,u32 request,u32 output);
#endif
