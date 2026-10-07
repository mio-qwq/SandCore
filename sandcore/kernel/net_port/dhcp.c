/* 上游BSD原件原样保留；构建派生仅修默认T2的32位乘法溢出。
 * 私有更名粗定时入口，在相同网络上下文
 * 清掉有限租约转无限租约时上游bind跳过赋值留下的旧计数。不能
 * 让前一次60秒租约的t0/T1/T2在新无限租约下继续生效。 */
#include "port.h"
#include "lwip/dhcp.h"
#define dhcp_coarse_tmr sc_lwip_dhcp_coarse_tmr
#include "dhcp_upstream.inc"
#undef dhcp_coarse_tmr

void dhcp_coarse_tmr(void)
{
    LWIP_ASSERT_CORE_LOCKED();struct netif *netif;
    NETIF_FOREACH(netif){
        struct dhcp *lease=netif_dhcp_data(netif);
        /* SINGLE_NETIF下上游宏展开成if，不能借continue假定它是for。 */
        if(lease && lease->state!=DHCP_STATE_OFF){
            if(lease->offered_t0_lease==0xFFFFFFFFu)lease->t0_timeout=0;
            if(lease->offered_t1_renew==0xFFFFFFFFu)lease->t1_timeout=lease->t1_renew_time=0;
            if(lease->offered_t2_rebind==0xFFFFFFFFu)lease->t2_timeout=lease->t2_rebind_time=0;
        }
    }
    sc_lwip_dhcp_coarse_tmr();
}
