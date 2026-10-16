#ifndef SANDCORE_SC32_DEBUG_H
#define SANDCORE_SC32_DEBUG_H
#include "io.h"
int sc32_debug_call(int pid,u32 operation,u32 request,u32 output);
/* 异常钩子返回0=旧处理、1=已继续、2=交旧pause_fault保存真实帧。 */
int sc32_debug_exception(int pid,u32 vector,u32 *frame);
void sc32_debug_unmap(int pid,u32 base,u32 bytes);
void sc32_debug_mapping_changed(int pid,u32 base,u32 bytes);
void sc32_debug_stop(int pid);
int sc32_debug_stop_step(int pid);
void sc32_debug_reset(int pid);
int sc32_debug_legacy_conflict(int pid,u32 address);
/* 只有可信SCX加载器调用；源文件与真正加载字节仍在同一个IF=0事务。 */
int sc32_debug_main_image(int pid,const char *path,u32 load,u32 bss);
int sc32_debug_bootstrap_image(int pid,u32 base,const void *file,u32 file_bytes,u32 allocation_bytes);
#endif
