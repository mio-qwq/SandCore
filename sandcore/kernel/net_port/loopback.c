#include "network_internal.h"
#include "lwip/pbuf.h"
#include "lwip/ip.h"
#include "lwip/stats.h"
#include "../task.h"
/* 自写适配只操作公开loopback队列合同；每轮32个IP报文。ip_input
 * 可以立刻生成新的本机响应，故不能一直处理到队列为空，否则本机
 * echo服务可以把task0占住。每个报文的链由tot_len==len界定，不能
 * 将队列上的下一报文一起交给IP，也不能重复计算loop_cnt_current。 */
static void loop_service(struct netif *netif,int deliver)
{
    LWIP_ASSERT_CORE_LOCKED();u32 budget=32;
    while(netif->loop_first && budget--){
        struct pbuf *packet=netif->loop_first,*end=packet;u32 parts=1;
        while(end->len!=end->tot_len){LWIP_ASSERT("loopback chain",end->next!=0);end=end->next;parts++;}
        LWIP_ASSERT("loopback count",parts<=netif->loop_cnt_current);
        netif->loop_cnt_current=(u16_t)(netif->loop_cnt_current-parts);
        netif->loop_first=end->next;if(!netif->loop_first)netif->loop_last=0;end->next=0;
        if(deliver){packet->if_idx=netif_get_index(netif);LINK_STATS_INC(link.recv);
            if(ip_input(packet,netif)!=ERR_OK)pbuf_free(packet);
        }else pbuf_free(packet);
    }
    if(netif->loop_first)task_kernel_wake();
}
void sc_net_loop_poll(struct netif *netif){loop_service(netif,1);}
void sc_net_loop_discard(struct netif *netif){loop_service(netif,0);}
