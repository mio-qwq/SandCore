#include "serial.h"

/* 单CPU的短IF临界区保护主循环与UART IRQ。不得把它说成SMP锁；
 * M10接入多核前需要按端口转为明确的per-CPU/自旋锁合同。
 * 1024B队列应对短时合成与输入突发，空闲不持续扫描UART。
 * 溢出显式计数，上层文件协议必须丢弃受损帧并重新同步。 */
typedef struct {
    u32 base,present;
    u32 rx_head,rx_tail,tx_head,tx_tail;
    u32 rx_bytes,tx_bytes,rx_dropped,line_errors;
    u8 rx[SERIAL_QUEUE_BYTES],tx[SERIAL_QUEUE_BYTES];
} serial_port_t;

/* 内部诊断可读的真实队列；不构成用户ABI，用户页表不能映射它。 */
serial_port_t serial_ports[SERIAL_PORTS];

static u32 serial_lock(void)
{
    u32 flags;
    __asm__ __volatile__("pushfl; popl %0; cli" : "=r"(flags) :: "memory");
    return flags;
}
static void serial_unlock(u32 flags)
{
    if(flags&0x200u)sti();
    else __asm__ __volatile__("" ::: "memory");
}

static void serial_receive(serial_port_t *p)
{
    /* FIFO深度只有16；有界读取覆盖短突发，不在IRQ等待将来的输入。 */
    for(u32 budget=64;budget;budget--){
        u8 status=inb((u16)(p->base+5));
        if(status&0x1Eu)p->line_errors++;
        if(!(status&1))break;
        u8 value=inb((u16)p->base);
        p->rx_bytes++;
        if(status&0x1Eu){p->rx_dropped++;continue;}
        if(p->rx_head-p->rx_tail>=SERIAL_QUEUE_BYTES){p->rx_dropped++;continue;}
        p->rx[p->rx_head&(SERIAL_QUEUE_BYTES-1)]=value;
        p->rx_head++;
    }
}

static void serial_transmit(serial_port_t *p)
{
    if(p->tx_head!=p->tx_tail && (inb((u16)(p->base+5))&0x20)){
        /* THRE表示FIFO为空，一次填16字节，避免每字节一个中断。 */
        for(u32 n=0;n<16 && p->tx_tail!=p->tx_head;n++){
            outb((u16)p->base,p->tx[p->tx_tail&(SERIAL_QUEUE_BYTES-1)]);
            p->tx_tail++;p->tx_bytes++;
        }
    }
    /* RX/线路异常常开；没有积压就关THRE，避免空闲中断风暴。 */
    outb((u16)(p->base+1),(u8)(p->tx_head==p->tx_tail ? 5 : 7));
}

static void serial_isr(u32 port)
{
    serial_port_t *p=&serial_ports[port];
    if(!p->present)return;
    for(u32 budget=32;budget;budget--){
        u8 cause=inb((u16)(p->base+2));
        if(cause&1)break;
        switch(cause&0x0E){
        case 4:case 12:case 6:serial_receive(p);break;
        case 2:serial_transmit(p);break;
        case 0:(void)inb((u16)(p->base+6));break;
        default:return;
        }
    }
}

void serial_debug_isr(void){serial_isr(SERIAL_DEBUG);}
void serial_management_isr(void){serial_isr(SERIAL_MANAGEMENT);}

void serial_init(void)
{
    u32 flags=serial_lock();
    static const u16 bases[SERIAL_PORTS]={0x3F8,0x2F8};
    for(u32 n=0;n<SERIAL_PORTS;n++){
        serial_port_t *p=&serial_ports[n];
        p->base=bases[n];
        outb((u16)(p->base+3),3);       /* 8N1，先关闭DLAB再操作IER。 */
        outb((u16)(p->base+1),0);
        u8 saved=inb((u16)(p->base+7));
        outb((u16)(p->base+7),0xA5);
        p->present=inb((u16)(p->base+7))==0xA5;
        outb((u16)(p->base+7),saved);
        if(!p->present)continue;
        outb((u16)(p->base+3),0x83);
        outb((u16)p->base,1);outb((u16)(p->base+1),0); /* 115200 */
        outb((u16)(p->base+3),3);
        outb((u16)(p->base+2),0xC7);   /* FIFO开、清收发、14B触发。 */
        outb((u16)(p->base+4),0x0B);   /* DTR/RTS/OUT2；LOOP始终为0。 */
        (void)inb((u16)(p->base+5));(void)inb((u16)(p->base+6));
        outb((u16)(p->base+1),5);
        u8 mask=(u8)(1u<<(n==SERIAL_DEBUG ? 4 : 3));
        outb(0x21,(u8)(inb(0x21)&~mask));
    }
    serial_unlock(flags);
}

u32 serial_read(u32 port,u8 *data,u32 bytes)
{
    if(port>=SERIAL_PORTS || !data)return 0;
    u32 flags=serial_lock(),n=0;
    serial_port_t *p=&serial_ports[port];
    while(n<bytes && p->rx_tail!=p->rx_head){
        data[n++]=p->rx[p->rx_tail&(SERIAL_QUEUE_BYTES-1)];p->rx_tail++;
    }
    serial_unlock(flags);return n;
}

u32 serial_write(u32 port,const u8 *data,u32 bytes)
{
    if(port>=SERIAL_PORTS || !data)return 0;
    u32 flags=serial_lock(),n=0;
    serial_port_t *p=&serial_ports[port];
    if(p->present){
        while(n<bytes && p->tx_head-p->tx_tail<SERIAL_QUEUE_BYTES){
            p->tx[p->tx_head&(SERIAL_QUEUE_BYTES-1)]=data[n++];p->tx_head++;
        }
        serial_transmit(p);
    }
    serial_unlock(flags);return n;
}

int serial_snapshot(u32 port,serial_stats_t *out)
{
    if(port>=SERIAL_PORTS || !out)return -1;
    u32 flags=serial_lock();serial_port_t *p=&serial_ports[port];
    out->present=p->present;out->rx_bytes=p->rx_bytes;out->tx_bytes=p->tx_bytes;
    out->rx_dropped=p->rx_dropped;out->line_errors=p->line_errors;
    out->rx_pending=p->rx_head-p->rx_tail;out->tx_pending=p->tx_head-p->tx_tail;
    serial_unlock(flags);return 0;
}

int serial_emergency_get(u32 port)
{
    if(port>=SERIAL_PORTS || !serial_ports[port].present)return -1;
    u32 flags=serial_lock();serial_port_t *p=&serial_ports[port];
    serial_receive(p);int value=-1;
    if(p->rx_head!=p->rx_tail){
        value=p->rx[p->rx_tail&(SERIAL_QUEUE_BYTES-1)];p->rx_tail++;
    }
    serial_unlock(flags);return value;
}

int serial_emergency_put(u32 port,u8 value)
{
    if(port>=SERIAL_PORTS || !serial_ports[port].present)return -1;
    u32 flags=serial_lock();serial_port_t *p=&serial_ports[port];
    /* 先排空已有队列，保持停止前后的字节顺序。最长等待固定有界。
     * 超时保留未发送队列，报告失败，不伪造发送成功。 */
    for(u32 budget=65536;budget;budget--){
        if(!(inb((u16)(p->base+5))&0x20))continue;
        if(p->tx_tail!=p->tx_head){serial_transmit(p);continue;}
        outb((u16)p->base,value);p->tx_bytes++;
        serial_unlock(flags);return 0;
    }
    serial_unlock(flags);return -1;
}
