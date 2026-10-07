/* 外部调试用的offsetof常量，编译成只读数据而非系统代码；
 * 必须用当前主核的32位配置，不能手写偏移猜测timer大小。 */
#include <stddef.h>
#include "lwip/netif.h"
#include "lwip/dhcp.h"
const unsigned int sc_dhcp_layout[]={
    sizeof(dhcp_timeout_t),sizeof(void *),
    offsetof(struct netif,client_data)+LWIP_NETIF_CLIENT_DATA_INDEX_DHCP*sizeof(void *),
    offsetof(struct dhcp,state),offsetof(struct dhcp,request_timeout),
    offsetof(struct dhcp,t1_timeout),offsetof(struct dhcp,t2_timeout),
    offsetof(struct dhcp,t1_renew_time),offsetof(struct dhcp,t2_rebind_time),
    offsetof(struct dhcp,lease_used),offsetof(struct dhcp,t0_timeout),
    offsetof(struct dhcp,offered_t0_lease),offsetof(struct dhcp,offered_t1_renew),
    offsetof(struct dhcp,offered_t2_rebind)
};
