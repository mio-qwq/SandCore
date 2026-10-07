#ifndef SANDCORE_LWIP_HOOKS_H
#define SANDCORE_LWIP_HOOKS_H
#include "port.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
const ip4_addr_t *sc_net_gateway(struct netif *interface,const ip4_addr_t *destination);
#define LWIP_HOOK_TCP_ISN(local,lp,remote,rp) sc_net_isn(ip4_addr_get_u32(ip_2_ip4(local)),ip4_addr_get_u32(ip_2_ip4(remote)),lp,rp)
#define LWIP_HOOK_ETHARP_GET_GW(interface,destination) sc_net_gateway(interface,destination)
#endif
