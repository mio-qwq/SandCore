#include "network.h"
#include "e1000.h"
#include "task.h"
#include "auth.h"
#include "objpool.h"
#include "timer.h"
#include "net_port/network_internal.h"
#include "lwip/tcp.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/udp.h"
#include "lwip/raw.h"
#include "lwip/dns.h"
#include "lwip/pbuf.h"
#include "lwip/inet_chksum.h"
#include "lwip/prot/etharp.h"

enum {SOCKET_NEW=1,SOCKET_BOUND,SOCKET_CONNECTING,SOCKET_CONNECTED,SOCKET_LISTEN,SOCKET_ERROR,SOCKET_CLOSING,SOCKET_DNS};
#define SOCKET_RX_BYTES 131072u
#define SOCKET_RX_MESSAGES 64u
#define SOCKET_DNS_TYPE 5u
typedef struct packet_node {
    struct packet_node *next;struct pbuf *packet;u32 offset,address,port,bytes;
} packet_node_t;
typedef struct socket_object {
    u32 id,generation,type,state,owner_generation;int owner,error;
    struct tcp_pcb *tcp;struct udp_pcb *udp;struct raw_pcb *raw;
    packet_node_t *rx_first,*rx_last;u32 rx_bytes,rx_messages,rx_eof,rx_shutdown,tx_shutdown;
    u32 owner_prev,owner_next,parent,accept_prev,accept_next,accept_first,accept_last,accept_count,backlog;
    u32 close_next,close_queued,close_started,close_retry,drain_next,drain_queued,tap_prev,tap_next;
    u32 local_address,local_port,peer_address,peer_port,ttl,icmp_id;
    u32 last_message,truncated,received,sent,dropped,dns_address,dns_state;
    char dns_name[256];
} socket_object_t;
typedef struct {u32 generation,first,last,cleanup_cursor;} socket_owner_t;
typedef struct tap_frame {
    struct tap_frame *next;struct pbuf *packet;u32 cursor,end,address;
} tap_frame_t;
typedef struct {u32 handle;} tap_entry_t;
static objpool_t sockets,owners,taps;
static tap_frame_t *tap_frames_first,*tap_frames_last;
static u32 tap_frames_count;
static u32 next_handle=1,close_first,close_last,tap_first;
static u32 drain_first,drain_last,close_urgent,close_batch_tick;
static u8_t tcp_owner_arg;
static u32 notification_epoch,notification_cursor,notification_active;
static u32 notification_again;
static int notification_error;
static u32 queued_bytes,live_sockets;
static u32 icmp_used[2048],icmp_cursor=1;

static socket_object_t *object(u32 handle){return handle && handle<=0x7FFFFFFEu?objpool_get(&sockets,(int)handle):0;}
static socket_owner_t *owner_for(int pid,int create)
{
    if(pid<=0 || !task_owned(pid))return 0;socket_owner_t *owner=objpool_get(&owners,pid);
    u32 generation=task_generation(pid);
    if(owner)return owner->generation==generation?owner:0;
    if(!create || objpool_claim(&owners,pid)<0)return 0;
    owner=objpool_get(&owners,pid);owner->generation=generation;return owner;
}
static socket_object_t *owned(int pid,u32 handle)
{
    socket_object_t *socket=object(handle);
    return socket && socket->state!=SOCKET_CLOSING && socket->owner==pid && socket->owner_generation==task_generation(pid)?socket:0;
}
static void notify(socket_object_t *socket)
{
    u32 flags;__asm__ __volatile__("pushfl; popl %0; cli":"=r"(flags)::"memory");
    task_notify_generation(socket->owner,socket->owner_generation,TASK_EVENT_NETWORK);
    __asm__ __volatile__("pushl %0; popfl"::"r"(flags):"memory","cc");
}
static ip_addr_t make_address(u32 value)
{ip_addr_t address;ip_addr_set_ip4_u32(&address,lwip_htonl(value));return address;}
static u32 address_value(const ip_addr_t *address){return lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(address)));}
static int address_ok(const u32 *address)
{return address && address[0]==1 && address[2]<=65535u && !address[3];}
static int local_ok(u32 value)
{
    struct netif *netif=sc_net_interface();return !value || (value>>24)==127 || (netif && value==lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(netif))));
}
static socket_object_t *allocate_socket(int pid,u32 type)
{
    socket_owner_t *owner=owner_for(pid,1);if(!owner)return 0;
    if(next_handle>0x7FFFFFFEu)return 0;u32 handle=next_handle++;
    if(objpool_claim(&sockets,(int)handle)<0){if(!owner->first)objpool_release(&owners,pid);return 0;}
    socket_object_t *socket=object(handle);socket->id=handle;socket->generation=handle;
    socket->owner=pid;socket->owner_generation=owner->generation;socket->type=type;socket->state=SOCKET_NEW;
    socket->ttl=64;socket->icmp_id=0xFFFFFFFFu;socket->owner_prev=owner->last;
    if(owner->last)object(owner->last)->owner_next=handle;else owner->first=handle;
    owner->last=handle;live_sockets++;return socket;
}
static void accept_unlink(socket_object_t *socket)
{
    socket_object_t *parent=object(socket->parent);if(!parent){socket->parent=0;return;}
    if(socket->accept_prev)object(socket->accept_prev)->accept_next=socket->accept_next;else parent->accept_first=socket->accept_next;
    if(socket->accept_next)object(socket->accept_next)->accept_prev=socket->accept_prev;else parent->accept_last=socket->accept_prev;
    if(parent->accept_count)parent->accept_count--;socket->parent=socket->accept_prev=socket->accept_next=0;
    if(socket->tcp)tcp_backlog_accepted(socket->tcp);
}
static void tap_unlink(socket_object_t *socket)
{
    if(socket->type!=NET_SOCKET_ETHERNET)return;
    objpool_release(&taps,(int)socket->id);
    if(socket->tap_prev)object(socket->tap_prev)->tap_next=socket->tap_next;else if(tap_first==socket->id)tap_first=socket->tap_next;
    if(socket->tap_next)object(socket->tap_next)->tap_prev=socket->tap_prev;socket->tap_prev=socket->tap_next=0;
}
static void release_socket(socket_object_t *socket)
{
    socket_owner_t *owner=objpool_get(&owners,socket->owner);
    if(owner && owner->generation==socket->owner_generation){
        if(owner->cleanup_cursor==socket->id)owner->cleanup_cursor=socket->owner_next;
        if(socket->owner_prev)object(socket->owner_prev)->owner_next=socket->owner_next;else owner->first=socket->owner_next;
        if(socket->owner_next)object(socket->owner_next)->owner_prev=socket->owner_prev;else owner->last=socket->owner_prev;
        if(!owner->first)objpool_release(&owners,socket->owner);
    }
    if(socket->icmp_id<65536u)icmp_used[socket->icmp_id/32]&=~(1u<<(socket->icmp_id%32));
    live_sockets--;objpool_release(&sockets,(int)socket->id);
}
static void enqueue_close(socket_object_t *socket)
{
    if(socket->close_queued)return;socket->close_queued=1;socket->close_next=0;
    if(close_last)object(close_last)->close_next=socket->id;else close_first=socket->id;
    close_last=socket->id;
}
static void begin_close(socket_object_t *socket)
{
    if(socket->state==SOCKET_CLOSING)return;
    accept_unlink(socket);tap_unlink(socket);socket->state=SOCKET_CLOSING;socket->close_started=sc_ticks;
    if(socket->udp){udp_recv(socket->udp,0,0);udp_remove(socket->udp);socket->udp=0;}
    if(socket->raw){raw_recv(socket->raw,0,0);raw_remove(socket->raw);socket->raw=0;}
    if(socket->tcp){
        /* 排队不等于已经关闭：下一轮设备收包可能先处理FIN/RST并
         * 销毁PCB。此时socket对象仍由关闭队列持有，destroy通知必须
         * 保留以清空tcp指针，否则稍后的tcp_close会再次释放旧PCB。
         * 普通收发回调先撤销；寿命通知在实际close调用前才解绑。 */
        tcp_arg(socket->tcp,0);
        if(socket->tcp->state==LISTEN)tcp_accept(socket->tcp,0);
        else{tcp_err(socket->tcp,0);tcp_recv(socket->tcp,0);tcp_sent(socket->tcp,0);tcp_poll(socket->tcp,0,0);}
    }
    notify(socket);enqueue_close(socket);close_urgent=1;task_kernel_wake();
}
static int queue_packet(socket_object_t *socket,struct pbuf *packet,u32 address,u32 port)
{
    if(socket->state==SOCKET_CLOSING || socket->rx_shutdown || socket->rx_messages>=SOCKET_RX_MESSAGES || packet->tot_len>SOCKET_RX_BYTES-socket->rx_bytes)return -1;
    packet_node_t *node=sc_net_calloc(1,sizeof(*node));if(!node)return -1;
    node->packet=packet;node->address=address;node->port=port;node->bytes=packet->tot_len;
    if(socket->rx_last)socket->rx_last->next=node;else socket->rx_first=node;socket->rx_last=node;
    socket->rx_messages++;socket->rx_bytes+=node->bytes;queued_bytes+=node->bytes;socket->received+=node->bytes;notify(socket);return 0;
}
static void drop_packet(socket_object_t *socket)
{
    packet_node_t *node=socket->rx_first;if(!node)return;
    socket->rx_first=node->next;if(!socket->rx_first)socket->rx_last=0;
    u32 remaining=node->bytes-node->offset;socket->rx_bytes-=remaining;queued_bytes-=remaining;socket->rx_messages--;
    pbuf_free(node->packet);sc_net_free(node);
}
static err_t tcp_received(void *opaque,struct tcp_pcb *pcb,struct pbuf *packet,err_t error)
{
    socket_object_t *socket=opaque;
    if(!socket || socket->tcp!=pcb || socket->state==SOCKET_CLOSING){if(packet){tcp_recved(pcb,packet->tot_len);pbuf_free(packet);}return ERR_OK;}
    if(error!=ERR_OK){socket->error=sc_net_error(error);notify(socket);return error;}
    if(!packet){socket->rx_eof=1;notify(socket);return ERR_OK;}
    if(socket->rx_shutdown){tcp_recved(pcb,packet->tot_len);pbuf_free(packet);return ERR_OK;}
    if(queue_packet(socket,packet,address_value(&pcb->remote_ip),pcb->remote_port)<0)return ERR_MEM;
    return ERR_OK; /* 应用读走才tcp_recved；未读数据真的占接收窗口。 */
}
static err_t tcp_sent_data(void *opaque,struct tcp_pcb *pcb,u16_t bytes)
{(void)bytes;socket_object_t *socket=opaque;if(socket && socket->tcp==pcb)notify(socket);return ERR_OK;}
static void tcp_failed(void *opaque,err_t error)
{
    socket_object_t *socket=opaque;if(!socket)return;
    socket->tcp=0;socket->state=SOCKET_ERROR;socket->error=error==ERR_CLSD?0:sc_net_error(error);socket->rx_eof=1;notify(socket);
}
static void tcp_destroyed(u8_t id,void *opaque)
{
    (void)id;socket_object_t *socket=opaque;if(!socket)return;
    /* 半关闭接收侧时，上游正常结束不会调用errf；独立destroy回调
     * 覆盖RST/正常结束/超时/TIME_WAIT回收，不留已释放PCB指针。
     * 主动close或listen布局转换先detach本引用，避免迟到回调摸旧对象。 */
    socket->tcp=0;socket->rx_eof=1;socket->tx_shutdown=1;
    if(socket->state!=SOCKET_CLOSING)socket->state=SOCKET_ERROR;notify(socket);
}
static const struct tcp_ext_arg_callbacks tcp_lifetime={tcp_destroyed,0};
static void attach_tcp(socket_object_t *socket,struct tcp_pcb *pcb)
{
    socket->tcp=pcb;tcp_arg(pcb,socket);tcp_recv(pcb,tcp_received);tcp_sent(pcb,tcp_sent_data);tcp_err(pcb,tcp_failed);
    tcp_ext_arg_set_callbacks(pcb,tcp_owner_arg,&tcp_lifetime);tcp_ext_arg_set(pcb,tcp_owner_arg,socket);
    pcb->ttl=(u8_t)socket->ttl;
}
static err_t tcp_connected(void *opaque,struct tcp_pcb *pcb,err_t error)
{
    socket_object_t *socket=opaque;if(!socket || socket->tcp!=pcb)return ERR_ABRT;
    socket->state=error==ERR_OK?SOCKET_CONNECTED:SOCKET_ERROR;socket->error=sc_net_error(error);
    socket->local_address=address_value(&pcb->local_ip);socket->local_port=pcb->local_port;notify(socket);return ERR_OK;
}
static err_t tcp_accepted_connection(void *opaque,struct tcp_pcb *pcb,err_t error)
{
    socket_object_t *parent=opaque;
    if(!parent || parent->state!=SOCKET_LISTEN || error!=ERR_OK || parent->accept_count>=parent->backlog){tcp_abort(pcb);return ERR_ABRT;}
    socket_object_t *child=allocate_socket(parent->owner,NET_SOCKET_TCP);
    if(!child){tcp_abort(pcb);return ERR_ABRT;}
    child->ttl=parent->ttl;child->state=SOCKET_CONNECTED;attach_tcp(child,pcb);
    child->local_address=address_value(&pcb->local_ip);child->local_port=pcb->local_port;
    child->peer_address=address_value(&pcb->remote_ip);child->peer_port=pcb->remote_port;child->parent=parent->id;
    child->accept_prev=parent->accept_last;if(parent->accept_last)object(parent->accept_last)->accept_next=child->id;else parent->accept_first=child->id;
    parent->accept_last=child->id;parent->accept_count++;tcp_backlog_delayed(pcb);notify(parent);return ERR_OK;
}
static void udp_received(void *opaque,struct udp_pcb *pcb,struct pbuf *packet,const ip_addr_t *address,u16_t port)
{
    socket_object_t *socket=opaque;
    if(!socket || socket->udp!=pcb || queue_packet(socket,packet,address_value(address),port)<0){if(socket)socket->dropped++;pbuf_free(packet);}
}
static u8_t icmp_received(void *opaque,struct raw_pcb *pcb,struct pbuf *packet,const ip_addr_t *address)
{
    socket_object_t *socket=opaque;if(!socket || socket->raw!=pcb || socket->rx_shutdown)return 0;
    u8 head[128];u32 length=packet->tot_len;if(length<28)return 0;
    u32 copied=pbuf_copy_partial(packet,head,(u16_t)(length<sizeof(head)?length:sizeof(head)),0);
    u32 header=(head[0]&15u)*4u;if((head[0]>>4)!=4 || header<20 || copied<header+8)return 0;
    u8 *icmp=head+header;u32 identifier=0xFFFFFFFFu;
    if(icmp[0]==0 && !icmp[1])identifier=((u32)icmp[4]<<8)|icmp[5];
    else if(icmp[0]==3 || icmp[0]==11 || icmp[0]==12){
        u8 *inner=icmp+8;u32 quoted=header+8;if(copied<quoted+20)return 0;
        u32 inner_header=(inner[0]&15u)*4u;
        if((inner[0]>>4)!=4 || inner_header<20 || copied<quoted+inner_header+8 || inner[9]!=1)return 0;
        u8 *echo=inner+inner_header;if(echo[0]!=8 || echo[1])return 0;identifier=((u32)echo[4]<<8)|echo[5];
    }
    if(identifier!=socket->icmp_id)return 0;
    /* raw回调在ICMP校验前；自行检查完整校验和，再复制本对象拥有的
     * 原IP帧。不能ref上游pbuf后让其remove_header修改队列里的视图。
     * 普通用户只收本socket标识的echo/错误，不旁听别人的TCP/UDP。 */
    if(pbuf_remove_header(packet,header)!=0)return 0;u16_t checksum=inet_chksum_pbuf(packet);
    if(pbuf_add_header(packet,header)!=0)sc_net_assert("NET ICMP HEADER RESTORE");if(checksum)return 0;
    struct pbuf *copy=pbuf_alloc(PBUF_RAW,(u16_t)length,PBUF_RAM);
    if(!copy || pbuf_copy(copy,packet)!=ERR_OK || queue_packet(socket,copy,address_value(address),0)<0){if(copy)pbuf_free(copy);socket->dropped++;}
    return 0;
}
static int assign_icmp(socket_object_t *socket)
{
    for(u32 count=0;count<65536u;count++){
        u32 id=(icmp_cursor++)&65535u,mask=1u<<(id%32);
        if(!(icmp_used[id/32]&mask)){icmp_used[id/32]|=mask;socket->icmp_id=id;return 0;}
    }
    return -98; /* 16位线协议标识真的耗尽，不是任务创建门槛。 */
}
void sc_net_socket_init(void)
{
    objpool_init(&sockets,sizeof(socket_object_t),0);objpool_init(&owners,sizeof(socket_owner_t),0);
    objpool_init(&taps,sizeof(tap_entry_t),0);
    tcp_owner_arg=tcp_ext_arg_alloc_id();if(tcp_owner_arg==LWIP_TCP_PCB_NUM_EXT_ARG_ID_INVALID)sc_net_assert("NET TCP LIFETIME SLOT");
}
int network_socket_create(int pid,u32 type,u32 protocol)
{
    if(!sc_net_available())return -100;
    if(type<1 || type>4 || (type<=2 && protocol) || (type==3 && protocol && protocol!=1) || (type==4 && protocol && protocol!=ETHTYPE_ARP))return -95;
    sc_net_context(1);socket_object_t *socket=allocate_socket(pid,type);int result=-12;
    if(!socket){sc_net_context(0);return result;}
    if(type==NET_SOCKET_TCP){struct tcp_pcb *pcb=tcp_new_ip_type(IPADDR_TYPE_V4);if(pcb)attach_tcp(socket,pcb);else goto failed;}
    else if(type==NET_SOCKET_UDP){socket->udp=udp_new_ip_type(IPADDR_TYPE_V4);if(!socket->udp)goto failed;socket->udp->ttl=(u8_t)socket->ttl;udp_recv(socket->udp,udp_received,socket);}
    else if(type==NET_SOCKET_ICMP){result=assign_icmp(socket);if(result<0)goto failed;socket->raw=raw_new_ip_type(IPADDR_TYPE_V4,1);if(!socket->raw)goto failed;socket->raw->ttl=(u8_t)socket->ttl;raw_recv(socket->raw,icmp_received,socket);}
    else{if(objpool_claim(&taps,(int)socket->id)<0)goto failed;((tap_entry_t *)objpool_get(&taps,(int)socket->id))->handle=socket->id;
        socket->tap_next=tap_first;if(tap_first)object(tap_first)->tap_prev=socket->id;tap_first=socket->id;}
    result=(int)socket->id;sc_net_context(0);return result;
failed:
    tap_unlink(socket);release_socket(socket);sc_net_context(0);return result<0?result:-12;
}
int network_socket_bind(int pid,u32 handle,const u32 address[4])
{
    socket_object_t *socket=owned(pid,handle);if(!socket)return -9;if(!address_ok(address) || !local_ok(address[1]))return -99;
    if(socket->state!=SOCKET_NEW)return -22;if(address[2] && address[2]<1024 && !auth_can_manage(pid))return -13;
    sc_net_context(1);ip_addr_t ip=make_address(address[1]);err_t error=ERR_ARG;
    if(socket->tcp)error=tcp_bind(socket->tcp,&ip,(u16_t)address[2]);
    else if(socket->udp)error=udp_bind(socket->udp,&ip,(u16_t)address[2]);
    else if(socket->raw && !address[2])error=raw_bind(socket->raw,&ip);
    else if(socket->type==NET_SOCKET_ETHERNET && !address[1] && !address[2])error=ERR_OK;
    if(error==ERR_OK){socket->state=SOCKET_BOUND;socket->local_address=address[1];socket->local_port=socket->tcp?socket->tcp->local_port:socket->udp?socket->udp->local_port:0;}
    sc_net_context(0);return sc_net_error(error);
}
int network_socket_connect(int pid,u32 handle,const u32 address[4])
{
    socket_object_t *socket=owned(pid,handle);if(!socket)return -9;if(!address_ok(address) || !address[1])return -22;
    if(!sc_net_available())return -100;if(socket->state==SOCKET_CONNECTING)return -114;if(socket->state==SOCKET_CONNECTED)return -106;
    if(socket->state!=SOCKET_NEW && socket->state!=SOCKET_BOUND)return -22;
    if(socket->type<=2 && !address[2])return -22;sc_net_context(1);ip_addr_t ip=make_address(address[1]);err_t error=ERR_ARG;
    if(socket->tcp)error=tcp_connect(socket->tcp,&ip,(u16_t)address[2],tcp_connected);
    else if(socket->udp)error=udp_connect(socket->udp,&ip,(u16_t)address[2]);
    else if(socket->raw && !address[2])error=ERR_OK; /* 路由器的ICMP错误源地址与目标不同，只按本对象echo标识过滤。 */
    int result=sc_net_error(error);
    if(socket->udp){socket->local_port=socket->udp->local_port;socket->local_address=address_value(&socket->udp->local_ip);}
    if(socket->tcp){socket->local_port=socket->tcp->local_port;socket->local_address=address_value(&socket->tcp->local_ip);}
    if(error==ERR_OK){socket->peer_address=address[1];socket->peer_port=address[2];socket->state=socket->tcp?SOCKET_CONNECTING:SOCKET_CONNECTED;result=socket->tcp?-115:0;}
    sc_net_context(0);return result;
}
int network_socket_listen(int pid,u32 handle,u32 backlog)
{
    socket_object_t *socket=owned(pid,handle);if(!socket)return -9;
    if(!socket->tcp || (socket->state!=SOCKET_NEW && socket->state!=SOCKET_BOUND) || !backlog || backlog>255)return -22;
    sc_net_context(1);err_t error=ERR_OK;struct tcp_pcb *original=socket->tcp;
    tcp_ext_arg_set(original,tcp_owner_arg,0);struct tcp_pcb *listen=tcp_listen_with_backlog_and_err(original,(u8_t)backlog,&error);
    if(listen){socket->tcp=listen;socket->state=SOCKET_LISTEN;socket->backlog=backlog;socket->local_port=listen->local_port;tcp_arg(listen,socket);tcp_accept(listen,tcp_accepted_connection);tcp_ext_arg_set(listen,tcp_owner_arg,socket);}
    else tcp_ext_arg_set(original,tcp_owner_arg,socket);
    sc_net_context(0);return sc_net_error(error);
}
int network_socket_accept(int pid,u32 handle,u32 address[4])
{
    socket_object_t *socket=owned(pid,handle);if(!socket)return -9;if(socket->state!=SOCKET_LISTEN)return -22;
    if(!socket->accept_first)return -11;sc_net_context(1);socket_object_t *child=object(socket->accept_first);accept_unlink(child);
    if(address){address[0]=1;address[1]=child->peer_address;address[2]=child->peer_port;address[3]=0;}
    int result=(int)child->id;sc_net_context(0);return result;
}
static u32 copy_bytes(void *destination,u32 bytes,void *opaque)
{sc_net_memcpy(destination,opaque,bytes);return bytes;}
static int send_ethernet(const void *data,u32 bytes)
{
    if(bytes<42 || bytes>1514)return -90;const u8 *frame=data;const u8 *mac=e1000_mac();
    if(sc_net_memcmp(frame+6,mac,6) || frame[12]!=8 || frame[13]!=6 || frame[14] || frame[15]!=1 || frame[16]!=8 || frame[17] || frame[18]!=6 || frame[19]!=4 || frame[20] || (frame[21]!=1 && frame[21]!=2) || sc_net_memcmp(frame+22,mac,6))return -13;
    u32 sender=((u32)frame[28]<<24)|((u32)frame[29]<<16)|((u32)frame[30]<<8)|frame[31];struct netif *netif=sc_net_interface();
    if(sender && (!netif || sender!=lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(netif)))))return -13;
    int result=e1000_send(bytes,copy_bytes,(void *)data);return result<0?result:(int)bytes;
}
int network_socket_send(int pid,u32 handle,const void *data,u32 bytes,const u32 *address)
{
    socket_object_t *socket=owned(pid,handle);if(!socket)return -9;if(bytes && !data)return -22;
    static const u8 empty[1]={0};if(!bytes && !data)data=empty;
    if(socket->tx_shutdown)return -108;if(socket->error)return socket->error;if(!sc_net_available())return -100;
    if(address && !address_ok(address))return -22;sc_net_context(1);int result=-107;
    if(socket->type==NET_SOCKET_TCP){
        if(address || socket->state!=SOCKET_CONNECTED)goto done;if(!bytes){result=0;goto done;}
        u32 count=bytes>16384u?16384u:bytes;if(count>tcp_sndbuf(socket->tcp))count=tcp_sndbuf(socket->tcp);
        if(!count){result=-11;goto done;}err_t error=tcp_write(socket->tcp,data,(u16_t)count,TCP_WRITE_FLAG_COPY);
        if(error==ERR_OK){(void)tcp_output(socket->tcp);result=(int)count;}else result=sc_net_error(error);
    }else if(socket->type==NET_SOCKET_ETHERNET){if(address){result=-22;goto done;}result=send_ethernet(data,bytes);}
    else if(socket->udp || socket->raw){
        u32 target=address?address[1]:socket->peer_address,port=address?address[2]:socket->peer_port;
        if(!target || (socket->udp && !port) || (socket->raw && port)){result=-22;goto done;}
        if(bytes>(socket->udp?65507u:65515u)){result=-90;goto done;}
        if(socket->raw && (bytes<8 || ((const u8 *)data)[0]!=8 || ((const u8 *)data)[1])){result=-13;goto done;}
        struct pbuf *packet=pbuf_alloc(socket->udp?PBUF_TRANSPORT:PBUF_IP,(u16_t)bytes,PBUF_RAM);
        if(!packet){result=-11;goto done;}err_t error=pbuf_take(packet,data,(u16_t)bytes);ip_addr_t ip=make_address(target);
        if(error==ERR_OK && socket->raw){u8 *icmp=packet->payload;icmp[2]=icmp[3]=0;icmp[4]=(u8)(socket->icmp_id>>8);icmp[5]=(u8)socket->icmp_id;*((u16_t *)(icmp+2))=inet_chksum_pbuf(packet);}
        if(error==ERR_OK)error=socket->udp?udp_sendto(socket->udp,packet,&ip,(u16_t)port):raw_sendto(socket->raw,packet,&ip);
        pbuf_free(packet);result=error==ERR_OK?(int)bytes:sc_net_error(error);
    }
done:
    if(socket->udp){socket->local_port=socket->udp->local_port;socket->local_address=address_value(&socket->udp->local_ip);}
    if(result>0)socket->sent+=(u32)result;sc_net_context(0);return result;
}
int network_socket_receive(int pid,u32 handle,void *data,u32 bytes,u32 *address)
{
    socket_object_t *socket=owned(pid,handle);if(!socket)return -9;if(bytes && !data)return -22;
    if(socket->rx_shutdown)return 0;
    if(!socket->rx_first)return socket->error?socket->error:socket->rx_eof?0:-11;
    if(socket->type==NET_SOCKET_TCP && !bytes)return 0;sc_net_context(1);
    packet_node_t *node=socket->rx_first;u32 available=node->bytes-node->offset,count=bytes<available?bytes:available;
    if(count>65535u)count=65535u;if(count && pbuf_copy_partial(node->packet,data,(u16_t)count,(u16_t)node->offset)!=count){sc_net_context(0);return -5;}
    if(address){address[0]=1;address[1]=node->address;address[2]=node->port;address[3]=0;}
    socket->last_message=available;socket->truncated=socket->type!=NET_SOCKET_TCP && count<available;
    if(socket->type==NET_SOCKET_TCP){
        node->offset+=count;socket->rx_bytes-=count;queued_bytes-=count;if(socket->tcp)tcp_recved(socket->tcp,(u16_t)count);
        if(node->offset==node->bytes)drop_packet(socket);
    }else drop_packet(socket);
    sc_net_context(0);return (int)count;
}
int network_socket_shutdown(int pid,u32 handle,u32 how)
{
    socket_object_t *socket=owned(pid,handle);if(!socket)return -9;if(how>2 || socket->state==SOCKET_LISTEN)return -22;
    if(socket->type==SOCKET_DNS_TYPE)return -95;
    sc_net_context(1);err_t result=ERR_OK;int receive=how!=1,transmit=how!=0;
    int both=(receive || socket->rx_shutdown) && (transmit || socket->tx_shutdown);
    if(socket->type==NET_SOCKET_TCP && !socket->tcp && !socket->rx_eof){sc_net_context(0);return -107;}
    if(socket->tcp){
        struct tcp_pcb *pcb=socket->tcp;
        if(both){tcp_arg(pcb,0);tcp_err(pcb,0);tcp_recv(pcb,0);tcp_sent(pcb,0);tcp_ext_arg_set(pcb,tcp_owner_arg,0);}
        result=tcp_shutdown(pcb,both?1:receive,both?1:transmit);
        if(result==ERR_OK && both)socket->tcp=0;
        else if(result!=ERR_OK && both)attach_tcp(socket,pcb);
    }
    if(result==ERR_OK){
        if(receive){socket->rx_shutdown=1;
            if(socket->rx_first && !socket->drain_queued){socket->drain_queued=1;socket->drain_next=0;if(drain_last)object(drain_last)->drain_next=socket->id;else drain_first=socket->id;drain_last=socket->id;task_kernel_wake();}}
        if(transmit)socket->tx_shutdown=1;if(both){socket->rx_eof=1;socket->state=SOCKET_ERROR;}notify(socket);
    }
    sc_net_context(0);return sc_net_error(result);
}
int network_socket_close(int pid,u32 handle)
{socket_object_t *socket=owned(pid,handle);if(!socket)return -9;sc_net_context(1);begin_close(socket);sc_net_context(0);return 0;}
static u32 readiness(socket_object_t *socket)
{
    u32 ready=0;if(socket->rx_first || socket->rx_eof || socket->rx_shutdown)ready|=NET_READY_READ;
    if(socket->accept_first)ready|=NET_READY_ACCEPT|NET_READY_READ;
    if(socket->error || !sc_net_available())ready|=NET_READY_ERROR;
    if(socket->rx_eof || socket->state==SOCKET_ERROR || socket->state==SOCKET_CLOSING)ready|=NET_READY_HUP;
    if(!socket->tx_shutdown && !socket->error && sc_net_available() && e1000_link()){
        if(socket->type==NET_SOCKET_TCP){if(socket->state==SOCKET_CONNECTED && socket->tcp && tcp_sndbuf(socket->tcp) && tcp_sndqueuelen(socket->tcp)<TCP_SND_QUEUELEN)ready|=NET_READY_WRITE;}
        else if(socket->type<=NET_SOCKET_ETHERNET && e1000_tx_ready())ready|=NET_READY_WRITE;
    }
    return ready;
}
int network_socket_status(int pid,u32 handle,u32 out[16])
{
    socket_object_t *socket=owned(pid,handle);if(!socket)return -9;sc_net_context(1);
    for(u32 i=0;i<16;i++)out[i]=0;out[0]=1;out[1]=socket->type;out[2]=socket->state;out[3]=readiness(socket);out[4]=(u32)socket->error;
    out[5]=socket->rx_bytes;out[6]=socket->rx_messages;out[7]=socket->accept_count;out[8]=socket->local_address;
    out[9]=socket->local_port;out[10]=socket->peer_address;out[11]=socket->peer_port;out[12]=socket->last_message;
    out[13]=socket->truncated;out[14]=socket->icmp_id;out[15]=sc_net_config_epoch();sc_net_context(0);return 0;
}
int network_socket_option(int pid,u32 handle,u32 option,u32 value)
{
    socket_object_t *socket=owned(pid,handle);if(!socket)return -9;int result=0;sc_net_context(1);
    if(socket->type==NET_SOCKET_TCP && !socket->tcp){sc_net_context(0);return -107;}
    if(option==1){
        if(!value || value>255)result=-22;else{socket->ttl=value;if(socket->tcp)socket->tcp->ttl=(u8_t)value;if(socket->udp)socket->udp->ttl=(u8_t)value;if(socket->raw)socket->raw->ttl=(u8_t)value;}
    }else if(option==2 && value<=1 && socket->type==NET_SOCKET_UDP){if(value)ip_set_option(socket->udp,SOF_BROADCAST);else ip_reset_option(socket->udp,SOF_BROADCAST);}
    else if(option==3 && value<=1 && socket->type==NET_SOCKET_TCP && socket->state!=SOCKET_LISTEN){if(value)ip_set_option(socket->tcp,SOF_KEEPALIVE);else ip_reset_option(socket->tcp,SOF_KEEPALIVE);}
    else if(option==4 && value<=1 && socket->type==NET_SOCKET_TCP && socket->state!=SOCKET_LISTEN){if(value)tcp_nagle_disable(socket->tcp);else tcp_nagle_enable(socket->tcp);}
    else if(option==5 && value<=1 && socket->type==NET_SOCKET_TCP){if(value)ip_set_option(socket->tcp,SOF_REUSEADDR);else ip_reset_option(socket->tcp,SOF_REUSEADDR);}
    else if(option==5 && value<=1 && socket->type==NET_SOCKET_UDP){if(value)ip_set_option(socket->udp,SOF_REUSEADDR);else ip_reset_option(socket->udp,SOF_REUSEADDR);}
    else result=-22;sc_net_context(0);return result;
}
int network_socket_page(int pid,u32 cursor,u32 *out,u32 words)
{
    if(words<32 || words>1040 || cursor>0x7FFFFFFEu)return -22;
    for(u32 i=0;i<words;i++)out[i]=0;out[0]=1;out[1]=sc_net_config_epoch();out[5]=16;
    int admin=auth_can_manage(pid);u32 capacity=(words-16)/16,count=0;
    int id=objpool_next(&sockets,cursor?(int)cursor:-1);
    u32 scan_budget=256;
    while(id>=0 && count<capacity && scan_budget--){
        socket_object_t *socket=object((u32)id);out[3]=(u32)id;
        if(socket->state!=SOCKET_CLOSING && (admin || (socket->owner_generation==task_generation(socket->owner) && auth_subject_uid(socket->owner)==auth_subject_uid(pid)))){
            u32 *row=out+16+16*count++;row[0]=socket->id;row[1]=(u32)socket->owner;row[2]=socket->owner_generation;row[3]=socket->type;row[4]=socket->state;
            row[5]=socket->local_address;row[6]=socket->local_port;row[7]=socket->peer_address;row[8]=socket->peer_port;row[9]=socket->rx_bytes;
            row[10]=socket->sent;row[11]=socket->received;row[12]=(u32)socket->error;row[13]=socket->dropped;row[14]=readiness(socket);row[15]=socket->icmp_id;
        }
        id=objpool_next(&sockets,id);
    }
    out[2]=count;if(id<0)out[3]=0;return (int)count;
}
static void dns_finished(const char *name,const ip_addr_t *address,void *opaque)
{
    (void)name;socket_object_t *socket=object((u32)opaque);
    if(!socket || socket->type!=SOCKET_DNS_TYPE || socket->state==SOCKET_CLOSING)return;
    socket->dns_state=address?2:3;socket->dns_address=address?address_value(address):0;socket->error=address?0:-110;notify(socket);
}
int network_dns_start(int pid,const char *name)
{
    if(!sc_net_available())return -100;if(!name || !*name)return -22;u32 length=0;
    while(length<256 && name[length])length++;if(length>=256)return -36;
    sc_net_context(1);socket_object_t *socket=allocate_socket(pid,SOCKET_DNS_TYPE);if(!socket){sc_net_context(0);return -12;}
    socket->state=SOCKET_DNS;socket->dns_state=1;sc_net_memcpy(socket->dns_name,name,length+1);ip_addr_t address;
    err_t error=dns_gethostbyname_addrtype(socket->dns_name,&address,dns_finished,(void *)socket->id,LWIP_DNS_ADDRTYPE_IPV4);
    if(error==ERR_OK){socket->dns_state=2;socket->dns_address=address_value(&address);}
    else if(error!=ERR_INPROGRESS){release_socket(socket);sc_net_context(0);return sc_net_error(error);}
    int handle=(int)socket->id;sc_net_context(0);return handle;
}
int network_dns_status(int pid,u32 ticket,u32 out[8])
{
    socket_object_t *socket=owned(pid,ticket);if(!socket || socket->type!=SOCKET_DNS_TYPE)return -9;
    if(socket->error && socket->dns_state==1)socket->dns_state=3;
    for(u32 i=0;i<8;i++)out[i]=0;out[0]=1;out[1]=socket->dns_state;out[2]=socket->dns_address;out[3]=(u32)socket->error;out[4]=sc_net_config_epoch();out[7]=socket->id;return 0;
}
int network_dns_cancel(int pid,u32 ticket)
{socket_object_t *socket=owned(pid,ticket);if(!socket || socket->type!=SOCKET_DNS_TYPE)return -9;return network_socket_close(pid,ticket);}
void sc_net_socket_frame(const unsigned char *frame,unsigned int bytes)
{
    if(!tap_first || bytes<42 || frame[12]!=8 || frame[13]!=6 || tap_frames_count>=64)return;
    tap_frame_t *work=sc_net_calloc(1,sizeof(*work));if(!work)return;
    work->packet=pbuf_alloc(PBUF_RAW,(u16_t)bytes,PBUF_RAM);
    if(!work->packet || pbuf_take(work->packet,frame,(u16_t)bytes)!=ERR_OK){if(work->packet)pbuf_free(work->packet);sc_net_free(work);return;}
    work->end=next_handle-1;work->address=((u32)frame[28]<<24)|((u32)frame[29]<<16)|((u32)frame[30]<<8)|frame[31];
    if(tap_frames_last)tap_frames_last->next=work;else tap_frames_first=work;tap_frames_last=work;tap_frames_count++;task_kernel_wake();
}
void sc_net_socket_changed(u32 epoch,int terminal_error)
{
    notification_epoch=epoch;if(terminal_error)notification_error=terminal_error;
    /* 高频TX完成不能反复把游标归零，否则编号64之后的socket会饿死。
     * 当前遍历继续，合并变化要求下一遍补齐已经经过的对象。 */
    if(notification_active)notification_again=1;
    else{notification_cursor=0;notification_active=1;}task_kernel_wake();
}
void sc_net_socket_poll(void)
{
    /* 一帧ARP可能有大量订阅者。每轮最多64次真实投递，独立稀疏索引
     * 仅遍历ARP消费者；保存编号游标/本帧最大编号，不借已关闭对象的
     * 旧next指针。后建socket不接收历史帧，关闭就删除索引。 */
    u32 fanout_budget=64;
    while(tap_frames_first && fanout_budget--){
        tap_frame_t *work=tap_frames_first;int id=objpool_next(&taps,work->cursor?(int)work->cursor:-1);
        if(id<0 || (u32)id>work->end){tap_frames_first=work->next;if(!tap_frames_first)tap_frames_last=0;pbuf_free(work->packet);sc_net_free(work);tap_frames_count--;continue;}
        work->cursor=(u32)id;socket_object_t *socket=object((u32)id);
        if(socket && socket->state!=SOCKET_CLOSING && !socket->rx_shutdown){
            struct pbuf *packet=pbuf_alloc(PBUF_RAW,work->packet->tot_len,PBUF_RAM);
            if(!packet || pbuf_copy(packet,work->packet)!=ERR_OK || queue_packet(socket,packet,work->address,ETHTYPE_ARP)<0){if(packet)pbuf_free(packet);socket->dropped++;}
        }
    }
    u32 budget=64;close_batch_tick=sc_ticks;int progress=0;
    while(drain_first && budget--){
        socket_object_t *socket=object(drain_first);drain_first=socket->drain_next;if(!drain_first)drain_last=0;
        socket->drain_next=0;drop_packet(socket);progress=1;
        if(socket->rx_first){if(drain_last)object(drain_last)->drain_next=socket->id;else drain_first=socket->id;drain_last=socket->id;}
        else socket->drain_queued=0;
    }
    budget=64;
    while(close_first && budget--){
        socket_object_t *socket=object(close_first);close_first=socket->close_next;if(!close_first)close_last=0;
        socket->close_queued=0;socket->close_next=0;
        if(socket->accept_first){begin_close(object(socket->accept_first));enqueue_close(socket);progress=1;continue;}
        if(socket->drain_queued){enqueue_close(socket);continue;}
        if(socket->rx_first){drop_packet(socket);enqueue_close(socket);progress=1;continue;}
        if(socket->tcp){
            if((i32)(sc_ticks-socket->close_retry)<0){enqueue_close(socket);continue;}
            /* 致命协议状态不再有重传/FIN定时服务。直接终止非listen
             * PCB，不能让tcp_close成功后留下永远不会结束的孤儿。 */
            if(sc_net_protocol_failed() && socket->tcp->state!=LISTEN){tcp_abort(socket->tcp);socket->tcp=0;}
        }
        if(socket->tcp){
            /* 原始API在同一网络上下文内连续完成解绑和close，中间
             * 不调度其它协议收包。成功后socket可释放而PCB继续FIN；
             * 失败表示PCB仍有效，重试队列必须恢复销毁通知。 */
            tcp_ext_arg_set(socket->tcp,tcp_owner_arg,0);
            err_t error=tcp_close(socket->tcp);
            if(error!=ERR_OK){
                tcp_ext_arg_set(socket->tcp,tcp_owner_arg,socket);
                if((u32)(sc_ticks-socket->close_started)<500u){socket->close_retry=sc_ticks+1;enqueue_close(socket);continue;}
                tcp_abort(socket->tcp);
            }
            socket->tcp=0;
        }
        release_socket(socket);progress=1;
    }
    if(notification_active){
        int id=objpool_next(&sockets,notification_cursor?(int)notification_cursor:-1);u32 batch=64;
        while(id>=0 && batch--){socket_object_t *socket=object((u32)id);notification_cursor=(u32)id;
            if(socket->state!=SOCKET_CLOSING){if(notification_error){socket->error=notification_error;socket->rx_eof=1;}notify(socket);}
            id=objpool_next(&sockets,id);
        }
        if(id<0){if(notification_again){notification_again=0;notification_cursor=0;}else{notification_active=0;notification_error=0;}}
    }
    if(sc_net_protocol_failed()){
        /* 正常主动关闭遗留的TIME_WAIT也归协议回收。异常停定时器时
         * 逐批abort，不把旧连接状态永久留成协议页泄漏。其destroy
         * 回调只会通知仍存在的对象；关闭对象早已detach私有引用。 */
        u32 terminal_budget=32;
        while(terminal_budget){
            struct tcp_pcb *pcb=tcp_active_pcbs?tcp_active_pcbs:tcp_bound_pcbs?tcp_bound_pcbs:tcp_tw_pcbs;
            if(pcb)tcp_abort(pcb);
            else if(tcp_listen_pcbs.pcbs){if(tcp_close(tcp_listen_pcbs.pcbs)!=ERR_OK)sc_net_assert("NET FAILED LISTENER CLOSE");}
            else break;terminal_budget--;
        }
        if(tcp_active_pcbs || tcp_bound_pcbs || tcp_tw_pcbs || tcp_listen_pcbs.pcbs)task_kernel_wake();
    }
    close_urgent=progress && close_first;
    if(close_urgent || drain_first || notification_active || tap_frames_first)task_kernel_wake();
    (void)notification_epoch;
}
int sc_net_socket_pending(void)
{return tap_frames_first || drain_first || notification_active || (close_first && (close_urgent || close_batch_tick!=sc_ticks))
    || (sc_net_protocol_failed() && (tcp_active_pcbs || tcp_bound_pcbs || tcp_tw_pcbs || tcp_listen_pcbs.pcbs));}
unsigned int sc_net_socket_count(void){return live_sockets;}
unsigned int sc_net_socket_rx_bytes(void){return queued_bytes;}
int network_stop_owner_step(int pid)
{
    socket_owner_t *owner=objpool_get(&owners,pid);if(!owner)return 1;sc_net_context(1);
    if(!owner->cleanup_cursor)owner->cleanup_cursor=owner->first;socket_object_t *socket=object(owner->cleanup_cursor);
    if(socket){owner->cleanup_cursor=socket->owner_next;begin_close(socket);}
    int finished=!owner->first;sc_net_context(0);return finished;
}
