/* 保留上游完整翻译单元与BSD声明，私有更名让其静态定时状态仍可见；
 * 未改原始快照。新的服务入口按回调数让出，不能用冻结sys_now或
 * 跳过到期定时器来假造预算。TCP/ARP/DHCP的单个周期仍由上游处理。 */
#include "port.h"
#include "lwip/timeouts.h"
#include "lwip/memp.h"
static struct sys_timeo *timer_reserve;
static struct sys_timeo *take_timer(memp_t type)
{
    if(type!=MEMP_SYS_TIMEOUT)return memp_malloc(type);
    struct sys_timeo *node=timer_reserve;
    if(node)timer_reserve=node->next;else node=sc_net_alloc(sizeof(*node));
    return node;
}
static void put_timer(memp_t type,void *pointer)
{
    if(type!=MEMP_SYS_TIMEOUT){memp_free(type,pointer);return;}
    struct sys_timeo *node=pointer;node->next=timer_reserve;timer_reserve=node;
}
int sc_net_timers_prepare(void)
{
    /* 每个协议一个周期节点，数量来自本构建的真实cyclic表。弹出节点
     * 在运行回调前回到专用保留链，ARP/PCB的OOM不能抢走续约定时器
     * 的最后一块。额外合法定时器仍动态增长，没有固定PCB/socket数。
     * 保留节点属于整个启动周期，作为明确协议常驻成本计入PF/bytes。 */
    for(int i=0;i<lwip_num_cyclic_timers+2;i++){
        struct sys_timeo *node=sc_net_alloc(sizeof(*node));
        if(!node){while(timer_reserve){struct sys_timeo *next=timer_reserve->next;sc_net_free(timer_reserve);timer_reserve=next;}return -1;}
        node->next=timer_reserve;timer_reserve=node;
    }
    return 0;
}
#define memp_malloc(type) take_timer(type)
#define memp_free(type,pointer) put_timer(type,pointer)
#define sys_check_timeouts sc_lwip_unbounded_timeouts
#include "../../third_party/lwip/src/core/timeouts.c"
#undef sys_check_timeouts
#include "../task.h"

void sys_check_timeouts(void)
{
    LWIP_ASSERT_CORE_LOCKED();u32_t now=sys_now();u32_t budget=8;
    while(budget--){
        PBUF_CHECK_FREE_OOSEQ();struct sys_timeo *expired=next_timeout;
        if(!expired || TIME_LESS_THAN(now,expired->time))return;
        next_timeout=expired->next;sys_timeout_handler handler=expired->h;void *arg=expired->arg;
        current_timeout_due_time=expired->time;memp_free(MEMP_SYS_TIMEOUT,expired);
        if(handler)handler(arg);
    }
    if(next_timeout && !TIME_LESS_THAN(now,next_timeout->time))task_kernel_wake();
}
