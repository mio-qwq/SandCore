/* =====================================================================
 * mio：M7 三环异常与真实 x86 调试。
 * 暂停保留寄存器/内存，调度器跳过 state=3；用户关闭卡片后才延后释放。
 * 注册回调优先获得一次恢复机会，回调自身出错禁止递归覆盖第一份现场。
 * 断点真正写入 INT3；命中后恢复原字节并退 EIP，再以 TF 执行原指令，
 * 在 #DB 重插断点。这样继续/单步都不会漏掉被断点替换的那条指令。
 * 所有入口在 IF=0 的临界区，现场/页表不会一边复制一边被调度改写。
 * ===================================================================== */
#include "process.h"
#include "task.h"
#include "paging.h"
#include "wm.h"

#define BREAKPOINTS 8
typedef struct { u32 address; u8 original; int used; } breakpoint_t;
typedef struct {
    int exit_code,debug_owner;
    u32 handlers[32],saved_fault[19],context;
    int in_handler;
    u32 cr2,event;
    breakpoint_t breaks[BREAKPOINTS];
    int pending,auto_continue;
} process_t;
static process_t processes[NTASK];

void process_reset(int pid)
{
    /* 槽位复用清理上一程序所有元数据。退出结果保留到下次 spawn 才清零，
     * 因此 IDE 等待编译子进程时可以在僵尸已经回收后查询退出码。 */
    u8 *p=(u8 *)&processes[pid];
    for(u32 i=0;i<sizeof(process_t);i++) p[i]=0;
    processes[pid].debug_owner=-1; processes[pid].pending=-1;
}
void process_exit(int pid,int code) { processes[pid].exit_code=code; }
int process_status(int pid)
{
    if(pid<=0 || pid>=NTASK) return -1;
    if(tasks[pid].state==0 || tasks[pid].state==2) return processes[pid].exit_code;
    return tasks[pid].state==3 ? 0x40000001 : 0x40000000;
}
int process_handler(int vector,u32 entry)
{
    int pid=task_pid();
    if(vector<0 || vector>=32 || vector==2 || vector==8) return -1;
    if(entry && !paging_user_range(tasks[pid].pd,entry,1)) return -1;
    processes[pid].handlers[vector]=entry; return 0;
}
static void pause_fault(int pid,u32 *frame,u32 cr2)
{
    process_t *p=&processes[pid];
    tasks[pid].saved_esp=(u32)frame; tasks[pid].state=3;
    p->cr2=cr2; p->event++;
    if(p->debug_owner<1 || tasks[p->debug_owner].state!=1) {
        /* 调试拥有者退出后回到普通卡片，不能留下无人能关闭的暂停任务。 */
        p->debug_owner=-1;
        wm_exception(pid,frame[12],frame[13],frame[14],cr2);
    }
}
void process_exception(u32 vector,u32 *frame)
{
    int pid=task_pid(); process_t *p=&processes[pid]; u32 cr2=0;
    if(vector==14) __asm__ __volatile__("mov %%cr2,%0":"=r"(cr2));
    frame[16]&=~0x100u;             /* 单步只执行一条，后续由 STEP 显式打开 TF */
    if(vector==1 && p->debug_owner>0) {
        if(p->pending>=0) {
            breakpoint_t *b=&p->breaks[p->pending]; u8 opcode=0xCC;
            if(b->used) paging_user_copy(tasks[pid].pd,b->address,&opcode,1,1);
            p->pending=-1;
            if(p->auto_continue) { p->auto_continue=0; return; }
        }
        pause_fault(pid,frame,0); return;
    }
    if(vector==3 && p->debug_owner>0) {
        for(int i=0;i<BREAKPOINTS;i++) {
            breakpoint_t *b=&p->breaks[i];
            if(b->used && b->address==frame[14]-1) {
                paging_user_copy(tasks[pid].pd,b->address,&b->original,1,1);
                frame[14]--; p->pending=i; break;
            }
        }
        pause_fault(pid,frame,0); return;
    }
    u32 handler=p->handlers[vector];
    if(handler && !p->in_handler && paging_user_range(tasks[pid].pd,handler,1)) {
        /* 独立异常栈不依赖可能已经损坏的普通栈。cdecl 回调参数指向 76B
         * 现场副本，可调整 EIP/寄存器后 EXCRETURN。普通 return 会跳到
         * 故意无映射的 0 地址，并因 in_handler=1 回到暂停而非递归调用。 */
        u32 ctx=USER_EXCEPTION_TOP-80,sp=ctx-8;
        if(paging_user_range(tasks[pid].pd,sp,88)) {
            for(int i=0;i<19;i++) { p->saved_fault[i]=frame[i]; ((u32 *)ctx)[i]=frame[i]; }
            ((u32 *)sp)[0]=0; ((u32 *)sp)[1]=ctx;
            p->context=ctx; p->in_handler=1; p->cr2=cr2;
            frame[14]=handler; frame[17]=sp; return;
        }
    }
    process_exit(pid,128+(int)vector); pause_fault(pid,frame,cr2);
}
int process_exception_return(u32 *frame,u32 context)
{
    int pid=task_pid(); process_t *p=&processes[pid];
    if(!p->in_handler || context!=p->context || !paging_user_range(tasks[pid].pd,context,76)) return -1;
    u32 *f=(u32 *)context;
    if(!paging_user_range(tasks[pid].pd,f[14],1) || !paging_user_range(tasks[pid].pd,f[17]-4,4)) return -1;
    /* 只开放通用寄存器/EIP/ESP/普通算术标志。选择子从可信原帧恢复，
     * 不能借返回现场改变 CPL、IOPL、NT、VM，不能关闭 IF 或偷开 TF。 */
    for(int i=0;i<19;i++) frame[i]=p->saved_fault[i];
    for(int i=4;i<12;i++) frame[i]=f[i];
    frame[14]=f[14]; frame[17]=f[17]; frame[16]=(f[16]&0xCD5u)|0x202u;
    p->in_handler=0; return 0;
}
int process_debug_bind(int pid,int owner)
{
    if(pid<=0 || pid>=NTASK || tasks[pid].state!=1) return -1;
    processes[pid].debug_owner=owner; tasks[pid].state=3; processes[pid].event++;
    return pid;
}
int process_debug(int owner,int pid,int command,u32 address,void *buffer,u32 length)
{
    if(pid<=0 || pid>=NTASK || owner!=processes[pid].debug_owner || tasks[pid].state==0 || tasks[pid].state==2) return -1;
    process_t *p=&processes[pid]; u32 *f=(u32 *)tasks[pid].saved_esp;
    if(command==DBG_KILL) { task_stop(pid); return 0; }
    if(command==DBG_INFO) {
        if(tasks[pid].state!=3 || length<88) return -2;
        u32 *out=buffer;
        for(int i=0;i<19;i++) out[i]=f[i];
        out[19]=(u32)tasks[pid].state; out[20]=p->cr2; out[21]=p->event; return 0;
    }
    if(command==DBG_READ) {
        if(length>256) return -1;
        return paging_user_copy(tasks[pid].pd,address,buffer,length,0);
    }
    if(command==DBG_PAUSE) {
        /* owner 调用时目标不可能是当前 CPU 任务；其 saved_esp 是最近
         * PIT/陷入留下的真实现场。暂停只改可运行状态，不伪造指令。
         * 下一次 STEP/CONT 再决定 TF；待恢复断点的原字节仍由 pending 记录。 */
        if(tasks[pid].state!=1 && tasks[pid].state!=3) return -2;
        tasks[pid].state=3; f[16]&=~0x100u; p->event++; return 0;
    }
    if(tasks[pid].state!=3) return -2;
    if(command==DBG_STEP || command==DBG_CONTINUE) {
        if(command==DBG_STEP || p->pending>=0) f[16]|=0x100u;
        p->auto_continue=(command==DBG_CONTINUE); tasks[pid].state=1; return 0;
    }
    if(command==DBG_BREAK) {
        for(int i=0;i<BREAKPOINTS;i++) if(p->breaks[i].used && p->breaks[i].address==address) return 0;
        for(int i=0;i<BREAKPOINTS;i++) if(!p->breaks[i].used) {
            breakpoint_t *b=&p->breaks[i]; u8 op=0xCC;
            if(paging_user_copy(tasks[pid].pd,address,&b->original,1,0)) return -1;
            if(b->original==0xCC) return -1; /* 不能接管目标自身的 INT3 语义 */
            if(paging_user_copy(tasks[pid].pd,address,&op,1,1)) return -1;
            b->address=address; b->used=1; return 0;
        }
        return -3;
    }
    if(command==DBG_UNBREAK) for(int i=0;i<BREAKPOINTS;i++) if(p->breaks[i].used && p->breaks[i].address==address) {
        breakpoint_t *b=&p->breaks[i];
        if(p->pending!=i) paging_user_copy(tasks[pid].pd,address,&b->original,1,1); else p->pending=-1;
        b->used=0; return 0;
    }
    return -1;
}
void process_detach_owner(int owner)
{
    /* UI 关闭时终止它独占的调试子进程，防止遗留暂停地址空间/断点。
     * 普通 EXEC 子进程不受影响；它们的生命周期仍属于普通应用。 */
    for(int i=1;i<NTASK;i++) if(processes[i].debug_owner==owner) {
        processes[i].debug_owner=-1; task_stop(i);
    }
}
