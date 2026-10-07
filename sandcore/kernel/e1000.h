#ifndef SANDCORE_E1000_H
#define SANDCORE_E1000_H
#include "io.h"
/* 本轮仅82540EM(8086:100e)。不将这个驱动宣称为其它真实网卡驱动。
 * 回调借用一帧DMA字节，仅调用期间有效；IRQ不调用回调或协议栈。 */
typedef int (*e1000_receive_t)(const u8 *frame,u32 bytes,void *opaque);
typedef u32 (*e1000_copy_t)(void *destination,u32 bytes,void *opaque);
enum {E1000_ABSENT,E1000_RESETTING,E1000_RUNNING,E1000_FAILED,E1000_STOPPED,
      E1000_START_PENDING}; /* 只发现/映射设备；首次服务轮才提交硬件复位。 */
int e1000_init(void);
void e1000_irq(u32 irq);
u32 e1000_poll(e1000_receive_t receive,void *opaque); /* 位0配置/链路变，位1发送空间变。 */
int e1000_send(u32 bytes,e1000_copy_t copy,void *opaque);
int e1000_control(u32 operation); /* 0停止，1恢复/复位；由network检查权限。 */
int e1000_pending(void);
int e1000_tx_ready(void);
void e1000_snapshot(u32 out[32]);
const u8 *e1000_mac(void);
int e1000_link(void);
int e1000_state(void);
#endif
