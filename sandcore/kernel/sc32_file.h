#ifndef SANDCORE_SC32_FILE_H
#define SANDCORE_SC32_FILE_H
#include "io.h"

/* 调用者为统一SYS0x300门，pid必须来自可信陷入现场。服务内部先
 * 预验全部请求/输出/数据范围，再分配对象、消费游标或写盘。 */
int sc32_file_call(int pid,u32 operation,u32 request,u32 output);
/* 退出只走本PID/代数的拥有者链。每步至多清32个句柄，返回1完成；
 * 不执行用户回调，不在所有进程/所有文件上重复全表扫描。 */
int sc32_file_stop_step(int pid);
void sc32_file_stop(int pid);
/* FS后台只发布内核结果，不借用户out指针。完成缓存归原writer句柄，
 * 下次经过全范围预验的同请求领取；独立writer无需等用户ACK。 */
int sc32_file_complete(int pid,u32 generation,u32 writer,u32 kind,u32 position,u32 length,u32 object,int raw,u32 transferred);

#endif
