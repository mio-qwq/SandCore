#ifndef SANDCORE_NETWORK_H
#define SANDCORE_NETWORK_H
#include "io.h"
#define NET_INFO_WORDS 96u
#define NET_CONTROL_WORDS 32u
#define NET_ADDRESS_WORDS 4u
enum {
    NET_SET_ADDRESS=1,NET_UP,NET_DOWN,NET_DHCP_START,NET_DHCP_RENEW,
    NET_DHCP_RELEASE,NET_RESET,NET_SET_DNS,NET_SET_HOSTNAME,NET_SET_DOMAIN,
    NET_ARP_ADD,NET_ARP_DELETE,NET_ARP_FLUSH,NET_ROUTE_ADD,NET_ROUTE_DELETE
};
enum {NET_SOCKET_TCP=1,NET_SOCKET_UDP,NET_SOCKET_ICMP,NET_SOCKET_ETHERNET};
enum {NET_READY_READ=1,NET_READY_WRITE=2,NET_READY_ERROR=4,NET_READY_HUP=8,NET_READY_ACCEPT=16};
/* 新地址结构四个u32：版本1、IPv4主机序数(A<<24|B<<16|C<<8|D)、
 * 主机序端口、保留0。所有调用非阻塞：-11等待就绪，-115连接进行中。
 * DNS等步进任务也使用TASK_EVENT_NETWORK，不能由GUI绘制来驱动。 */
void network_init(void);
void network_poll(void); /* task0、IF=0进入，内置IRQ可继续，三环不重入。 */
int network_info(u32 out[NET_INFO_WORDS]);
int network_control(int pid,u32 operation,const u32 in[NET_CONTROL_WORDS]);
int network_route_page(u32 cursor,u32 *out,u32 words); /* 16字头+8字行。 */
int network_arp_page(u32 cursor,u32 *out,u32 words); /* 16字头+8字行。 */
int network_socket_create(int pid,u32 type,u32 protocol);
int network_socket_bind(int pid,u32 handle,const u32 address[4]);
int network_socket_connect(int pid,u32 handle,const u32 address[4]);
int network_socket_listen(int pid,u32 handle,u32 backlog);
int network_socket_accept(int pid,u32 handle,u32 address[4]);
int network_socket_send(int pid,u32 handle,const void *data,u32 bytes,const u32 *address);
int network_socket_receive(int pid,u32 handle,void *data,u32 bytes,u32 *address);
int network_socket_shutdown(int pid,u32 handle,u32 how);
int network_socket_close(int pid,u32 handle);
int network_socket_status(int pid,u32 handle,u32 out[16]);
int network_socket_option(int pid,u32 handle,u32 option,u32 value);
int network_socket_page(int pid,u32 cursor,u32 *out,u32 words);
int network_dns_start(int pid,const char *name);
int network_dns_status(int pid,u32 ticket,u32 out[8]);
int network_dns_cancel(int pid,u32 ticket);
int network_stop_owner_step(int pid); /* 一次只撤一个对象，代数核对。 */
#endif
