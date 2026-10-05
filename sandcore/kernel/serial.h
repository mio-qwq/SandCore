#ifndef SANDCORE_SERIAL_H
#define SANDCORE_SERIAL_H
#include "io.h"

/* M9：只给可信内核的硬件传输层，不向三环发布原始串口访问。
 * 两个消费者分别处理调试与管理；这里不解析命令、不授予身份。
 * 读写非阻塞，返回实际字节数；队列满时由上层退让/重试。 */
#define SERIAL_DEBUG 0u
#define SERIAL_MANAGEMENT 1u
#define SERIAL_PORTS 2u
#define SERIAL_QUEUE_BYTES 4096u

typedef struct {
    u32 present,rx_bytes,tx_bytes,rx_dropped,line_errors;
    u32 rx_pending,tx_pending;
} serial_stats_t;

void serial_init(void);
void serial_debug_isr(void);
void serial_management_isr(void);
u32 serial_read(u32 port,u8 *data,u32 bytes);
u32 serial_write(u32 port,const u8 *data,u32 bytes);
int serial_snapshot(u32 port,serial_stats_t *out);
/* 调试器停止调度/关闭中断后独立收发。只在没有并发消费者时使用；
 * get先取IRQ已收的字节，再轮询硬件，不能漏掉暂停前最后一个字节。
 * put先发送已有TX队列，有硬上限；缺设备时不挂死panic。 */
int serial_emergency_get(u32 port);
int serial_emergency_put(u32 port,u8 value);
#endif
