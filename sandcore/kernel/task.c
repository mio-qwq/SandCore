/* =====================================================================
 *  SandCore 任务子系统 v2 (kernel/task.c)
 *  ---------------------------------------------------------------------
 *  【v2 质变: 分页 + 多用户程序并发】
 *   - paging_init() 后内核恒等映射全部物理内存;
 *   - 每个用户任务一份页目录: 共享内核映射 + 私有用户区 (0x400000);
 *   - 所有程序都链接在 org 0x400000, 装载时映射到各自物理帧 ——
 *     同一个 hello.scx 可以同时跑 N 份互不覆盖;
 *   - 换任务 = sched_pick 换 esp + CR3;
 *   - 系统调用沿用调用者 CR3，指针经逐页校验后按用户虚址访问。
 *     物理帧可以碎片化；不能用“首帧 + 偏移”猜测后面的页。
 *   - 内核代码、任务表、内核栈位于各目录相同的 supervisor 映射中；
 *     因此切 CR3 之后仍能执行调度器的最后几条指令。
 * ===================================================================== */
#include "io.h"
#include "task.h"
#include "task_store.h"
#ifdef M10_DISK_CORE
#include "network.h"
#endif
#include "session.h"
#include "interrupts.h"
#include "memory.h"
#include "paging.h"
#include "timer.h"
#include "wm.h"
#include "fs.h"
#include "mouse.h"
#include "keyboard.h"
#include "font.h"
#include "display.h"
#include "desktop.h"
#include "image.h"
#include "image_service.h"
#include "gfx.h"
#include "process.h"
#include "theme.h"
#include "userspace.h"
#include "auth.h"
#include "streams.h"
#include "simd.h"
#include "module.h"
#include "audio.h"
#include "rtc.h"
#include "ata.h"
#include "ttf.h"

/* ---- GDT: 内核2 + 用户2 + TSS ---- */
static u64 gdt[6];
static struct __attribute__((packed)) { u16 len; u32 base; } gdt_ptr;
static u8  tss[104];                       /* 32 位 TSS 最小 104 字节 */

extern void gdt_flush(u32 gdtr_addr);      /* idt.asm: lgdt + 重载段寄存器 */
extern void tss_flush(void);               /* idt.asm: ltr 0x28 */

static void tss_set_esp0(u32 esp)
{
    *(u32 *)(tss + 4) = esp;               /* TSS.esp0 */
    *(u16 *)(tss + 8) = 0x10;              /* TSS.ss0  = 内核数据段 */
}

/* ---- 任务表 ---- */
static int current;                        /* IRQ 发生时正在跑的任务 */
/* 0=未持有，否则为可信当前pid+1。不能继续用“真且current==0”：
 * 用户系统调用的大帧复制也需开IRQ，那个条件会直接切走复制者，
 * 使其它任务/任务0关闭画布或换模式，形成释放后写入。编码避开
 * pid0和“没有持有”同值，公开的task_t仍完全不增字段。 */
static volatile int render_hold;
/* 只读诊断计数，不属于SCAPI/ABI。每个真实PIT在三环任务的内核
 * 整帧复制持有期间采样一次；便于验证确实开了IRQ，不靠源码推断。
 * 普通任务0合成仍使用原cpu_graphics含义，两者不能混为一类。 */
static volatile u32 render_copy_ticks;
/* mio：CPU计数独立于公共task_t，旧调试器的168B步长不变。PIT
 * 采样使用CPU压栈CS，而不是猜current对应代码特权级。只有任务0
 * 真正待机算idle；三环忙轮询即便什么也不画，仍真实算用户CPU。 */
static u32 cpu_total,cpu_idle,cpu_kernel,cpu_user,cpu_graphics;
static int cpu_ready,kernel_idle;
/* 为什么不能简单“有三环就不停yield”：旧工具用YIELD忙轮询；
 * 若任务0立刻还给同一轮询者，原本HLT节流就消失，静止桌面会
 * 烧满CPU。独立旁表记本tick真正的用户YIELD，不修改168B任务
 * 布局/保留字段。新任务以tick-1初始化，首轮允许立即执行。 */
static volatile u32 idle_dispatches; /* 私有诊断：真实任务0主动换栈次数 */
enum {QUEUE_NONE,QUEUE_READY,QUEUE_DEFERRED,QUEUE_ZOMBIE,QUEUE_WAKE};
static int ready_head=-1,ready_tail=-1,deferred_head=-1,deferred_tail=-1,zombie_head=-1;
static int wake_head=-1,wake_tail=-1;
static u32 wake_dispatches;
static u32 ready_tick;
static u64 deferred_epoch;
static volatile int kernel_pending;
static int cleanup_head=-1,cleanup_tail=-1;
static int *wait_heap;
static u32 wait_count,wait_capacity,wait_pages,wait_tick;
static u64 wait_clock;
static void wait_cancel(int pid);
static void wait_resume(int pid,u32 events);
static void wait_poll(void);
void syscall_c_common(u32 vec,u32 *frame);

static int queue_kind(task_record_t *p)
{return p->queue==QUEUE_DEFERRED && p->queue_epoch!=deferred_epoch?QUEUE_READY:p->queue;}
static void ready_remove(int pid)
{
    task_record_t *p=task_record(pid);int kind=queue_kind(p);
    if(kind!=QUEUE_READY && kind!=QUEUE_DEFERRED && kind!=QUEUE_WAKE)return;
    int *head=kind==QUEUE_WAKE?&wake_head:kind==QUEUE_READY?&ready_head:&deferred_head;
    int *tail=kind==QUEUE_WAKE?&wake_tail:kind==QUEUE_READY?&ready_tail:&deferred_tail;
    if(p->ready_prev>=0)task_record(p->ready_prev)->ready_next=p->ready_next;else *head=p->ready_next;
    if(p->ready_next>=0)task_record(p->ready_next)->ready_prev=p->ready_prev;else *tail=p->ready_prev;
    p->ready_prev=p->ready_next=-1;p->queue=QUEUE_NONE;
}
static void ready_append(int pid,int deferred)
{
    task_record_t *p=task_record(pid);int *head=deferred?&deferred_head:&ready_head;
    int *tail=deferred?&deferred_tail:&ready_tail;
    p->ready_prev=*tail;p->ready_next=-1;p->queue=deferred?QUEUE_DEFERRED:QUEUE_READY;
    p->queue_epoch=deferred_epoch;
    if(*tail>=0)task_record(*tail)->ready_next=pid;else *head=pid;*tail=pid;
}
static void ready_wake(int pid)
{
    /* 输入、管道ACK与刚启动的任务只需先处理一小段工作。把它们
     * 放到131个忙任务的队尾，实测一次键响应约2秒，串行创建还把
     * 每次ACK排队成本累加成平方增长。独立FIFO只借已有稳定对象
     * 的链字段，不加固定容量，也不按UID、可见窗口或测试名称选人。
     * 这个提前机会只到首次真正派发：随后归普通轮转；连续派发
     * 最多两次就必须给普通队首一次，持续唤醒不能饿死计算任务。 */
    task_record_t *p=task_record(pid);p->ready_prev=wake_tail;p->ready_next=-1;
    p->queue=QUEUE_WAKE;p->queue_epoch=deferred_epoch;
    if(wake_tail>=0)task_record(wake_tail)->ready_next=pid;else wake_head=pid;wake_tail=pid;
}
static void ready_advance(void)
{
    wait_poll();
    if(ready_tick==sc_ticks)return;ready_tick=sc_ticks;
    /* 老程序的YIELD仍返回0，下一tick再让它忙轮询。整条等待链
     * 拼接是常数工作；epoch使旧链上的节点立即视为就绪，不逐PID改标记。
     * 热调度没有“扫描全部任务找可运行者”的数量平方成本。 */
    if(deferred_head>=0){
        if(ready_tail>=0){task_record(ready_tail)->ready_next=deferred_head;task_record(deferred_head)->ready_prev=ready_tail;}
        else ready_head=deferred_head;
        ready_tail=deferred_tail;deferred_head=deferred_tail=-1;
    }
    deferred_epoch++;
}
void task_state_set(int pid,int state)
{
    if(pid<=0 || !task_owned(pid) || (state!=1 && state!=2 && state!=3))return;
    task_record_t *p=task_record(pid);if(p->task.state==state || p->task.state==2)return;
    ready_remove(pid);p->task.state=state;
    if(state==1){if(p->waiting){u32 events=p->events&(p->wait_mask|TASK_EVENT_TIMEOUT);if(events)wait_resume(pid,events);}
        else ready_wake(pid);}
    else if(state==2){wait_cancel(pid);p->queue=QUEUE_NONE;}
}
static int wait_before(int a,int b)
{
    u64 x=task_record(a)->wait_deadline,y=task_record(b)->wait_deadline;
    return x<y || (x==y && a<b);
}
static void wait_set(u32 index,int pid){wait_heap[index]=pid;task_record(pid)->wait_heap=(int)index;}
static u32 wait_up(u32 at,int pid)
{
    while(at){u32 parent=(at-1)/2;if(!wait_before(pid,wait_heap[parent]))break;wait_set(at,wait_heap[parent]);at=parent;}
    wait_set(at,pid);return at;
}
static void wait_down(u32 at,int pid)
{
    while(at<wait_count/2){u32 child=at*2+1;if(child+1<wait_count && wait_before(wait_heap[child+1],wait_heap[child]))child++;
        if(!wait_before(wait_heap[child],pid))break;wait_set(at,wait_heap[child]);at=child;}
    wait_set(at,pid);
}
static void wait_remove(int pid)
{
    task_record_t *p=task_record(pid);if(p->wait_heap<0)return;
    u32 at=(u32)p->wait_heap;p->wait_heap=-1;int replacement=wait_heap[--wait_count];
    if(!wait_count && wait_pages>1){pframe_free_run((u32)wait_heap,wait_pages);wait_heap=0;wait_pages=wait_capacity=0;return;}
    if(at==wait_count)return;
    if(at && wait_before(replacement,wait_heap[(at-1)/2]))(void)wait_up(at,replacement);else wait_down(at,replacement);
}
static int wait_reserve(void)
{
    if(wait_count<wait_capacity)return 0;
    u32 capacity=wait_capacity?wait_capacity*2:32;if(capacity<wait_capacity || capacity>0x1FFFFFFFu)return -1;
    u32 pages=(capacity*4+4095)/4096;int *fresh=(int *)pframe_alloc_run(pages);if(!fresh)return -1;
    for(u32 i=0;i<wait_count;i++)fresh[i]=wait_heap[i];
    if(wait_heap)pframe_free_run((u32)wait_heap,wait_pages);wait_heap=fresh;wait_pages=pages;wait_capacity=capacity;return 0;
}
static void wait_cancel(int pid)
{task_record_t *p=task_record(pid);wait_remove(pid);p->waiting=0;p->wait_mask=p->wait_frame=0;}
static void wait_resume(int pid,u32 events)
{
    task_record_t *p=task_record(pid);if(!p->waiting)return;
    if(p->task.state==3){p->events|=events;return;}
    u32 frame=p->wait_frame;p->events&=~events;wait_cancel(pid);if(frame)((u32 *)frame)[11]=events;
    if(p->task.state==1)ready_wake(pid);
}
static void wait_poll(void)
{
    /* 32位PIT自然回绕只转成单调64位逻辑时间，不用浮点/除法。
     * 定时堆只取到期队首，唤醒批次限64；剩余项促使任务0再处理。
     * 等待数量增加不会使每个IRQ扫描全部等待者。 */
    u32 tick=sc_ticks;wait_clock+=(u32)(tick-wait_tick);wait_tick=tick;
    for(u32 budget=0;wait_count && budget<64;budget++){
        int pid=wait_heap[0];if(task_record(pid)->wait_deadline>wait_clock)break;
        wait_remove(pid);wait_resume(pid,TASK_EVENT_TIMEOUT);
    }
    if(wait_count && task_record(wait_heap[0])->wait_deadline<=wait_clock)kernel_pending=1;
}
void task_notify(int pid,u32 events)
{
    if(pid<=0 || !task_owned(pid) || TASK(pid).state==2)return;
    task_record_t *p=task_record(pid);p->events|=events&255u;
    if(p->waiting && (p->events&p->wait_mask))wait_resume(pid,p->events&p->wait_mask);
}
void task_notify_generation(int pid,u32 generation,u32 events)
{if(task_owned(pid) && task_generation(pid)==generation)task_notify(pid,events);}
static int task_event_wait(u32 *frame)
{
    task_record_t *p=task_record(current);u32 mask=frame[8],timeout=frame[10];
    if(current<=0 || (mask&~255u) || (timeout>0x7FFFFFFFu && timeout!=0xFFFFFFFFu)
        || (!mask && timeout==0xFFFFFFFFu))return -1;
    u32 events=p->events&mask;if(events){p->events&=~events;return (int)events;}
    if(!timeout)return (int)TASK_EVENT_TIMEOUT;
    if(timeout!=0xFFFFFFFFu && wait_reserve())return -4;
    wait_poll();ready_remove(current);p->waiting=1;p->wait_mask=mask;p->wait_frame=(u32)frame;p->wait_heap=-1;
    if(timeout!=0xFFFFFFFFu){p->wait_deadline=wait_clock+timeout;(void)wait_up(wait_count++,current);}
    return 0;
}
void task_kernel_wake(void){kernel_pending=1;}
int task_pid(void) { return current; }
u32 task_generation(int pid){return pid>=0 && (u32)pid<NTASK?task_record(pid)->generation:0;}
void task_idle(int idle){if(current==0)kernel_idle=idle!=0;}
int task_idle_yield(void)
{
    /* 调用者已经CLI。检查与陷入之间不会发生状态变化：不选僵尸/
     * 暂停，不在渲染持有中切走。忙任务得到空闲量子的剩余时间，
     * PIT抢占/原轮转仍决定公平性，鼠标/键盘IRQ仍能及时送回任务0。
     * 所有可运行任务都已在本tick主动让出时仍走HLT，旧程序没有
     * 被强制改成等待态，也没有因为此优化改变旧输入消费语义。 */
    if(current || render_hold)return 0;
    ready_advance();kernel_pending=0;
    if(ready_head>=0 || wake_head>=0){
        u32 call=SYS_YIELD;idle_dispatches++;
        /* 必须沿已有陷入桩保存完整现场/切ESP/CR3/TSS。直接调用
         * sched_pick只会返回另一栈地址，当前C栈不会因此安全切换。
         * ring0陷入只含EIP/CS/EFLAGS，公共桩和IRET本来就兼容；
         * 原IF=0在恢复后仍为0，主循环重新处理事件后再决定待机。
         * memory屏障防止编译器把共享状态访问搬过真正换栈点。 */
        __asm__ __volatile__("int $0x7c" : "+a"(call) : : "memory","cc");
        return 1;
    }
    return 0;
}
void task_cpu_tick(u32 interrupted_cs)
{
    if(!cpu_ready)return;
    cpu_total++;
    if(current==0 && kernel_idle){cpu_idle++;return;}
    task_record(current)->running++;
    if(current>0 && (interrupted_cs&3)==3)cpu_user++;
    else {
        cpu_kernel++;
        if(current==0 && render_hold)cpu_graphics++;
        if(current>0 && render_hold==current+1)render_copy_ticks++;
    }
}
void task_cpu_info(u32 *out)
{
    /* 调用入口IF=0：全部计数一次复制，分类之和等于总样本；不能
     * 先复制total后被IRQ打断，输出无法相加的两代统计。 */
    for(int i=0;i<64;i++)out[i]=0;
    out[0]=1;out[1]=100;out[2]=sc_ticks;out[3]=cpu_total;
    out[4]=cpu_idle;out[5]=cpu_kernel;out[6]=cpu_user;out[7]=cpu_graphics;
    out[8]=LEGACY_TASK_ROWS;out[9]=1;
    for(int i=0;i<LEGACY_TASK_ROWS;i++) {
        u32 *row=out+16+i*6;row[0]=(u32)i;row[1]=(u32)TASK(i).state;
        row[2]=task_record(i)->generation;row[3]=task_record(i)->running;row[4]=task_record(i)->dispatch;
    }
}
static int task_page(int caller,u32 *out,u32 words,u32 after)
{
    if(words<32 || words>1040 || (after!=0xFFFFFFFFu && after>0x7FFFFFFFu))return -1;
    u32 capacity=(words-16)/16,count=0;int last=-1;
    for(u32 i=0;i<16+capacity*16;i++)out[i]=0;
    u32 stats[8];task_store_stats(stats);
    out[0]=1;out[1]=16;out[2]=16;out[5]=stats[1];out[6]=stats[2];out[7]=sc_ticks;
    out[8]=cpu_total;out[9]=cpu_idle;out[10]=cpu_kernel;out[11]=cpu_user;out[12]=cpu_graphics;
    out[13]=100;out[14]=stats[3];out[15]=stats[4];
    int candidate=task_next(after==0xFFFFFFFFu?-1:(int)after);
    while(candidate>=0 && count<capacity){
        task_record_t *p=task_record(candidate);u32 *row=out+16+count*16;
        row[0]=(u32)candidate;row[1]=(u32)p->task.state;row[2]=p->generation;
        row[3]=(u32)auth_uid(candidate);row[4]=(u32)auth_gid(candidate);
        if(candidate==0 || candidate==caller || auth_can_manage(caller) || auth_uid(candidate)==auth_uid(caller)){
            row[5]=p->running;row[6]=p->dispatch;row[7]=p->session;row[11]=1;
            row[13]=(u32)p->waiting;row[14]=p->wait_mask;row[15]=p->events;
            for(u32 j=0;j<12;j++)((char *)(row+8))[j]=p->task.name[j];
            if(p->task.state==2)row[12]=(u32)p->exit_code;
        }
        last=candidate;count++;candidate=task_next(candidate);
    }
    out[3]=count;out[4]=candidate>=0?(u32)last:0xFFFFFFFFu;return (int)count;
}
void task_render_hold(int hold)
{
    /* 只在短IF=0区切换，由当前可信零环调用者获取/释放。随后
     * STI时IRQ继续处理驱动和EOI，调度尾部保持同一现场/CR3。
     * 主循环合成与三环的内核复制共用生命周期保护，没有再分配
     * 六份大屏快照。释放只接受当前持有者，不清别人的持有。
     * 没有三环系统调用能设置此值；不允许在持有中调用YIELD或
     * 嵌套获取，所有当前路径都是一对获取/复制/释放。 */
    if(hold){if(!render_hold)render_hold=current+1;}
    else if(render_hold==current+1)render_hold=0;
}

/* 功能号统一使用 task.h，避免头文件、分发器与应用文档各养一份常量。 */

void task_init(void)
{
    u32 flags;__asm__ __volatile__("pushfl;popl %0":"=r"(flags));cli();
    paging_init();                        /* 先开分页 (恒等映射, 环境不变) */
    u32 sizes[TASK_DATA_COUNT]={auth_task_bytes(),auth_login_bytes(),userspace_task_bytes(),
        process_task_bytes(),streams_task_bytes(),simd_task_bytes(),image_service_task_bytes(),wm_task_bytes()};
    if(task_store_init(sizes))panic("TASK MEMORY",0);

    gdt[0] = 0;
    gdt[1] = 0x00CF9A000000FFFFULL;       /* 0x08 内核代码 */
    gdt[2] = 0x00CF92000000FFFFULL;       /* 0x10 内核数据 */
    gdt[3] = 0x00CFFA000000FFFFULL;       /* 0x18 用户代码 DPL3 */
    gdt[4] = 0x00CFF2000000FFFFULL;       /* 0x20 用户数据 DPL3 */
    {
        u32 base = (u32)tss;
        /* TSS 描述符字节: [4]=基址16-23 [5]=属性0x89 [6]=0 [7]=基址24-31
         * 【M5 调试实录】基址高位和属性位置曾摆反, 切换即 GP */
        u32 lo = 0x0067u | ((base & 0xFFFFu) << 16);
        u32 hi = ((base >> 16) & 0xFFu)
               | (0x89u << 8)
               | ((base >> 24) & 0xFFu) << 24;
        gdt[5] = lo | ((u64)hi << 32);    /* 0x28 TSS 描述符 */
    }
    gdt_ptr.len = (u16)(sizeof(gdt) - 1);
    gdt_ptr.base = (u32)gdt;
    gdt_flush((u32)&gdt_ptr);
    /* I/O位图起点在TSS限长之外，所有三环IN/OUT均GP。BSS默认0
     * 会把TSS本体误当位图，不能靠UART端口恰好在界外侥幸保护。 */
    *(u16 *)(tss+102)=sizeof(tss);
    tss_flush();

    TASK(0).state = 1;                   /* 任务 0 = 内核主循环 */
    TASK(0).pd = paging_kernel_pd();
    TASK(0).kstack_top = 0x90000;        /* 引导栈 (恒等映射) */
    TASK(0).saved_esp = 0;
    current = 0;
    render_hold=0;render_copy_ticks=0;idle_dispatches=0;
    cpu_total=cpu_idle=cpu_kernel=cpu_user=cpu_graphics=0;
    task_record(0)->generation=1;task_record(0)->last_yield=sc_ticks-1;kernel_idle=0;
    ready_head=ready_tail=deferred_head=deferred_tail=zombie_head=-1;
    wake_head=wake_tail=-1;wake_dispatches=0;
    cleanup_head=cleanup_tail=-1;
    ready_tick=sc_ticks;deferred_epoch=1;kernel_pending=0;
    wait_clock=0;wait_tick=sc_ticks;wait_count=wait_capacity=wait_pages=0;wait_heap=0;task_record(0)->wait_heap=-1;
    process_reset(0);
    simd_init();                          /* 对象先存在，任务0才能保存扩展现场。 */
    streams_init();
    cpu_ready=1;
    tss_set_esp0(0x90000);
    if(flags&0x200u)sti();
}

int task_spawn(const char *name, u32 pd, u32 entry, u32 user_esp)
{
    int i=task_store_reserve();if(i<0)return -1;
    {
        u32 ks = pframe_alloc();          /* 内核栈一页 (恒等映射可达) */
        if(!ks){task_store_cancel(i);return -1;}

        /* 预置"初始现场", iretd 直接落进 ring3:
         * 【M5 调试实录】CS/SS 必须带 RPL3 (0x1B/0x23), 否则 GP */
        u32 *f = (u32 *)(ks + 4096 - 76);
        for (int k = 0; k < 19; k++)
            f[k] = 0;
        for (int k = 0; k < 4; k++) f[k] = 0x23; /* 用户数据段不能恢复为空选择子 */
        f[14] = entry;                    /* EIP */
        f[15] = 0x1B;                     /* CS = 0x18|RPL3 */
        f[16] = 0x202;                    /* EFLAGS: IF=1 */
        f[17] = user_esp;                 /* ESP = 用户栈 */
        f[18] = 0x23;                     /* SS = 0x20|RPL3 */

        /* 一切可能失败的物理资源先取得，再提交新代数/名字/身份。
         * reserve只持有编号，不擦除兼容状态历史；创建失败不会把上次
         * 退出码变成另一个任务。新对象的关联旁表已逐块清零。 */
        u8 *prefix=(u8 *)&TASK(i);for(u32 k=0;k<sizeof(task_t);k++)prefix[k]=0;
        task_record_t *record=task_record(i);record->queue=QUEUE_NONE;
        record->ready_prev=record->ready_next=record->zombie_next=record->cleanup_next=-1;
        record->exit_code=0;record->session=0;record->session_previous=record->session_next=-1;
        record->transactions=record->windows=record->captures=0;
        record->events=record->wait_mask=record->wait_frame=0;record->waiting=0;record->wait_heap=-1;
        record->deferred_draw=0;record->visibility_aware=0;record->cleanup_phase=0;
        process_reset(i);
        task_record(i)->generation++;if(!task_record(i)->generation)task_record(i)->generation=1;
        task_record(i)->running=task_record(i)->dispatch=0;
        task_record(i)->last_yield=sc_ticks-1; /* 槽复用不能继承前任的本tick节流 */
        TASK(i).pd = pd;
        TASK(i).kstack_top = ks + 4096;
        TASK(i).saved_esp = (u32)f;
        TASK(i).entry = entry;
        TASK(i).user_esp = user_esp;
        task_state_set(i,1);
        auth_spawn(i,current);
        userspace_spawn(i,current);
        streams_spawn(i,current);
        simd_spawn(i);
        for (int k = 0; k < 11; k++) {
            TASK(i).name[k] = name[k];
            if (!name[k]) break;
        }
        TASK(i).name[11] = 0;
        task_store_commit(i);
        return i;
    }
}

/* ---------------- 调度 ---------------- */
u32 sched_pick(u32 cur_esp)
{
    /* 引导早期IRQ可在任务对象建立前到达，不能恢复尚不存在的栈。 */
    if(!cpu_ready)return cur_esp;
    /* IRQ在完整保存现场、驱动处理及EOI后到这里。合成/整帧复制
     * 已开中断，PS/2和计时继续；只有持有者匹配才原路返回当前
     * 内核栈，既不切CR3，也不执行僵尸回收。解除后恢复原轮转，
     * 不能让复制还引用的源用户页或目标画布被释放复用。 */
    if(render_hold==current+1) return cur_esp;
    /* 【回收为什么延后一轮】
     * EXIT 的 C 分发器只是把任务置为 2；这时 ESP 仍在它自己的内核栈上。
     * 立刻 free 该栈，下一次分配就可能覆盖本函数的返回地址。
     * 这里排除 current，只回收更早已经切走的僵尸：先释放私有地址空间，
     * 再释放内核栈，最后 state=0 才允许 task_spawn 复用任务槽。
     * 调度入口的 IF=0，所以回收与分配不会在两条指令之间互相穿插。 */
    /* 每次只收有限个退出对象，成本跟真正的待回收资源相关，不扫
     * 历史高水位。当前栈尚在使用，跳过后续出口再收，避免释放自身。 */
    int previous=-1,i=zombie_head;
    for(int budget=0;i>=0 && budget<4;budget++){
        task_record_t *dead=task_record(i);int following=dead->zombie_next;
        if(i==current){previous=i;i=following;continue;}
        if(previous<0)zombie_head=following;else task_record(previous)->zombie_next=following;
            paging_free_task_dir(TASK(i).pd);
            pframe_free(TASK(i).kstack_top - PAGE_SIZE);
            simd_stop(i);
            TASK(i).state=0;TASK(i).pd=TASK(i).kstack_top=TASK(i).saved_esp=0;
            dead->queue=QUEUE_NONE;dead->zombie_next=-1;task_store_release(i);i=following;
    }
    TASK(current).saved_esp = cur_esp;
    ready_advance();
    /* 任务0处理设备、窗口与服务，不排到几千个10ms量子之后。
     * 三环主动让出/硬件IRQ均先回主循环；主循环完成工作即取队首。
     * 用户队列只在真正得到CPU时轮转，稳定轮询者不会压过其它用户。 */
    int next=0;
    if(!current && !kernel_pending && cleanup_head<0 && (ready_head>=0 || wake_head>=0)){
        if(wake_head>=0 && (ready_head<0 || wake_dispatches<2)){
            next=wake_head;if(wake_dispatches<2)wake_dispatches++;
        }else {next=ready_head;wake_dispatches=0;}
        ready_remove(next);ready_append(next,0);
    }
    if (next != current) {
        simd_switch(current,next);
        current = next;
        task_record(current)->dispatch++;
        tss_set_esp0(TASK(current).kstack_top);
        paging_switch(TASK(current).pd); /* 换地址空间 */
        if(next>0 && task_record(next)->deferred_draw){
            /* 历史SCX原字节没有可见性协议。保留首次未执行的绘制陷入，
             * 等目标窗口能显示后才在它的CR3验证/执行；源用户页一直归
             * 该存活任务，不能在任务0地址空间猜测用户指针。旧返回值、
             * 忽略寄存器和增量画布操作因此不被“隐藏直接丢帧”破坏。
             * 分发仍用公共整数实现，长复制沿原render_hold保护生命周期。
             * 新GUI声明可见性后自己停止生成画面，后台服务不会被此路径停车。 */
            task_record_t *p=task_record(next);u32 *frame=(u32 *)TASK(next).saved_esp;
            frame[11]=p->deferred_draw;p->deferred_draw=0;syscall_c_common(0x7C,frame);
            if(p->waiting){simd_switch(current,0);current=0;tss_set_esp0(TASK(0).kstack_top);paging_switch(TASK(0).pd);}
        }
    }
    return TASK(current).saved_esp;
}

u32 syscall_finish(u32 cur_esp)
{
    /* 【为什么不在每次绘字之后切任务】
     * GUI 一次重绘可能调用几十次 TXT/FILL；若每次都切回任务0的 hlt，
     * 每个字符要再等一次 PIT，一行文字就从毫秒变成秒，按键队列很快满。
     * 普通调用直接恢复当前现场，100Hz PIT 仍保证任何程序不能霸占 CPU。
     * EXIT 则必须立刻离开僵尸代码；它仍走完整 sched_pick 和安全回收链。
     * 这个函数只决定出口，不在 C 里直接改 ESP；真正换栈由汇编桩完成。 */
    u32 *frame=(u32 *)cur_esp;
    return TASK(current).state==1 && !task_record(current)->waiting && frame[12]!=0x107C ? cur_esp : sched_pick(cur_esp);
}

/* ---------------- 系统调用 (int 0x7C, DPL3) ----------------
 * frame 是 idt.asm 保存现场的起点，不是 CPU 原始压栈的起点。
 * 四个段寄存器占 [0..3]，pusha 逆序占 [4..11]：
 *   [4]=EDI [5]=ESI [6]=EBP [7]=旧ESP [8]=EBX [9]=EDX [10]=ECX [11]=EAX。
 * 返回值必须写 frame[11]，否则 popa 会把 C 侧 eax 覆盖回旧的功能号。
 * 软中断只换 CPL 与栈，不换 CR3；所以用户虚址在这里仍有效，
 * 但必须先确认它属于本任务且映射在场，不能信任 ring3 传来的整数。 */
static char *uptr(u32 p)
{
    /* 系统调用仍在调用者 CR3 中，直接访问用户虚址，不假定物理页连续。 */
    return paging_user_range(TASK(current).pd, p, 1) ? (char *)p : 0;
}

static char *ustr(u32 p)
{
    /* 字符串没有显式长度，逐字节检查终止符，同时确认所跨过的每一页。
     * 4096 是扫描上限而非映射大小；超过上限仍无 NUL 就判失败，
     * 防止应用把输出或 EXEC 变成无限扫描。检查先于解引用。 */
    for (u32 i = 0; i < 4096; i++) {
        if (!paging_user_range(TASK(current).pd, p+i, 1)) return 0;
        if (!((char *)p)[i]) return (char *)p;
    }
    return 0;
}

void task_stop(int pid)
{
    /* pid=0 是桌面主循环，不能经 GUI 红叉关闭；可运行/暂停任务都可变僵尸。
     * 窗口先回收，下一帧立即显露下层内容；页与栈交给 sched_pick 延后释放。
     * 同一接口供 SYS_EXIT 和窗口红叉使用，避免两条退出路径语义漂移。 */
    if (pid <= 0 || pid >= NTASK || TASK(pid).state == 0 || TASK(pid).state == 2) return;
    task_state_set(pid,2);          /* 先改变状态，避免关闭调试子进程时重复进入 */
    task_record(pid)->cleanup_next=-1;
    if(cleanup_tail>=0)task_record(cleanup_tail)->cleanup_next=pid;else cleanup_head=pid;
    cleanup_tail=pid;
    task_kernel_wake();
}
int task_cleanup_pending(void){return cleanup_head>=0;}
void task_cleanup_poll(void)
{
    /* 动态树/窗口/作业不能一次递归扫完。每轮64个资源阶段、每任务轮转，
     * 主循环其余输入/设备每轮照常服务；关联授权全撤销前暂不恢复三环，
     * 防止父进程关闭后，尚在清理队列末尾的孩子继续使用继承能力。
     * 只有最后阶段才入真正的僵尸回收链，旁表/订阅游标/当前栈仍有效。
     * 这是清理操作数预算，不宣称一次磁盘事务或窗口重排有固定耗时。 */
    if(current || render_hold)return;
    u32 budget=64;
    while(cleanup_head>=0 && budget--){
        int owner=cleanup_head;task_record_t *p=task_record(owner);cleanup_head=p->cleanup_next;
        if(cleanup_head<0)cleanup_tail=-1;p->cleanup_next=-1;int advance=1;
        switch(p->cleanup_phase){
        case 0:image_service_owner_stopped(owner);userspace_stop(owner,process_status(owner));audio_stop_owner(owner);break;
        case 1:advance=streams_stop_step(owner,process_status(owner));break;
        case 2:advance=process_detach_owner_step(owner);break;
        case 3:advance=wm_close_owner_step(owner);break;
        case 4:advance=wm_capture_stop_step(owner);break;
#ifdef M10_DISK_CORE
        case 5:advance=network_stop_owner_step(owner);break;
        case 6:auth_stop(owner);break;
#else
        case 5:auth_stop(owner);break;
#endif
        default:break;
        }
        if(advance)p->cleanup_phase++;
        if(p->cleanup_phase>=
#ifdef M10_DISK_CORE
            7
#else
            6
#endif
        ){p->queue=QUEUE_ZOMBIE;p->zombie_next=zombie_head;zombie_head=owner;}
        else{
            if(cleanup_tail>=0)task_record(cleanup_tail)->cleanup_next=owner;else cleanup_head=owner;
            cleanup_tail=owner;
        }
    }
    if(cleanup_head>=0)task_kernel_wake();
}

void syscall_c_common(u32 vec, u32 *frame)
{
    (void)vec;
    u32 call=frame[11];int defer=0;
    if(current>0 && !task_record(current)->visibility_aware){
        /* 先排除坏用户指针；旧接口的非法输入仍立即给出原错误。
         * 句柄/格式不属于本人时不等，随后原分发器保留旧拒绝语义。 */
        if(call==SYS_PUTS && ustr(frame[8]))defer=wm_defer_draw(current,0,-1);
        else if((call==SYS_TXT || call==SYS_UTF8TEXT || call==SYS_TEXTRGB) && ustr(frame[5])
            && (call==SYS_TXT || ((int)frame[10]>=-32768 && (int)frame[10]<=32767 && (int)frame[9]>=-32768 && (int)frame[9]<=32767)))
            defer=wm_defer_draw(current,(int)frame[8],call==SYS_TEXTRGB?2:-1);
        else if((call==SYS_FILL || call==SYS_FILLRGB) && (int)frame[5]>0 && (int)frame[4]>0)
            defer=wm_defer_draw(current,(int)frame[8],call==SYS_FILLRGB?2:-1);
        else if((call==SYS_FRAME || call==SYS_FRAME32) && frame[9]<=1920u*1080u*(call==SYS_FRAME32?4u:1u)
            && paging_user_range(TASK(current).pd,frame[10],frame[9]))
            {i32 info[5];if(!wm_user_info(current,(int)frame[8],info) && frame[9]==(u32)info[2]*info[3]*(call==SYS_FRAME32?4u:1u))
                defer=wm_defer_draw(current,(int)frame[8],call==SYS_FRAME32?2:1);}
    }
    if(defer){task_record_t *p=task_record(current);ready_remove(current);p->waiting=1;p->wait_mask=TASK_EVENT_VISIBILITY;
        p->wait_heap=-1;p->wait_frame=(u32)frame;p->deferred_draw=call;frame[11]=0;return;}
    /* pusha 布局对应的 u32 下标: EDI=4 ESI=5 EBP=6 旧ESP=7
     * EBX=8 EDX=9 ECX=10 EAX=11 */
    switch (frame[11]) {                  /* EAX = 功能号 */
    case SYS_PUTS: {
        const char *text=ustr(frame[8]);
        int result=text?wm_user_puts(current,text):-1;
        /* 有本人窗口仍保留旧路由；无窗口CLI才使用内核记下的授权。
         * 非法字符串不能降级另一路由，错误UTF-8也不产生部分输出。 */
        if(text && result==-1)result=userspace_puts(current,text);
        if(text && result==-1){u32 n=0;while(text[n])n++;result=streams_write(current,1,text,n);}
        frame[11]=(u32)result;break;
    }
    case SYS_GETTICK:
        frame[11] = sc_ticks;
        break;
    case SYS_USERALLOC:
        frame[11]=paging_user_alloc(TASK(current).pd,frame[8]);break;
    case SYS_USERFREE:
        frame[11]=(u32)paging_user_free(TASK(current).pd,frame[8]);break;
    case SYS_STATUS:
        frame[11]=(u32)process_status((int)frame[8]); break;
    case SYS_YIELD:
        /* 只记录真实三环调用，本来的0返回/立即轮转仍保持。旧
         * 忽略寄存器继续忽略；任务0内部陷入不能替用户伪造让出。 */
        if(current>0){task_record(current)->last_yield=sc_ticks;ready_remove(current);ready_append(current,1);}
        frame[11]=0; frame[12]=0x107C; break; /* 只标记出口，让汇编统一换 ESP */
    case SYS_EXCHANDLER:
        frame[11]=(u32)process_handler((int)frame[8],frame[10]); break;
    case SYS_EXCRETURN:
        if(process_exception_return(frame,frame[8])) frame[11]=(u32)-1;
        break; /* 成功时保留恢复现场的 EAX，不能覆盖成普通 syscall 返回值 */
    case SYS_GETARGS: {
        /* 参数已在 EXEC 时拷贝到任务槽，父任务退出也不会留下悬空指针。
         * 调用者显式提供容量；复制最多 max-1 字节，始终保留末尾 NUL。
         * 返回实际复制长度，便于 CLI 在空参数时显示用法。 */
        u32 max = frame[9], used = 0;
        if (!max || !paging_user_range(TASK(current).pd, frame[8], max)) {
            frame[11] = (u32)-1; break;
        }
        char *out = uptr(frame[8]);
        while (used+1 < max && TASK(current).args[used]) {
            out[used] = TASK(current).args[used]; used++;
        }
        out[used] = 0;
        frame[11] = used;
        break;
    }
    case SYS_EXIT:
        process_exit(current,(int)frame[8]);
        task_stop(current);
        frame[11] = 0;
        break;
    case SYS_WINOPEN: {                   /* EBX=标题 ECX=宽 EDX=高 */
        const char *src = ustr(frame[8]);
        if (!src) { frame[11] = (u32)-1; break; }
        frame[11] = (u32)wm_open_user_window(current, src,
                                             (int)frame[10], (int)frame[9]);
        break;
    }
    case SYS_WINCLOSE:                    /* EBX = 窗口句柄 */
        wm_close_user_window(current, (int)frame[8]);
        frame[11] = 0;
        break;
    case SYS_TXT:                         /* EBX=句柄 ECX=x EDX=y ESI=串 EDI=色 */
        if (!ustr(frame[5])) { frame[11] = (u32)-1; break; }
        wm_user_text(current, (int)frame[8], (int)frame[10], (int)frame[9],
                     uptr(frame[5]), (u8)frame[4]);
        frame[11] = 0;
        break;
    case SYS_FILL:                        /* EBX=句柄 ECX=x EDX=y ESI=宽 EDI=高 EBP=色 */
        wm_user_fill(current, (int)frame[8], (int)frame[10], (int)frame[9],
                     (int)frame[5], (int)frame[4], (u8)frame[6]);
        frame[11] = 0;
        break;
    case SYS_GETKEY:
        frame[11] = (u32)wm_user_getkey(current);
        break;
    case SYS_KEYPEEK:
        frame[11] = (u32)wm_user_peekkey(current);
        break;
    case SYS_MOUSE:                       /* 打包: x9b y9b 按键3b... 简化 x|y<<9|btn<<18 */
        frame[11] = wm_legacy_mouse(current);
        break;
    case SYS_FRAME:
        if(frame[9]>1920u*1080u || !paging_user_range(TASK(current).pd,frame[10],frame[9])) frame[11]=(u32)-1;
        else frame[11]=(u32)wm_user_frame(current,(int)frame[8],(u8 *)frame[10],frame[9]);
        break;
    case SYS_KEYDOWN:
        frame[11]=wm_focused(current)?(u32)keyboard_down(frame[8]):0; break;
    case SYS_FONT8:
        if(!paging_user_range(TASK(current).pd,frame[8],1024)) { frame[11]=(u32)-1; break; }
        for(int c=0;c<128;c++)gfx_font8((u32)c,(u8 *)frame[8]+c*8);
        frame[11]=1024; break;
    case SYS_GLYPH16:
        /* 返回行是位图副本，不泄露 SCF 字符串或 supervisor 指针。
         * 标量和完整输出区都先检查，错误调用不写任何半个字形。 */
        if(frame[8]>0x10FFFF || (frame[8]>=0xD800 && frame[8]<=0xDFFF)
           || !paging_user_range(TASK(current).pd,frame[10],32)) frame[11]=(u32)-1;
        else frame[11]=(u32)gfx_glyph16(frame[8],(u16 *)frame[10]);
        break;
    case SYS_UTF8TEXT: {
        const char *text=ustr(frame[5]);
        if(!text) { frame[11]=(u32)-1; break; }
        /* 整串先验证再画；否则发现最后一个坏字节时，用户会得到
         * “失败但已经画出前半串”的难以恢复状态。缺字是合法编码，
         * 仍绘制占位框；编码错误与字库未收录必须分开处理。 */
        if(!gfx_utf8_valid(text)) { frame[11]=(u32)-2; break; }
        frame[11]=(u32)wm_user_utf8(current,(int)frame[8],(int)frame[10],
                                    (int)frame[9],text,(u8)frame[4]);
        break;
    }
    case SYS_FONTINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],16)) frame[11]=(u32)-1;
        else { gfx_font_info((u32 *)frame[8]); frame[11]=0; }
        break;
    case SYS_WALLPAPER: {
        char path[64];frame[11]=session_config_path("WALL.CFG",path)>=0 && fs_user_mutable(path,0) && fs_access(current,path,FS_ACCESS_WRITE)
            ?(u32)wm_set_wallpaper((int)frame[8]):(u32)-5;break;
    }
    case SYS_WININFO:
        if(!paging_user_range(TASK(current).pd,frame[10],20)) { frame[11]=(u32)-1; break; }
        frame[11]=(u32)wm_user_info(current,(int)frame[8],(i32 *)frame[10]);
        break;
    case SYS_FSREAD:                      /* EBX=名 ECX=缓冲 EDX=上限 */
        if (!ustr(frame[8]) || !paging_user_range(TASK(current).pd, frame[10], frame[9])) {
            frame[11] = (u32)-1; break;
        }
        if(!fs_access(current,ustr(frame[8]),FS_ACCESS_READ)){frame[11]=(u32)-5;break;}
        frame[11] = (u32)fs_read(uptr(frame[8]), uptr(frame[10]), frame[9]);
        break;
    /* 以下M8调用先检查完整输出映射，才交给各子系统；新增原生接口
     * 不改变旧WINOPEN/MOUSE布局，旧应用无需重新编译才能启动。 */
    case SYS_FSREADAT:
        if(!ustr(frame[8]) || !paging_user_range(TASK(current).pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else if(!fs_access(current,ustr(frame[8]),FS_ACCESS_READ))frame[11]=(u32)-5;
        else frame[11]=(u32)fs_read_at(ustr(frame[8]),(void *)frame[10],frame[9],frame[5]);
        break;
    case SYS_DISPLAYINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],32))frame[11]=(u32)-1;
        else {display_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_DISPLAYAPPLY:
        if(!fs_user_mutable("SYS/DISPLAY.CFG",0) || !fs_access(current,"SYS/DISPLAY.CFG",FS_ACCESS_WRITE))frame[11]=(u32)-5;
        else frame[11]=(u32)display_apply((int)frame[8],(int)frame[10],(int)frame[9],(int)frame[5]);break;
    case SYS_PALETTEINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],1024))frame[11]=(u32)-1;
        else {display_palette((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_THEMEINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],128))frame[11]=(u32)-1;
        else {theme_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_THEMELOAD: {
        /* action=2重读固定配置不使用用户字符串；其它动作先复制验证
         * 完整路径。解析/写盘成功才换快照，失败不能改掉当前主题。 */
        const char *path=frame[10]==2?0:ustr(frame[8]);
        if(path && !fs_access(current,path,FS_ACCESS_READ))frame[11]=(u32)-5;
        else if(frame[10]==1)frame[11]=path?(u32)theme_load(path,1):(u32)-1;
        else frame[11]=frame[10]!=2 && !path?(u32)-1:(u32)theme_load(path,(int)frame[10]);break;
    }
    case SYS_THEMEPATH:
        if(!frame[9] || frame[9]>64 || !paging_user_range(TASK(current).pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else frame[11]=(u32)theme_path((int)frame[8],(char *)frame[10],frame[9]);
        break;
    case SYS_POINTER:
    case SYS_POINTERPEEK:
        if(!paging_user_range(TASK(current).pd,frame[10],24))frame[11]=(u32)-1;
        else frame[11]=(u32)(frame[11]==SYS_POINTERPEEK?wm_user_pointer_peek(current,(int)frame[8],(i32 *)frame[10])
                            :wm_user_pointer(current,(int)frame[8],(i32 *)frame[10]));
        break;
    case SYS_MOUSECAPTURE:
        frame[11]=(u32)wm_user_mouse_capture(current,(int)frame[8],(int)frame[10]);break;
    case SYS_MOUSERELATIVE:
        if(!paging_user_range(TASK(current).pd,frame[10],32))frame[11]=(u32)-1;
        else frame[11]=(u32)wm_user_mouse_relative(current,(int)frame[8],(i32 *)frame[10]);
        break;
    case SYS_WINOPEN2: {
        const char *title=ustr(frame[8]);
        frame[11]=title?(u32)wm_open_native_window(current,title,(int)frame[10],(int)frame[9]):(u32)-1;break;
    }
    case SYS_WINRGB: {
        const char *title=ustr(frame[8]);
        frame[11]=title?(u32)wm_open_rgb_window(current,title,(int)frame[10],(int)frame[9]):(u32)-1;break;
    }
    case SYS_FRAME32:
        if(frame[9]>1920u*1080u*4 || !paging_user_range(TASK(current).pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else frame[11]=(u32)wm_user_frame_rgb(current,(int)frame[8],(const u32 *)frame[10],frame[9]);
        break;
    case SYS_FILLRGB:
        frame[11]=(u32)wm_user_fill_rgb(current,(int)frame[8],(int)frame[10],(int)frame[9],(int)frame[5],(int)frame[4],frame[6]);break;
    case SYS_TEXTRGB: {
        const char *text=ustr(frame[5]);
        if(!text)frame[11]=(u32)-1;
        else if(!gfx_utf8_valid(text))frame[11]=(u32)-2;
        else frame[11]=(u32)wm_user_text_rgb(current,(int)frame[8],(int)frame[10],(int)frame[9],text,frame[4]);
        break;
    }
    case SYS_WINCONTROL:
        frame[11]=(u32)wm_window_control(current,(int)frame[8],(int)frame[10]);break;
    case SYS_DESKTOPRELOAD:
        frame[11]=(u32)desktop_reload();if(session_current()==session_active())wm_request_compose();break;
    case SYS_MONITOR: {
        /* 统计是只读快照，绝不包含内核指针；已用页不是CPU利用率。
         * 8个头字段+8行20B任务，空槽同样记录，便于观测退出回收。 */
        if(!paging_user_range(TASK(current).pd,frame[8],192)){frame[11]=(u32)-1;break;}
        u32 *out=(u32 *)frame[8];for(int i=0;i<48;i++)out[i]=0;
        out[0]=1;out[1]=sc_ticks;out[2]=memory_managed_bytes();
        out[3]=memory_used_bytes();out[4]=memory_free_bytes();out[5]=LEGACY_TASK_ROWS;out[6]=fs_count();
        for(int i=0;i<LEGACY_TASK_ROWS;i++) {
            u32 *row=out+8+i*5;row[0]=i;row[1]=TASK(i).state;
            for(int j=0;j<12;j++)((char *)(row+2))[j]=TASK(i).name[j];
            if(TASK(i).state==1)out[7]++;
        }
        frame[11]=0;break;
    }
    case SYS_CPUINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],256))frame[11]=(u32)-1;
        else {task_cpu_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_STORAGEINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],128))frame[11]=(u32)-1;
        else {fs_storage_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_FSWRITE:                     /* EBX=名 ECX=数据 EDX=长度 */
        if (!ustr(frame[8]) || !paging_user_range(TASK(current).pd, frame[10], frame[9])) {
            frame[11] = (u32)-1; break;
        }
        if(!fs_user_mutable(ustr(frame[8]),0) || !fs_access(current,ustr(frame[8]),FS_ACCESS_WRITE)) { frame[11]=(u32)-5; break; }
        frame[11] = (u32)fs_write(uptr(frame[8]),
                                  (const u8 *)uptr(frame[10]), frame[9]);
        break;
    case SYS_FSLIST: {                    /* EBX=缓冲 EDX=上限；每行“路径 空格 大小 LF” */
        char *out = uptr(frame[8]);
        u32 max = frame[9], used = 0;
        if (!max || !paging_user_range(TASK(current).pd, frame[8], max)) {
            frame[11] = (u32)-1; break;
        }
        for (int i = 0; i < fs_count() && used + 80 < max; i++) {
            const char *n = fs_name(i);
            if(!fs_access(current,n,FS_ACCESS_META))continue;
            while (*n) out[used++] = *n++;
            out[used++] = ' ';
            char b[12]; u32 v = fs_size(i); int k = 0;
            do { b[k++] = (char)('0' + v % 10); v /= 10; } while (v);
            while (k--) out[used++] = b[k];
            out[used++] = '\n';
        }
        out[used] = 0;
        frame[11] = used;
        break;
    }
    case SYS_EXEC:                        /* EBX = SCX 路径 */
        frame[11] = ustr(frame[8]) ? (u32)task_exec_scx(ustr(frame[8])) : (u32)-1;
        break;
    case SYS_USERQUERY: {
        const char *name=frame[8]==4?ustr(frame[10]):0;
        if(!frame[5] || frame[5]>256 || !paging_user_range(TASK(current).pd,frame[9],frame[5])
           || (frame[8]==4 && !name))frame[11]=(u32)-1;
        else frame[11]=(u32)userspace_query(current,(int)frame[8],name,(char *)frame[9],frame[5]);
        break;
    }
    case SYS_USERCONFIG: {
        const char *path=frame[9]==2?0:ustr(frame[10]);
        if(frame[9]==1 && !auth_can_manage(current))frame[11]=(u32)-5;
        else frame[11]=frame[9]!=2 && !path?(u32)-1:(u32)userspace_config((int)frame[8],path,(int)frame[9]);break;
    }
    case SYS_CONFIGCHECK: {
        const char *path=ustr(frame[10]);int result=-1;
        if(path && fs_access(current,path,FS_ACCESS_READ)){
            int kind=(int)frame[8];
            if(kind==0 || kind==1)result=desktop_config_check(kind,path);
            else if(kind==2)result=theme_check(path);
            else if(kind==3)result=display_config_check(path);
            else if(kind==4){
                u32 info[2];u8 text[16];
                if(fs_stat(path,info) || info[0]!=1 || !info[1] || info[1]>sizeof(text))result=-2;
                else if(fs_read(path,text,info[1])!=(int)info[1])result=-2;
                else {result=text[0]>='0' && text[0]<='2'?0:-1;for(u32 i=1;i<info[1];i++)if(text[i]!='\r' && text[i]!='\n')result=-1;}
            }
        }
        frame[11]=(u32)result;break;
    }
    case SYS_IMAGEREQUEST: {
        const char *path=ustr(frame[8]);
        frame[11]=path?(u32)image_service_request(current,path):(u32)-1;break;
    }
    case SYS_IMAGERESULT:
        /* 独立新快照只写32B，不复用旧图形/监控的预留槽。像素范围
         * 由服务在宽高已知后再全量检查；无像素指针时只允许容量0。 */
        if(!paging_user_range(TASK(current).pd,frame[10],32))frame[11]=(u32)-1;
        else frame[11]=(u32)image_service_result(current,frame[8],(u32 *)frame[10],(void *)frame[9],frame[5]);
        break;
    case SYS_IMAGECLAIM:
        if(!frame[9] || frame[9]>64 || !paging_user_range(TASK(current).pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else frame[11]=(u32)image_service_claim(current,frame[8],(char *)frame[10],frame[9]);
        break;
    case SYS_IMAGESUBMIT:
        frame[11]=(u32)image_service_submit(current,frame[8],frame[10],frame[9],frame[5],(int)frame[4]);break;
    case SYS_IMAGECANCEL:
        frame[11]=(u32)image_service_cancel(current,frame[8]);break;
    case SYS_CHDIR: {
        const char *path=ustr(frame[8]);
        frame[11]=path?(u32)userspace_chdir(current,path):(u32)-1;break;
    }
    case SYS_RESOLVE: {
        const char *path=ustr(frame[8]);
        if(!path || !frame[9] || frame[9]>64 || !paging_user_range(TASK(current).pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else frame[11]=(u32)userspace_resolve(current,path,(char *)frame[10],frame[9]);
        break;
    }
    case SYS_CLIRUN: {
        const char *command=ustr(frame[8]);
        frame[11]=command?(u32)userspace_cli_run(current,command,(int)frame[10]):(u32)-1;break;
    }
    case SYS_JOBSTATUS:
        frame[11]=(u32)userspace_job_status(current,frame[8]);break;
    case SYS_TERMINAL:
        frame[11]=(u32)wm_terminal_control(current,(int)frame[8],(int)frame[10]);break;
    case SYS_FSDIR:
        if(!ustr(frame[8]) || !frame[9] || !paging_user_range(TASK(current).pd,frame[10],frame[9])) frame[11]=(u32)-1;
        else if(!fs_access(current,ustr(frame[8]),FS_ACCESS_READ))frame[11]=(u32)-5;
        else frame[11]=(u32)fs_list_dir(ustr(frame[8]),(char *)frame[10],frame[9]);
        break;
    case SYS_MKDIR:
    case SYS_REMOVE:
        if(!ustr(frame[8])) { frame[11]=(u32)-1; break; }
        if(!fs_user_mutable(ustr(frame[8]),1) || !fs_access_parent(current,ustr(frame[8]))) { frame[11]=(u32)-5; break; }
        frame[11]=(u32)(frame[11]==SYS_MKDIR ? fs_mkdir(ustr(frame[8])) : fs_remove(ustr(frame[8])));
        break;
    case SYS_RENAME:
        if(!ustr(frame[8]) || !ustr(frame[10])) { frame[11]=(u32)-1; break; }
        if(!fs_user_mutable(ustr(frame[8]),1) || !fs_user_mutable(ustr(frame[10]),1)
            || !fs_access_parent(current,ustr(frame[8])) || !fs_access_parent(current,ustr(frame[10]))) { frame[11]=(u32)-5; break; }
        frame[11]=(u32)fs_rename(ustr(frame[8]),ustr(frame[10])); break;
    case SYS_STAT:
        if(!ustr(frame[8]) || !paging_user_range(TASK(current).pd,frame[10],8)) frame[11]=(u32)-1;
        else if(!fs_access(current,ustr(frame[8]),FS_ACCESS_META))frame[11]=(u32)-5;
        else frame[11]=(u32)fs_stat(ustr(frame[8]),(u32 *)frame[10]);
        break;
    case SYS_DBGEXEC: {
        int pid=ustr(frame[8])?task_exec_scx(ustr(frame[8])):-1;
        frame[11]=(u32)process_debug_bind(pid,current); break;
    }
    case SC_SYS_DEBUG:
        /* 先检查调用者缓冲，再检查调试拥有关系，不能借目标 pid
         * 把 supervisor 恒等映射当成任意地址读写入口。 */
        if((frame[10]==DBG_INFO || frame[10]==DBG_READ)
           && !paging_user_range(TASK(current).pd,frame[5],frame[4])) frame[11]=(u32)-1;
        else frame[11]=(u32)process_debug(current,(int)frame[8],(int)frame[10],frame[9],(void *)frame[5],frame[4]);
        break;
    case SYS_AUTHINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],32))frame[11]=(u32)-1;
        else frame[11]=(u32)auth_info(current,(u32 *)frame[8]);break;
    case SYS_AUTHLOGIN: {
        const char *name=ustr(frame[8]),*password=ustr(frame[10]);
        frame[11]=name && password?(u32)auth_login(current,name,password):(u32)-1;break;
    }
    case SYS_AUTHSTATUS:
        if(!paging_user_range(TASK(current).pd,frame[10],32))frame[11]=(u32)-1;
        else frame[11]=(u32)auth_status(current,frame[8],(u32 *)frame[10]);break;
    case SYS_AUTHEXEC: {
        const char *command=frame[9]==1?"":ustr(frame[10]);
        frame[11]=command && frame[9]<=1?(u32)auth_exec_ticket(current,frame[8],command,(int)frame[9]):(u32)-1;break;
    }
    case SYS_AUTHUSERS:
        if(!frame[10] || frame[10]>4096 || !paging_user_range(TASK(current).pd,frame[8],frame[10]))frame[11]=(u32)-1;
        else frame[11]=(u32)auth_users(current,(char *)frame[8],frame[10]);break;
    case SYS_AUTHACCOUNT: {
        const char *name=ustr(frame[10]),*home=frame[8]==0?ustr(frame[9]):0;
        if(!name || frame[8]>1 || (!frame[8] && !home))frame[11]=(u32)-1;
        else frame[11]=frame[8]?(u32)auth_user_remove(current,name):(u32)auth_user_add(current,name,home,(int)frame[5],(int)frame[4]);break;
    }
    case SYS_AUTHCANCEL:
        frame[11]=(u32)auth_cancel(current,frame[8]);break;
    case SYS_AUTHPASSWORD: {
        const char *name=ustr(frame[8]),*password=ustr(frame[10]);
        frame[11]=name && password?(u32)auth_password(current,name,password,frame[9]):(u32)-1;break;
    }
    case SYS_FSMETA: {
        const char *path=ustr(frame[8]);
        if(!path || !paging_user_range(TASK(current).pd,frame[10],32))frame[11]=(u32)-1;
        else if(!fs_access(current,path,FS_ACCESS_META))frame[11]=(u32)-5;
        else frame[11]=(u32)fs_metadata(path,(u32 *)frame[10]);break;
    }
    case SYS_FSPERMISSIONS: {
        const char *path=ustr(frame[8]);
        frame[11]=path?(u32)fs_permissions(current,path,(int)frame[10],(int)frame[9],frame[5]):(u32)-1;break;
    }
    case SYS_STREAMOPEN: {
        const char *path=ustr(frame[8]);frame[11]=path?(u32)streams_open(current,path,frame[10],frame[9]):(u32)-1;break;
    }
    case SYS_CREATEEX: {
        const char *path=ustr(frame[8]);char resolved[64];
        frame[11]=path && userspace_resolve(current,path,resolved,sizeof(resolved))>=0
            ?(u32)fs_create_ex(current,resolved,frame[10],frame[9]):(u32)-1;break;
    }
    case SYS_STREAMREAD:
    case SYS_STREAMWRITE: {
        u32 call=frame[11];
        if(frame[9]>65536 || !paging_user_range(TASK(current).pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else frame[11]=call==SYS_STREAMREAD?(u32)streams_read(current,(int)frame[8],(void *)frame[10],frame[9])
            :(u32)streams_write(current,(int)frame[8],(const void *)frame[10],frame[9]);break;
    }
    case SYS_STREAMCLOSE:frame[11]=frame[10]<=1?(u32)streams_close(current,(int)frame[8],(int)frame[10]):(u32)-1;break;
    case SYS_STREAMEVENT:frame[11]=(u32)streams_event(current,(int)frame[8],frame[10]);break;
    case SYS_STORAGEINFO2:
        if(!paging_user_range(TASK(current).pd,frame[8],64))frame[11]=(u32)-1;
        else {fs_storage_info2((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_PROCESSSELF: {
        if(!paging_user_range(TASK(current).pd,frame[8],32)){frame[11]=(u32)-1;break;}
        u32 *out=(u32 *)frame[8];for(int i=0;i<8;i++)out[i]=0;out[0]=1;out[1]=(u32)current;out[2]=task_generation(current);
        out[3]=(u32)auth_uid(current);out[4]=(u32)auth_gid(current);u32 identity[8];auth_info(current,identity);out[5]=identity[3];frame[11]=0;break;
    }
    case SYS_KILLGEN2: {
        int target=(int)frame[8];
        /* 在同一IF=0陷入里检查代数与权限，名称查询后槽复用不能
         * 杀掉后来进程。旧KILL2的寄存器含义仍不增加隐藏选项。 */
        if(target<1 || target>=NTASK || task_generation(target)!=frame[10])frame[11]=(u32)-8;
        else frame[11]=(u32)streams_kill(current,target);break;
    }
    case SYS_DBGSPAWN2: {
        const char *command=ustr(frame[8]);u32 status[8];
        if(!command || !paging_user_range(TASK(current).pd,frame[10],12)){frame[11]=(u32)-1;break;}
        int job=streams_exec(current,command,(const i32 *)frame[10],0);
        if(job>0){
            /* 同一次IF=0陷入内绑定，目标还未执行首指令。绑定失败
             * 必须终止并领取作业，不能返回一个正在运行的假调试目标。 */
            if(streams_wait(current,(u32)job,status)>0 && status[3]){
                if(process_debug_bind((int)status[3],current)<0){task_stop((int)status[3]);streams_wait(current,(u32)job,status);job=-1;}
            }else job=-1;
        }
        frame[11]=(u32)job;break;
    }
    case SYS_STREAMDUP:frame[11]=(u32)streams_dup(current,(int)frame[8],(int)frame[10]);break;
    case SYS_STREAMPIPE:
        frame[11]=paging_user_range(TASK(current).pd,frame[8],8)?(u32)streams_pipe(current,(int *)frame[8]):(u32)-1;break;
    case SYS_STREAMSEEK:frame[11]=(u32)streams_seek(current,(int)frame[8],frame[10]);break;
    case SYS_STREAMMEMORY:
        frame[11]=frame[10]<=32768 && paging_user_range(TASK(current).pd,frame[8],frame[10])
            ?(u32)streams_memory(current,(const void *)frame[8],frame[10]):(u32)-1;break;
    case SYS_SPAWNBUFFER2: {
        const char *args=frame[5]?ustr(frame[5]):0;
        if(frame[10]>32768 || !paging_user_range(TASK(current).pd,frame[8],frame[10])
            || !paging_user_range(TASK(current).pd,frame[9],12) || (frame[5] && !args))frame[11]=(u32)-1;
        else frame[11]=(u32)streams_exec_buffer(current,(const void *)frame[8],frame[10],(const i32 *)frame[9],args);break;
    }
    case SYS_STREAMTTY:frame[11]=(u32)streams_tty(current,(int)frame[8]);break;
    case SYS_TERMINALINFO2:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],32)?(u32)streams_terminal_info(current,(int)frame[8],(u32 *)frame[10]):(u32)-1;break;
    case SYS_TERMINALCLEAR:frame[11]=(u32)streams_terminal_clear(current,(int)frame[8]);break;
    case SYS_SPAWN2: {
        const char *command=ustr(frame[8]);
        if(!command || !paging_user_range(TASK(current).pd,frame[10],12))frame[11]=(u32)-1;
        else frame[11]=(u32)streams_exec(current,command,(const i32 *)frame[10],frame[9]);break;
    }
    case SYS_WAIT2:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],32)?(u32)streams_wait(current,frame[8],(u32 *)frame[10]):(u32)-1;break;
    case SYS_JOBINFO2:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],32)?(u32)streams_job_snapshot(current,frame[8],(u32 *)frame[10]):(u32)-1;break;
    case SYS_KILL2:frame[11]=(u32)streams_kill(current,(int)frame[8]);break;
    case SYS_PROCESSINFO2:
        frame[11]=frame[10]>=8+LEGACY_TASK_ROWS2*16 && frame[10]<=1024 && paging_user_range(TASK(current).pd,frame[8],frame[10]*4)
            ?(u32)streams_processes(current,(u32 *)frame[8],frame[10]):(u32)-1;break;
    case SYS_ARGUMENTS2:
        frame[11]=frame[10]>0 && frame[10]<=1024 && paging_user_range(TASK(current).pd,frame[8],frame[10])
            ?(u32)streams_arguments(current,(char *)frame[8],frame[10]):(u32)-1;break;
    case SYS_SIMDINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],32))frame[11]=(u32)-1;
        else {simd_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_MODULEEXEC: {
        const char *path=ustr(frame[8]);frame[11]=path?(u32)modules_execute(current,path,(int)frame[10]):(u32)-1;break;
    }
    case SYS_MODULEINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],32))frame[11]=(u32)-1;
        else {modules_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_MODULELIST:
        frame[11]=frame[10]>0 && frame[10]<=4096 && paging_user_range(TASK(current).pd,frame[8],frame[10])?(u32)modules_list(current,(char *)frame[8],frame[10]):(u32)-1;break;
    case SYS_WINDOWPLACE:
        frame[11]=(u32)wm_user_place(current,(int)frame[8],(int)frame[10],(int)frame[9],(int)frame[5],(int)frame[4]);break;
    case SYS_CAPTUREOPEN:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],32)?(u32)wm_capture_open(current,(int)frame[8],(u32 *)frame[10]):(u32)-1;break;
    case SYS_CAPTUREREAD:
        frame[11]=frame[5]<=65536 && paging_user_range(TASK(current).pd,frame[9],frame[5])
            ?(u32)wm_capture_read(current,frame[8],frame[10],(void *)frame[9],frame[5]):(u32)-1;break;
    case SYS_CAPTURECLOSE:frame[11]=(u32)wm_capture_close(current,frame[8]);break;
    case SYS_CAPTURELIST:
        frame[11]=frame[10]>0 && frame[10]<=4096 && paging_user_range(TASK(current).pd,frame[8],frame[10])
            ?(u32)wm_capture_list(current,(char *)frame[8],frame[10]):(u32)-1;break;
    case SYS_WINDOWPAGE:
        frame[11]=frame[10]>=32 && frame[10]<=1040 && paging_user_range(TASK(current).pd,frame[8],frame[10]*4)
            ?(u32)wm_window_page(current,(u32 *)frame[8],frame[10],frame[9]):(u32)-1;break;
    case SYS_STREAMREADY:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],32)?(u32)streams_ready(current,(int)frame[8],(u32 *)frame[10]):(u32)-1;break;
#ifdef M10_DISK_CORE
    case SYS_NETINFO:
        frame[11]=paging_user_range(TASK(current).pd,frame[8],NET_INFO_WORDS*4)?(u32)network_info((u32 *)frame[8]):(u32)-14;break;
    case SYS_NETCONTROL:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],NET_CONTROL_WORDS*4)?(u32)network_control(current,frame[8],(const u32 *)frame[10]):(u32)-14;break;
    case SYS_SOCKET:frame[11]=(u32)network_socket_create(current,frame[8],frame[10]);break;
    case SYS_SOCKETBIND:case SYS_SOCKETCONNECT:
        if(!paging_user_range(TASK(current).pd,frame[10],16))frame[11]=(u32)-14;
        else frame[11]=(u32)(call==SYS_SOCKETBIND?network_socket_bind(current,frame[8],(const u32 *)frame[10]):network_socket_connect(current,frame[8],(const u32 *)frame[10]));break;
    case SYS_SOCKETLISTEN:frame[11]=(u32)network_socket_listen(current,frame[8],frame[10]);break;
    case SYS_SOCKETACCEPT:
        frame[11]=!frame[10] || paging_user_range(TASK(current).pd,frame[10],16)?(u32)network_socket_accept(current,frame[8],(u32 *)frame[10]):(u32)-14;break;
    case SYS_SOCKETSEND:case SYS_SOCKETRECEIVE:
        if(frame[9]>65536u || (frame[9] && !paging_user_range(TASK(current).pd,frame[10],frame[9])) || (frame[5] && !paging_user_range(TASK(current).pd,frame[5],16)))frame[11]=(u32)-14;
        else frame[11]=(u32)(call==SYS_SOCKETSEND?network_socket_send(current,frame[8],(const void *)frame[10],frame[9],(const u32 *)frame[5]):network_socket_receive(current,frame[8],(void *)frame[10],frame[9],(u32 *)frame[5]));break;
    case SYS_SOCKETSHUTDOWN:frame[11]=(u32)network_socket_shutdown(current,frame[8],frame[10]);break;
    case SYS_SOCKETCLOSE:frame[11]=(u32)network_socket_close(current,frame[8]);break;
    case SYS_SOCKETSTATUS:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],64)?(u32)network_socket_status(current,frame[8],(u32 *)frame[10]):(u32)-14;break;
    case SYS_SOCKETOPTION:frame[11]=(u32)network_socket_option(current,frame[8],frame[10],frame[9]);break;
    case SYS_SOCKETPAGE:
        frame[11]=frame[10]>=32 && frame[10]<=1040 && paging_user_range(TASK(current).pd,frame[8],frame[10]*4)?(u32)network_socket_page(current,frame[9],(u32 *)frame[8],frame[10]):(u32)-14;break;
    case SYS_ROUTEPAGE:case SYS_ARPPAGE:
        if(frame[10]<24 || frame[10]>528 || !paging_user_range(TASK(current).pd,frame[8],frame[10]*4))frame[11]=(u32)-14;
        else frame[11]=(u32)(call==SYS_ROUTEPAGE?network_route_page(frame[9],(u32 *)frame[8],frame[10]):network_arp_page(frame[9],(u32 *)frame[8],frame[10]));break;
    case SYS_DNSSTART:{const char *name=ustr(frame[8]);frame[11]=name?(u32)network_dns_start(current,name):(u32)-14;break;}
    case SYS_DNSSTATUS:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],32)?(u32)network_dns_status(current,frame[8],(u32 *)frame[10]):(u32)-14;break;
    case SYS_DNSCANCEL:frame[11]=(u32)network_dns_cancel(current,frame[8]);break;
    case SYS_NETEXEC:{
        const char *command=ustr(frame[8]);
        frame[11]=command && paging_user_range(TASK(current).pd,frame[10],12)?(u32)auth_network_exec(current,command,(const i32 *)frame[10]):(u32)-14;break;
    }
#endif
    case SYS_FONTINFO2:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],64)?(u32)ttf_info(frame[8],(u32 *)frame[10]):(u32)-1;break;
    case SYS_GLYPHBITMAP:
        frame[11]=frame[4]>=TTF_BITMAP_WORDS && frame[4]<=4096
            && paging_user_range(TASK(current).pd,frame[5],TTF_BITMAP_WORDS*4)
            ?(u32)ttf_bitmap(frame[8],frame[10],frame[9],(u32 *)frame[5]):(u32)-1;break;
    case SYS_ENVSET: {
        const char *name=ustr(frame[8]),*value=frame[9]?0:ustr(frame[10]);
        frame[11]=name && frame[9]<=1 && (frame[9] || value)?(u32)userspace_env_set(current,name,value,(int)frame[9]):(u32)-1;break;
    }
    case SYS_ENVLIST:
        frame[11]=frame[10]>0 && frame[10]<=8192 && paging_user_range(TASK(current).pd,frame[8],frame[10])
            ?(u32)userspace_env_list(current,(char *)frame[8],frame[10]):(u32)-1;break;
    case SYS_AUDIOINFO:
        if(!paging_user_range(TASK(current).pd,frame[8],64))frame[11]=(u32)-1;
        else {audio_info((u32 *)frame[8]);frame[11]=0;}break;
    case SYS_CLOCK2:
        frame[11]=paging_user_range(TASK(current).pd,frame[8],32)?(u32)rtc_snapshot((u32 *)frame[8]):(u32)-1;break;
    case SYS_AUDIOOPEN:frame[11]=(u32)audio_open(current,frame[8],frame[10]);break;
    case SYS_AUDIOSUBMIT: {
        u32 info[16];int result=audio_status(current,frame[8],info);
        if(result<0)frame[11]=(u32)result;
        else if(frame[9]>4096 || !paging_user_range(TASK(current).pd,frame[10],frame[9]*info[2]*2))frame[11]=(u32)-1;
        else frame[11]=(u32)audio_submit(current,frame[8],(const void *)frame[10],frame[9]);break;
    }
    case SYS_AUDIOCONTROL:frame[11]=(u32)audio_control(current,frame[8],frame[10],frame[9]);break;
    case SYS_AUDIOSTATUS:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],64)?(u32)audio_status(current,frame[8],(u32 *)frame[10]):(u32)-1;break;
    case SYS_AUDIOEFFECT:frame[11]=(u32)audio_effect((int)frame[8]);break;
    case SYS_POWER2:frame[11]=(u32)audio_power(current,(int)frame[8]);break;
    case SYS_STORAGEFLUSH:frame[11]=(u32)ata_flush();break;
    case SYS_CPUINFO2: {
        if(!paging_user_range(TASK(current).pd,frame[8],(8+LEGACY_TASK_ROWS2*6)*4)){frame[11]=(u32)-1;break;}
        u32 *out=(u32 *)frame[8];for(int i=0;i<8+LEGACY_TASK_ROWS2*6;i++)out[i]=0;
        out[0]=1;out[1]=100;out[2]=cpu_total;out[3]=cpu_idle;out[4]=cpu_kernel;out[5]=cpu_user;out[6]=cpu_graphics;out[7]=LEGACY_TASK_ROWS2;
        for(int i=0;i<LEGACY_TASK_ROWS2;i++){u32 *row=out+8+i*6;row[0]=(u32)i;row[1]=(u32)TASK(i).state;row[2]=task_record(i)->generation;
            if(i==current || auth_can_manage(current) || auth_uid(i)==auth_uid(current)){row[3]=task_record(i)->running;row[4]=task_record(i)->dispatch;row[5]=1;}}
        frame[11]=0;break;
    }
    case SYS_PROCESSPAGE:
        frame[11]=frame[10]>=32 && frame[10]<=1040 && paging_user_range(TASK(current).pd,frame[8],frame[10]*4)
            ?(u32)task_page(current,(u32 *)frame[8],frame[10],frame[9]):(u32)-1;break;
    case SYS_EVENTWAIT:
        frame[11]=(u32)task_event_wait(frame);break;
    case SYS_SESSIONPAGE:
        frame[11]=frame[10]>=32 && frame[10]<=1040 && paging_user_range(TASK(current).pd,frame[8],frame[10]*4)
            ?(u32)session_page(current,(u32 *)frame[8],frame[10],frame[9]):(u32)-1;break;
    case SYS_SESSIONCONTROL:
        frame[11]=(u32)session_control(current,frame[8],frame[10]);break;
    case SYS_VISIBILITY:
        frame[11]=paging_user_range(TASK(current).pd,frame[10],32)?(u32)wm_user_visibility(current,(int)frame[8],(u32 *)frame[10]):(u32)-1;break;
    case SYS_SESSIONPATH: {
        char *leaf=ustr(frame[8]),path[64];
        if(!leaf || !frame[9] || frame[9]>64 || !paging_user_range(TASK(current).pd,frame[10],frame[9]))frame[11]=(u32)-1;
        else {int n=session_config_path(leaf,path);if(n<0 || (u32)n>=frame[9])frame[11]=(u32)-1;
            else {for(int i=0;i<=n;i++)((char *)frame[10])[i]=path[i];frame[11]=(u32)n;}}
        break;
    }
    default:
        frame[11] = 0xFFFFFFFF;
        break;
    }
}

/* ---------------- SCX 加载器 (分页版) ---------------- */
static u8 *scx_stage;

int task_exec_scx(const char *path)
{
    /* EXEC 接受“路径 空格 参数串”。内核只切出首个路径，参数原样传给
     * 用户程序解释；这样汇编器/记事本可以选择输入输出文件，内核无需
     * 知道任何具体应用的语法。拷贝完再建任务，消除父子缓冲共享。 */
    char filename[64];
    int len = 0;
    while (*path == ' ') path++;
    while (path[len] && path[len] != ' ' && len < 63) { filename[len] = path[len]; len++; }
    if (path[len] && path[len] != ' ') return -1;
    filename[len] = 0;
    if(!auth_launch_access(filename,FS_ACCESS_READ))return -5;
    const char *arguments = path + len;
    while (*arguments == ' ') arguments++;
    int arglen = 0;
    while (arguments[arglen] && arglen < 127) arglen++;
    if (arguments[arglen]) return -1;
    /* M9首轮链接已证明大暂存挤出1..2MiB的BSS边界。SCX中转
     * 固定64页，只在首次合法装载时向PF申请，之后复用到重启；
     * 仍是IF=0加载路径的单一内核工作区，不随任务反复分配。
     * 旧262144B容量及容器/载荷边界保持，不抬高低端保留上限。 */
    if(!scx_stage){scx_stage=(u8 *)pframe_alloc_run(64);if(!scx_stage)return -4;}
    u32 icon_offset,icon_length;
    int checked=image_scx_info(filename,scx_stage,&icon_offset,&icon_length);
    if(checked)return checked;

    u32 entry_rva = *(u32 *)(scx_stage + 8);
    u32 load_size = *(u32 *)(scx_stage + 12);
    u32 bss_size  = *(u32 *)(scx_stage + 16);
    u32 stack_size = *(u32 *)(scx_stage + 20);
    /* 图标只是容器资源，不搬进程序地址域。先校验整个容器，再仅读
     * 载荷，既保留旧装载上限，也支持“最大载荷+独立图标”大文件。 */
    if(fs_read_at(filename,scx_stage+36,load_size,36)!=(int)load_size)return -1;

    u32 pd = paging_new_task_dir();
    if (!pd) return -4;
    u32 npages = (load_size + bss_size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (u32 i = 0; i < npages; i++) {
        u32 fr = pframe_alloc();
        if (fr == 0) { paging_free_task_dir(pd); return -4; }
        paging_map_user(pd, USER_BASE + i * PAGE_SIZE, fr);
        /* 经恒等映射把内容写进物理帧 */
        u8 *dst = (u8 *)fr;
        for (u32 k = 0; k < PAGE_SIZE; k++) {
            u32 file_off = i * PAGE_SIZE + k;
            dst[k] = (file_off < load_size) ? scx_stage[36 + file_off]
                   : (file_off < load_size + bss_size) ? 0 : 0;
        }
    }
    /* 栈在用户区顶端向下生长，不紧挨映像尾部。
     * SCX 建议长度按页向上取整；每页先分配成功再登记，失败时统一回滚
     * 已登记的用户页。映像上限 0x3D0000B、普通栈 128KB，并另留一页异常栈，三者不重叠。 */
    for (u32 i = 0; i < (stack_size+PAGE_SIZE-1)/PAGE_SIZE; i++) {
        u32 fr = pframe_alloc();
        if (!fr) { paging_free_task_dir(pd); return -4; }
        paging_map_user(pd, USER_TOP-(i+1)*PAGE_SIZE, fr);
        for (u32 k = 0; k < PAGE_SIZE; k++) ((u8 *)fr)[k] = 0;
    }

    char nm[12];
    /* 异常栈独立映射一页，不依赖程序损坏的普通栈；失败完整回滚。 */
    u32 exception_frame=pframe_alloc();
    if(!exception_frame) { paging_free_task_dir(pd); return -4; }
    paging_map_user(pd,USER_EXCEPTION_TOP-PAGE_SIZE,exception_frame);
    for(u32 k=0;k<PAGE_SIZE;k++) ((u8 *)exception_frame)[k]=0;
    const char *p = filename;
    for(int i=0;filename[i];i++) if(filename[i]=='/') p=filename+i+1;
    for (int k = 0; k < 11; k++) {
        nm[k] = p[k];
        if (!p[k]) break;
    }
    nm[11] = 0;
    int pid = task_spawn(nm, pd, USER_BASE + entry_rva, USER_TOP - 16);
    if (pid < 0) paging_free_task_dir(pd);
    else {
        for (int i = 0; i <= arglen; i++) TASK(pid).args[i] = arguments[i];
        streams_set_arguments(pid,arguments);
        userspace_executable(pid,filename);
    }
    return pid;
}
