#include "e1000.h"
#include "memory.h"
#include "paging.h"
#include "task.h"
#include "timer.h"

/* 寄存器/描述符来自Intel 8254x SDM Rev4的3/4/13/14章，驱动自写。
 * 只用legacy描述符与软件校验和：队列生命周期可审阅，不借GPL驱动
 * 或QEMU设备实现作移植依赖。位名仅覆盖本轮确实使用的硬件合同。 */
enum {
    REG_CTRL=0x00000,REG_STATUS=0x00008,REG_EERD=0x00014,
    REG_FCAL=0x00028,REG_FCAH=0x0002C,REG_FCT=0x00030,
    REG_ICR=0x000C0,REG_ITR=0x000C4,REG_IMS=0x000D0,REG_IMC=0x000D8,
    REG_RCTL=0x00100,REG_FCTTV=0x00170,REG_TCTL=0x00400,REG_TIPG=0x00410,
    REG_RDBAL=0x02800,REG_RDBAH=0x02804,REG_RDLEN=0x02808,
    REG_RDH=0x02810,REG_RDT=0x02818,REG_RDTR=0x02820,REG_RADV=0x0282C,
    REG_TDBAL=0x03800,REG_TDBAH=0x03804,REG_TDLEN=0x03808,
    REG_TDH=0x03810,REG_TDT=0x03818,REG_TIDV=0x03820,REG_TADV=0x0382C,
    REG_RXCSUM=0x05000,REG_MTA=0x05200,REG_RAL=0x05400,REG_RAH=0x05404,REG_VFTA=0x05600
};
#define CTRL_RESET (1u<<26)
#define CTRL_LINK (1u<<6)
#define CTRL_AUTO_SPEED (1u<<5)
#define CTRL_CLEAR ((1u<<31)|(1u<<30)|(1u<<27)|(1u<<28)|(1u<<12)|(1u<<11)|(1u<<7)|(1u<<3))
#define STATUS_LINK 2u
#define IRQ_TX 1u
#define IRQ_LINK 4u
#define IRQ_RX_LOW 16u
#define IRQ_RX_OVERFLOW 64u
#define IRQ_RX 128u
#define IRQ_USED (IRQ_TX|IRQ_LINK|IRQ_RX_LOW|IRQ_RX_OVERFLOW|IRQ_RX)
#define RING_COUNT 128u
#define BUFFER_BYTES 2048u
#define BUFFER_PAGES 64u
#define DESC_DD 1u
#define RX_EOP 2u
#define RX_BAD_FRAME 0x87u
#define TX_COMMAND 11u /* EOP|IFCS|RS，先拷完帧，再交给设备。 */
typedef struct __attribute__((packed)) {
    u32 low,high;u16 length,checksum;u8 status,errors;u16 special;
} rx_descriptor_t;
typedef struct __attribute__((packed)) {
    u32 low,high;u16 length;u8 checksum_offset,command,status,checksum_start;u16 special;
} tx_descriptor_t;
static volatile u32 *registers;
static volatile rx_descriptor_t *rx_ring;
static volatile tx_descriptor_t *tx_ring;
static u32 pci_address,pci_command,bar_base,bar_bytes,irq_line,irq_pin;
static u32 rx_page,tx_page,rx_buffers,tx_buffers,rx_next,tx_next,tx_clean,tx_count;
static u32 tx_ticks[RING_COUNT],rx_packets,rx_bytes,rx_dropped,rx_bad,rx_overflows;
static u32 tx_packets,tx_bytes,tx_bad,tx_backpressure,irq_count,resets,watchdogs;
static u32 state,reset_started,phase,phase_started,eeprom_word,last_link_poll,link_up;
static u32 error_code,epoch=1,last_irq;
static volatile u32 irq_pending;
static u8 station[6];
static int rx_discard,dma_started;

static u32 pci_read(u32 address,u32 offset)
{outl(0xCF8,address|(offset&0xFCu));return inl(0xCFC);}
static void pci_write(u32 address,u32 offset,u32 value)
{outl(0xCF8,address|(offset&0xFCu));outl(0xCFC,value);}
static void pci_enable(u32 command)
{
    /* status高16位是W1C，不能把一次read的错误位写回清掉别人的诊断。 */
    pci_write(pci_address,4,command&0xFFFFu);(void)pci_read(pci_address,4);
}
static u32 read_reg(u32 offset){return registers[offset/4];}
static void write_reg(u32 offset,u32 value)
{registers[offset/4]=value;__asm__ __volatile__("":::"memory");}
static void flush(void){(void)read_reg(REG_STATUS);}
static void zero_bytes(u32 base,u32 count){u8 *p=(u8 *)base;for(u32 i=0;i<count;i++)p[i]=0;}
static int station_valid(void)
{u32 any=0,all=0xFFu;for(u32 i=0;i<6;i++){any|=station[i];all&=station[i];}return any && all!=0xFFu && !(station[0]&1u);}
static void station_from_registers(void)
{
    u32 low=read_reg(REG_RAL),high=read_reg(REG_RAH);
    for(u32 i=0;i<4;i++)station[i]=(u8)(low>>(8*i));station[4]=(u8)high;station[5]=(u8)(high>>8);
}
static void mask_pic(int enable)
{
    u16 port=irq_line<8?0x21:0xA1;u8 mask=inb(port),bit=(u8)(1u<<(irq_line&7u));
    /* 关闭本设备只关IMC，不屏蔽共享PIC线，否则会误停AC97等设备。 */
    if(enable)outb(port,(u8)(mask&~bit));
}
static void disable_dma(void)
{
    write_reg(REG_IMC,0xFFFFFFFFu);write_reg(REG_RCTL,0);write_reg(REG_TCTL,0);flush();
    pci_enable((pci_command|2u)&~4u);irq_pending=0;
}
static void release_unstarted(void)
{
    /* 曾交给设备的页若复位失败保持隔离，不能将可能仍DMA的页交给
     * 新任务。成功复位后可复用同一组页；首次启用前的失败完整回滚。 */
    if(dma_started)return;
    if(rx_page)pframe_free(rx_page);if(tx_page)pframe_free(tx_page);
    if(rx_buffers)pframe_free_run(rx_buffers,BUFFER_PAGES);
    if(tx_buffers)pframe_free_run(tx_buffers,BUFFER_PAGES);
    rx_page=tx_page=rx_buffers=tx_buffers=0;rx_ring=0;tx_ring=0;
}
static void failed(u32 reason)
{disable_dma();error_code=reason;state=E1000_FAILED;link_up=0;epoch++;release_unstarted();}
static int begin_reset(void)
{
    if(!registers)return -19;disable_dma();write_reg(REG_CTRL,read_reg(REG_CTRL)|CTRL_RESET);flush();
    state=E1000_RESETTING;phase=0;reset_started=sc_ticks;resets++;link_up=0;epoch++;task_kernel_wake();return 0;
}
int e1000_init(void)
{
    if(registers)return -16;
    for(u32 bus=0;bus<256 && !pci_address;bus++)for(u32 dev=0;dev<32 && !pci_address;dev++){
        u32 base=0x80000000u|(bus<<16)|(dev<<11);
        if((pci_read(base,0)&0xFFFFu)==0xFFFFu)continue;
        u32 functions=(pci_read(base,0x0C)&0x00800000u)?8:1;
        for(u32 f=0;f<functions;f++){
            u32 address=base|(f<<8);
            if(pci_read(address,0)!=0x100E8086u || (pci_read(address,8)>>16)!=0x0200u)continue;
            pci_address=address;break;
        }
    }
    if(!pci_address){state=E1000_ABSENT;error_code=1;return -19;}
    u32 interrupt=pci_read(pci_address,0x3C);irq_line=interrupt&255u;irq_pin=(interrupt>>8)&255u;
    if(irq_line<3 || irq_line>15 || !irq_pin || irq_pin>4){state=E1000_FAILED;error_code=2;return -19;}
    u32 bar=pci_read(pci_address,0x10);pci_command=pci_read(pci_address,4)&0xFFFFu;
    if((bar&7u) || !(bar&~15u)){state=E1000_FAILED;error_code=3;return -19;}
    pci_enable(pci_command&~7u);pci_write(pci_address,0x10,0xFFFFFFFFu);
    u32 mask=pci_read(pci_address,0x10);pci_write(pci_address,0x10,bar);
    bar_base=bar&~15u;bar_bytes=~(mask&~15u)+1u;
    if(bar_bytes<0x20000u || bar_bytes>0x200000u || (bar_bytes&(bar_bytes-1u)) || (bar_base&(bar_bytes-1u)) || paging_map_device(bar_base,bar_bytes)<0){
        pci_enable(pci_command&~4u);state=E1000_FAILED;error_code=4;return -19;
    }
    registers=(volatile u32 *)bar_base;pci_enable((pci_command|2u)&~4u);
    station_from_registers();disable_dma();state=E1000_START_PENDING;
    /* 此后还有字体/原生欢迎页初始化，不能提前提交复位却直到几秒
     * 后才首次服务，误把尚未观察的完成位当成250ms硬件失败。
     * 延后的是实际CTRL写入，不延长已提交硬件操作的截止时间。 */
    task_kernel_wake();return 0;
}
void e1000_irq(u32 irq)
{
    if(!registers || irq!=irq_line || state==E1000_ABSENT)return;
    /* ICR是读清；仅取本设备原因，共享IRQ其它设备照常独立确认。
     * RX DD/TX DD是数据真实来源，IRQ只是唤醒提示，不假定一IRQ一帧。 */
    u32 cause=read_reg(REG_ICR)&IRQ_USED;if(!cause)return;
    irq_pending|=cause;last_irq=cause;irq_count++;task_kernel_wake();
}
static int allocate_rings(void)
{
    if(!rx_page)rx_page=pframe_alloc();if(!tx_page)tx_page=pframe_alloc();
    if(!rx_buffers)rx_buffers=pframe_alloc_run(BUFFER_PAGES);
    if(!tx_buffers)tx_buffers=pframe_alloc_run(BUFFER_PAGES);
    if(!rx_page || !tx_page || !rx_buffers || !tx_buffers){release_unstarted();return -12;}
    zero_bytes(rx_page,4096);zero_bytes(tx_page,4096);
    rx_ring=(volatile rx_descriptor_t *)rx_page;tx_ring=(volatile tx_descriptor_t *)tx_page;
    for(u32 i=0;i<RING_COUNT;i++){
        rx_ring[i].low=rx_buffers+i*BUFFER_BYTES;tx_ring[i].low=tx_buffers+i*BUFFER_BYTES;
        tx_ring[i].status=DESC_DD;tx_ticks[i]=0;
    }
    rx_next=tx_next=tx_clean=tx_count=0;rx_discard=0;return 0;
}
static int start_hardware(void)
{
    if(allocate_rings()<0)return -12;
    write_reg(REG_CTRL,(read_reg(REG_CTRL)&~CTRL_CLEAR)|CTRL_LINK|CTRL_AUTO_SPEED);
    write_reg(REG_FCAL,0);write_reg(REG_FCAH,0);write_reg(REG_FCT,0);write_reg(REG_FCTTV,0);
    for(u32 i=0;i<128;i++){write_reg(REG_MTA+4*i,0);write_reg(REG_VFTA+4*i,0);}
    for(u32 i=1;i<16;i++){write_reg(REG_RAL+8*i,0);write_reg(REG_RAH+8*i,0);}
    u32 low=(u32)station[0]|((u32)station[1]<<8)|((u32)station[2]<<16)|((u32)station[3]<<24);
    u32 high=(u32)station[4]|((u32)station[5]<<8)|0x80000000u;
    write_reg(REG_RAL,low);write_reg(REG_RAH,high);write_reg(REG_RXCSUM,0);
    write_reg(REG_RDBAL,rx_page);write_reg(REG_RDBAH,0);write_reg(REG_RDLEN,RING_COUNT*sizeof(rx_descriptor_t));
    write_reg(REG_RDH,0);write_reg(REG_RDT,RING_COUNT-1);write_reg(REG_RDTR,0);write_reg(REG_RADV,0);
    write_reg(REG_TDBAL,tx_page);write_reg(REG_TDBAH,0);write_reg(REG_TDLEN,RING_COUNT*sizeof(tx_descriptor_t));
    write_reg(REG_TDH,0);write_reg(REG_TDT,0);write_reg(REG_TIDV,0);write_reg(REG_TADV,0);
    write_reg(REG_TIPG,10u|(10u<<10)|(10u<<20));
    write_reg(REG_TCTL,2u|8u|(0x10u<<4)|(0x40u<<12));
    write_reg(REG_RCTL,2u|0x8000u|(1u<<26));
    write_reg(REG_ITR,2000); /* 512us中断间距，真实收益由统一矩阵测量。 */
    (void)read_reg(REG_ICR);irq_pending=0;pci_enable((pci_command|6u)&~0x0400u);
    dma_started=1;state=E1000_RUNNING;error_code=0;epoch++;link_up=(read_reg(REG_STATUS)&STATUS_LINK)!=0;
    last_link_poll=sc_ticks;mask_pic(1);write_reg(REG_IMS,IRQ_USED);flush();return 0;
}
u32 e1000_poll(e1000_receive_t receive,void *opaque)
{
    if(!registers)return 0;
    if(state==E1000_START_PENDING){begin_reset();return 1;}
    if(state==E1000_RESETTING){
        if(!phase){
            /* 先观察条件再判超时；调度延后不代表硬件完成位仍挂起。
             * 真正未完成的操作仍受25tick限制，不忽略失败或重启计时。 */
            if(read_reg(REG_CTRL)&CTRL_RESET){if((u32)(sc_ticks-reset_started)>25u)failed(5);return 0;}
            if(sc_ticks==reset_started)return 0;
            if(station_valid()){if(start_hardware()<0)failed(6);return 1;}
            eeprom_word=0;phase=1;phase_started=sc_ticks;write_reg(REG_EERD,1u);flush();return 0;
        }
        u32 data=read_reg(REG_EERD);
        if(!(data&16u)){if((u32)(sc_ticks-phase_started)>10u){failed(7);return 1;}return 0;}
        station[eeprom_word*2]=(u8)(data>>16);station[eeprom_word*2+1]=(u8)(data>>24);
        if(++eeprom_word==3){if(!station_valid())failed(8);else if(start_hardware()<0)failed(6);return 1;}
        phase_started=sc_ticks;write_reg(REG_EERD,1u|(eeprom_word<<8));flush();return 0;
    }
    if(state!=E1000_RUNNING)return 0;
    u32 flags,pending;
    __asm__ __volatile__("pushfl; popl %0; cli":"=r"(flags)::"memory");
    pending=irq_pending;irq_pending=0;
    __asm__ __volatile__("pushl %0; popfl"::"r"(flags):"memory","cc");
    pending|=read_reg(REG_ICR)&IRQ_USED;u32 changes=0;
    if(pending&IRQ_RX_OVERFLOW)rx_overflows++;
    if((pending&IRQ_LINK) || (u32)(sc_ticks-last_link_poll)>=25u){
        last_link_poll=sc_ticks;u32 linked=(read_reg(REG_STATUS)&STATUS_LINK)!=0;
        if(linked!=link_up){link_up=linked;epoch++;changes|=1;}
    }
    u32 tx_budget=64;
    while(tx_count && tx_budget--){
        volatile tx_descriptor_t *d=&tx_ring[tx_clean];if(!(d->status&DESC_DD))break;
        __asm__ __volatile__("":::"memory");
        if(d->status&0x0Eu)tx_bad++;else{tx_packets++;tx_bytes+=d->length;}
        tx_clean=(tx_clean+1)&(RING_COUNT-1);tx_count--;changes|=2;
    }
    if(tx_count && link_up && (u32)(sc_ticks-tx_ticks[tx_clean])>500u){watchdogs++;begin_reset();return changes|1;}
    u32 rx_budget=32,last_returned=RING_COUNT;
    while(rx_budget--){
        volatile rx_descriptor_t *d=&rx_ring[rx_next];u8 status=d->status;if(!(status&DESC_DD))break;
        __asm__ __volatile__("":::"memory");u32 bytes=d->length;
        if(!(status&RX_EOP)){rx_discard=1;rx_bad++;}
        else if(rx_discard || (d->errors&RX_BAD_FRAME) || bytes<14 || bytes>1514){rx_bad++;rx_dropped++;rx_discard=0;}
        else{rx_packets++;rx_bytes+=bytes;if(!receive || receive((const u8 *)(rx_buffers+rx_next*BUFFER_BYTES),bytes,opaque)<0)rx_dropped++;}
        d->status=0;d->errors=0;d->length=0;__asm__ __volatile__("":::"memory");
        last_returned=rx_next;rx_next=(rx_next+1)&(RING_COUNT-1);
    }
    if(last_returned<RING_COUNT){write_reg(REG_RDT,last_returned);flush();}
    if(e1000_pending())task_kernel_wake();return changes;
}
int e1000_send(u32 bytes,e1000_copy_t copy,void *opaque)
{
    if(state!=E1000_RUNNING)return -19;if(!link_up)return -100;
    if(!copy || bytes<14 || bytes>1514)return -90;
    /* 头尾相等只能代表空环，留一描述符避免满环变成“空”。队列满
     * 让协议栈背压/重传，不覆盖还在DMA的帧，也不在系统调用里等待。 */
    if(tx_count>=RING_COUNT-1 || !(tx_ring[tx_next].status&DESC_DD)){tx_backpressure++;return -11;}
    void *buffer=(void *)(tx_buffers+tx_next*BUFFER_BYTES);
    if(copy(buffer,bytes,opaque)!=bytes)return -22;
    volatile tx_descriptor_t *d=&tx_ring[tx_next];d->length=(u16)bytes;d->checksum_offset=0;
    d->checksum_start=0;d->special=0;d->command=TX_COMMAND;d->status=0;tx_ticks[tx_next]=sc_ticks;
    __asm__ __volatile__("":::"memory");tx_next=(tx_next+1)&(RING_COUNT-1);tx_count++;
    write_reg(REG_TDT,tx_next);flush();return 0;
}
int e1000_control(u32 operation)
{
    if(!registers)return -19;
    if(operation==1)return begin_reset();
    if(operation)return -22;disable_dma();state=E1000_STOPPED;link_up=0;epoch++;return 0;
}
int e1000_pending(void)
{
    if(state==E1000_START_PENDING)return 1;
    if(state==E1000_RESETTING)return 0; /* PIT会继续服务复位，避免同tick空转。 */
    return state==E1000_RUNNING && (irq_pending || (rx_ring[rx_next].status&DESC_DD) || (tx_count && (tx_ring[tx_clean].status&DESC_DD)));
}
int e1000_state(void){return (int)state;}
int e1000_tx_ready(void)
{return state==E1000_RUNNING && link_up && tx_count<RING_COUNT-1 && (tx_ring[tx_next].status&DESC_DD);}
int e1000_link(void){return state==E1000_RUNNING && link_up;}
const u8 *e1000_mac(void){return station;}
void e1000_snapshot(u32 out[32])
{
    for(u32 i=0;i<32;i++)out[i]=0;out[0]=1;out[1]=state;out[2]=epoch;out[3]=link_up;
    out[4]=pci_address;out[5]=bar_base;out[6]=bar_bytes;out[7]=irq_line;out[8]=irq_pin;
    for(u32 i=0;i<4;i++)out[9]|=(u32)station[i]<<(8*i);out[10]=station[4]|((u32)station[5]<<8);
    out[11]=rx_packets;out[12]=rx_bytes;out[13]=rx_dropped;out[14]=rx_bad;out[15]=rx_overflows;
    out[16]=tx_packets;out[17]=tx_bytes;out[18]=tx_bad;out[19]=tx_backpressure;out[20]=tx_count;
    out[21]=irq_count;out[22]=resets;out[23]=watchdogs;out[24]=error_code;out[25]=last_irq;
    out[26]=(rx_page?1:0)+(tx_page?1:0)+(rx_buffers?BUFFER_PAGES:0)+(tx_buffers?BUFFER_PAGES:0);
    out[27]=rx_next;out[28]=tx_next;out[29]=tx_clean;out[30]=RING_COUNT;out[31]=BUFFER_BYTES;
}
