#ifndef SANDCORE_NETWORK_INTERNAL_H
#define SANDCORE_NETWORK_INTERNAL_H
#include "port.h"
#include "../network.h"
#include "lwip/netif.h"
#include "lwip/err.h"
/* 内核协议桥和socket对象分文件，公共头不泄漏lwIP指针或PCB布局。
 * 对象回调与raw API必须在sc_net_context内；IRQ完全不使用本头。 */
struct netif *sc_net_interface(void);
int sc_net_available(void);
int sc_net_protocol_failed(void);
int sc_net_error(err_t error);
void sc_net_socket_init(void);
void sc_net_socket_poll(void);
void sc_net_socket_changed(u32 epoch,int terminal_error);
void sc_net_socket_frame(const unsigned char *frame,unsigned int bytes);
unsigned int sc_net_socket_count(void);
unsigned int sc_net_socket_rx_bytes(void);
unsigned int sc_net_config_epoch(void);
int sc_net_socket_pending(void);
void sc_net_loop_poll(struct netif *netif);
void sc_net_loop_discard(struct netif *netif);
#endif
