#ifndef SANDCORE_LWIP_OPTS_H
#define SANDCORE_LWIP_OPTS_H
#include "port.h"
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_IPV4 1
#define LWIP_IPV6 0
/* 上游初始化无条件读取PPP选项/声明头；原头完整保留，协议不启用。 */
#define PPP_SUPPORT 0
#define PPPOE_SUPPORT 0
#define PPPOL2TP_SUPPORT 0
#define PPPOS_SUPPORT 0
#define LWIP_ETHERNET 1
#define LWIP_ARP 1
#define LWIP_ICMP 1
#define LWIP_RAW 1
#define LWIP_UDP 1
#define LWIP_IP_SOF_BROADCAST 1
#define LWIP_IP_SOF_BROADCAST_RECV 1
#define LWIP_TCP 1
#define LWIP_DHCP 1
/* 60秒粗粒度会把60秒租约的20/40秒T1/T2都折成一个tick，
 * 首个回调就先走到期分支，根本没有自动续期。仅一个IPv4接口，
 * 每秒一次租约回调成本有界；秒计数必须扩为32位，不能因此把
 * 原来的长租约暗截成65535秒。保持上游快照不变，走公开配置钩子。 */
#define DHCP_COARSE_TIMER_SECS 1
#define DHCP_TIMEOUT_SIZE_T u32_t
#define DHCP_DEFINE_CUSTOM_TIMEOUTS 1
#define SC_DHCP_SECONDS(result,offered) do { \
    u32_t sc_dhcp_seconds=(offered); \
    (result)=sc_dhcp_seconds?sc_dhcp_seconds:1u; \
} while(0)
#define DHCP_SET_TIMEOUT_FROM_OFFERED_T0_LEASE(result,dhcp) SC_DHCP_SECONDS(result,(dhcp)->offered_t0_lease)
#define DHCP_SET_TIMEOUT_FROM_OFFERED_T1_RENEW(result,dhcp) SC_DHCP_SECONDS(result,(dhcp)->offered_t1_renew)
#define DHCP_SET_TIMEOUT_FROM_OFFERED_T2_REBIND(result,dhcp) SC_DHCP_SECONDS(result,(dhcp)->offered_t2_rebind)
#define DHCP_NEXT_TIMEOUT_THRESHOLD 60u
#define DHCP_REQUEST_BACKOFF_SEQUENCE(tries) (u16_t)(((tries)<6?1u<<(tries):60u)*1000u)
#define LWIP_DHCP_DOES_ACD_CHECK 1
#define LWIP_DHCP_CHECK_LINK_UP 1
#define LWIP_DNS 1
#define LWIP_DHCP_MAX_DNS_SERVERS 2
#define LWIP_DHCP_GET_NTP_SRV 0
#define LWIP_DNS_SECURE 7
#define LWIP_NETIF_HOSTNAME 1
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_EXT_STATUS_CALLBACK 0
#define LWIP_NETIF_API 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_AUTOIP 0
#define LWIP_IGMP 0
#define IP_FORWARD 0
#define IP_REASSEMBLY 1
#define IP_FRAG 1
#define IP_REASS_MAX_PBUFS 64
#define ARP_QUEUEING 1
#define ARP_QUEUE_LEN 8
#define ETHARP_SUPPORT_STATIC_ENTRIES 1
#define LWIP_NETIF_TX_SINGLE_PBUF 0
#define LWIP_NETIF_LOOPBACK 1
#define LWIP_HAVE_LOOPIF 0
#define LWIP_LOOPBACK_MAX_PBUFS 64
#define LWIP_SINGLE_NETIF 1
#define MEM_CUSTOM_ALLOCATOR 1
#define MEM_CUSTOM_MALLOC sc_net_alloc
#define MEM_CUSTOM_CALLOC sc_net_calloc
#define MEM_CUSTOM_FREE sc_net_free
#define MEM_ALIGNMENT 4
#define MEMP_MEM_MALLOC 1
#define MEM_OVERFLOW_CHECK 0
#define MEMP_OVERFLOW_CHECK 0
#define LWIP_STATS 1
#define LWIP_STATS_DISPLAY 0
#define MEM_STATS 0
#define MEMP_STATS 0
#define SYS_STATS 0
#define LWIP_CHECKSUM_ON_COPY 0
#define LWIP_CHECKSUM_CTRL_PER_NETIF 0
#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_GEN_ICMP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_TCP 1
#define CHECKSUM_CHECK_UDP 1
#define CHECKSUM_CHECK_ICMP 1
#define TCP_MSS 1460
#define TCP_WND 32768
#define TCP_SND_BUF 32768
#define TCP_SND_QUEUELEN 128
#define TCP_LISTEN_BACKLOG 1
#define TCP_DEFAULT_LISTEN_BACKLOG 16
#define SO_REUSE 1
#define SO_REUSE_RXTOALL 0
#define TCP_QUEUE_OOSEQ 1
#define TCP_OOSEQ_MAX_BYTES 32768
#define TCP_OOSEQ_MAX_PBUFS 64
#define LWIP_TCP_SACK_OUT 1
#define LWIP_TCP_TIMESTAMPS 1
#define LWIP_TCP_KEEPALIVE 1
#define LWIP_TCP_PCB_NUM_EXT_ARGS 1
#define ARP_TABLE_SIZE 128
#define DNS_TABLE_SIZE 32
#define DNS_MAX_REQUESTS 32
#define LWIP_WND_SCALE 0
#define TCP_OVERSIZE 0
#define TCP_CALCULATE_EFF_SEND_MSS 1
#define LWIP_HOOK_FILENAME "hooks.h"
/* 每连接的窗口/发送队列、ARP缓存、DNS缓存与重组预算是明确的
 * 协议资源背压，不能拿它们限制任务或socket对象的总创建数量。
 * MEMP_MEM_MALLOC将PCB/定时器/分片节点改为按真实内存增长。 */
#endif
