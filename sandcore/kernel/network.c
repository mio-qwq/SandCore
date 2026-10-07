#include "network.h"
#include "e1000.h"
#include "auth.h"
#include "task.h"
#include "timer.h"
#include "objpool.h"
#include "net_port/network_internal.h"
#include "lwip/init.h"
#include "lwip/pbuf.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/prot/dhcp.h"
#include "lwip/dns.h"
#include "lwip/timeouts.h"
#include "lwip/stats.h"
#include "lwip/ip.h"
#include "lwip/ip4_frag.h"
#include "lwip/prot/ethernet.h"
#include "netif/ethernet.h"

typedef struct route_node {
    struct route_node *child[2],*parent;
    ip4_addr_t gateway;
    u32 destination,prefix,metric;
    int id;
} route_node_t;
typedef struct {route_node_t *node;} route_entry_t;
static route_node_t routes={.id=-1};
static objpool_t route_entries;
static struct netif interface;
static char hostname[64]="sandcore",domain[64];
static u32 ready,config_epoch=1,dhcp_enabled=1,admin_up=1,last_service_tick;
static u32 startup_error,protocol_failed,driver_epoch,loop_pending;
static u32 ingress_dropped,egress_route_errors,service_batches;
static u32 failure_reassembly_steps,failure_dns_steps;

int sc_net_error(err_t error)
{
    switch(error){
    case ERR_OK:return 0;case ERR_MEM:case ERR_BUF:case ERR_WOULDBLOCK:return -11;
    case ERR_TIMEOUT:return -110;case ERR_RTE:return -101;case ERR_INPROGRESS:return -115;
    case ERR_VAL:case ERR_ARG:return -22;case ERR_USE:return -98;case ERR_ALREADY:return -114;
    case ERR_ISCONN:return -106;case ERR_CONN:return -107;case ERR_IF:return -100;
    case ERR_ABRT:return -103;case ERR_RST:return -104;case ERR_CLSD:return -108;
    default:return -5;
    }
}
int sc_net_available(void)
{return ready && !protocol_failed && admin_up && e1000_state()==E1000_RUNNING;}
int sc_net_protocol_failed(void){return protocol_failed!=0;}
struct netif *sc_net_interface(void){return ready?&interface:0;}
u32 sc_net_config_epoch(void){return config_epoch;}
static void changed(void)
{if(!++config_epoch)config_epoch=1;sc_net_socket_changed(config_epoch,0);}
static void status_changed(struct netif *netif){(void)netif;changed();}
static u32 host_ip(const ip4_addr_t *ip){return lwip_ntohl(ip4_addr_get_u32(ip));}
static ip4_addr_t make_ip(u32 address){ip4_addr_t ip;ip4_addr_set_u32(&ip,lwip_htonl(address));return ip;}
static int unicast(u32 address)
{u32 top=address>>24;return address && top && top!=127 && top<224 && address!=0xFFFFFFFFu;}
static u32 prefix_mask(u32 prefix){return prefix?0xFFFFFFFFu<<(32-prefix):0;}
static int mask_prefix(u32 mask)
{u32 prefix=0;while(prefix<32 && (mask&(1u<<(31-prefix))))prefix++;return prefix_mask(prefix)==mask?(int)prefix:-1;}
static route_node_t *lookup_route(u32 destination)
{
    route_node_t *node=&routes,*best=node->id>=0?node:0;
    for(u32 depth=0;depth<32;depth++){
        node=node->child[(destination>>(31-depth))&1u];if(!node)break;if(node->id>=0)best=node;
    }
    return best;
}
const ip4_addr_t *sc_net_gateway(struct netif *netif,const ip4_addr_t *destination)
{
    if(netif!=&interface)return 0;route_node_t *route=lookup_route(host_ip(destination));
    return route && ip4_addr_get_u32(&route->gateway)?&route->gateway:0;
}
static u32 copy_packet(void *destination,u32 bytes,void *opaque)
{return pbuf_copy_partial((const struct pbuf *)opaque,destination,(u16_t)bytes,0);}
static err_t link_output(struct netif *netif,struct pbuf *packet)
{
    (void)netif;int result=e1000_send(packet->tot_len,copy_packet,packet);
    if(result==-11)return ERR_MEM;if(result<0)return ERR_IF;return ERR_OK;
}
static err_t interface_ip_output(struct netif *netif,struct pbuf *packet,const ip4_addr_t *destination)
{
    u32 target=host_ip(destination),mask=host_ip(netif_ip4_netmask(netif)),local=host_ip(netif_ip4_addr(netif));
    if(ip4_addr_isbroadcast(destination,netif))return etharp_output(netif,packet,destination);
    if(!netif_is_up(netif) || !netif_is_link_up(netif))return ERR_IF;
    if(ip4_addr_ismulticast(destination))return ERR_RTE; /* 本轮IPv4单播主机，未启IGMP。 */
    route_node_t *route=lookup_route(target);int connected=mask && ((target&mask)==(local&mask));
    int connected_prefix=mask_prefix(mask);
    if(route && (!connected || route->prefix>=(u32)connected_prefix)){
        ip4_addr_t next=ip4_addr_get_u32(&route->gateway)?route->gateway:*destination;
        u32 next_host=host_ip(&next);
        if(ip4_addr_get_u32(&route->gateway) && (!mask || (next_host&mask)!=(local&mask))){egress_route_errors++;return ERR_RTE;}
        /* 直接按选定下一跳查询ARP，故同网段的更精确gateway路由也
         * 生效；不能只挂ETHARP_GET_GW让上游同网段分支绕过路由表。 */
        return etharp_query(netif,&next,packet);
    }
    if(connected)return etharp_output(netif,packet,destination);
    if(!ip4_addr_isany_val(*netif_ip4_gw(netif)))return etharp_output(netif,packet,destination);
    egress_route_errors++;return ERR_RTE;
}
static err_t interface_init(struct netif *netif)
{
    netif->name[0]='e';netif->name[1]='n';netif->hwaddr_len=6;netif->mtu=1500;
    netif->flags=NETIF_FLAG_BROADCAST|NETIF_FLAG_ETHARP|NETIF_FLAG_ETHERNET;
    sc_net_memcpy(netif->hwaddr,e1000_mac(),6);netif->hostname=hostname;
    netif->output=interface_ip_output;netif->linkoutput=link_output;return ERR_OK;
}
static int receive_frame(const u8 *frame,u32 bytes,void *opaque)
{
    (void)opaque;
    if(!ready || protocol_failed || !admin_up)return -1;
    sc_net_socket_frame(frame,bytes);
    struct pbuf *packet=pbuf_alloc(PBUF_RAW,(u16_t)bytes,PBUF_RAM);
    if(!packet){ingress_dropped++;return -12;}
    if(pbuf_take(packet,frame,(u16_t)bytes)!=ERR_OK){pbuf_free(packet);ingress_dropped++;return -12;}
    err_t result=interface.input(packet,&interface);
    if(result!=ERR_OK){pbuf_free(packet);ingress_dropped++;return sc_net_error(result);}return 0;
}
void network_init(void)
{
    sc_net_context(1);objpool_init(&route_entries,sizeof(route_entry_t),0);sc_net_socket_init();
    int device=e1000_init();
    if(device<0){startup_error=1;sc_net_context(0);return;}
    if(sc_net_random_init()<0){startup_error=2;e1000_control(0);sc_net_context(0);return;}
    if(sc_net_timers_prepare()<0){startup_error=3;e1000_control(0);sc_net_context(0);return;}
    lwip_init();ip4_addr_t zero=make_ip(0);
    if(sc_net_timer_failed() || !netif_add(&interface,&zero,&zero,&zero,0,interface_init,ethernet_input)){
        startup_error=3;protocol_failed=1;e1000_control(0);sc_net_context(0);return;
    }
    ready=1;netif_set_default(&interface);netif_set_status_callback(&interface,status_changed);
    netif_set_link_callback(&interface,status_changed);netif_set_up(&interface);
    /* DHCP由真实链路上升启动；没有把QEMU的默认地址硬编码成成功。
     * 启动阶段与隐藏桌面也必须服务租约/ARP，不依赖任一GUI应用。 */
    sc_net_context(0);
}
void network_poll(void)
{
    if(!ready || task_pid()!=0)return;
    if(!e1000_pending() && sc_ticks==last_service_tick && !loop_pending && !sc_net_socket_pending())return;
    last_service_tick=sc_ticks;task_render_hold(1);sc_net_context(1);sti();service_batches++;
    u32 differences=e1000_poll(receive_frame,0);
    u32 driver[32];e1000_snapshot(driver);
    if(driver[2]!=driver_epoch || (differences&1u)){
        driver_epoch=driver[2];int linked=e1000_link();
        if(linked && admin_up){
            sc_net_memcpy(interface.hwaddr,e1000_mac(),6);
            if(!netif_is_link_up(&interface)){
                netif_set_link_up(&interface);
                struct dhcp *lease=netif_dhcp_data(&interface);
                if(dhcp_enabled && (!lease || lease->state==DHCP_STATE_OFF)){
                    if(dhcp_start(&interface)!=ERR_OK)startup_error=4;
                }
            }
        }else if(netif_is_link_up(&interface))netif_set_link_down(&interface);
        changed();
    }
    if(sc_net_timer_failed() && !protocol_failed){
        protocol_failed=1;startup_error=5;netif_set_down(&interface);e1000_control(0);
        dhcp_release_and_stop(&interface);dhcp_cleanup(&interface);
        failure_reassembly_steps=IP_REASS_MAXAGE+1;
        /* DNS请求的随机UDP端口属于上游查询，而不是应用句柄。终止
         * 正常定时器以后仍须走它的超时释放：每个服务器的线性重试
         * 最坏1+1+2+...+(R-1)轮，另留NEW与切换边界余量。
         * 一轮至多检查DNS_TABLE_SIZE项，不循环催熟全部请求。 */
        failure_dns_steps=(DNS_MAX_RETRIES*(DNS_MAX_RETRIES+1)/2+2)*DNS_MAX_SERVERS+2;
        etharp_cleanup_netif(&interface);
        sc_net_socket_changed(++config_epoch,-12);
    }
    if(!protocol_failed){sys_check_timeouts();sc_net_loop_poll(&interface);}
    else{
        /* 失败后不再交付本机旧报文。分片最多64pbuf，逐服务轮执行一次
         * 到期清理直到最长寿命归零；这是终止协议的回收，不报告正常
         * 租约/网络时间或成功包，避免未服务队列永远唤醒task0。 */
        sc_net_loop_discard(&interface);
        if(failure_reassembly_steps){ip_reass_tmr();failure_reassembly_steps--;if(failure_reassembly_steps)task_kernel_wake();}
        if(failure_dns_steps){dns_tmr();failure_dns_steps--;if(failure_dns_steps)task_kernel_wake();}
    }
    loop_pending=interface.loop_first!=0;
    sc_net_socket_poll();
    if(differences&2u)sc_net_socket_changed(config_epoch,0);
    cli();sc_net_context(0);task_render_hold(0);
}
static int set_route(const u32 *in,int remove)
{
    u32 target=in[1],prefix=in[2],gateway=in[3],metric=in[4];
    if(prefix>32 || (target&~prefix_mask(prefix)) || (gateway && !unicast(gateway)))return -22;
    u32 mask=host_ip(netif_ip4_netmask(&interface)),local=host_ip(netif_ip4_addr(&interface));
    if(gateway && (!mask || (gateway&mask)!=(local&mask) || gateway==local))return -101;
    route_node_t *node=&routes,*first_new=0;
    for(u32 depth=0;depth<prefix;depth++){
        u32 side=(target>>(31-depth))&1u;
        if(!node->child[side]){
            if(remove)return -2;route_node_t *next=sc_net_calloc(1,sizeof(*next));
            if(!next){
                /* 新路径尚无已提交分支，沿唯一孩子完整回滚，原路由
                 * 和页统计不变；不拿部分成功节点伪造可用路由。 */
                if(first_new){route_node_t *parent=first_new->parent;
                    if(parent->child[0]==first_new)parent->child[0]=0;else parent->child[1]=0;
                    while(first_new){route_node_t *following=first_new->child[0]?first_new->child[0]:first_new->child[1];sc_net_free(first_new);first_new=following;}}
                return -12;
            }
            next->parent=node;next->id=-1;node->child[side]=next;if(!first_new)first_new=next;
        }
        node=node->child[side];
    }
    if(remove){
        if(node->id<0)return -2;objpool_release(&route_entries,node->id);node->id=-1;
        while(node!=&routes && node->id<0 && !node->child[0] && !node->child[1]){
            route_node_t *parent=node->parent;if(parent->child[0]==node)parent->child[0]=0;else parent->child[1]=0;
            sc_net_free(node);node=parent;
        }
    }else{
        if(node->id<0){int id=objpool_alloc(&route_entries);if(id<0){
            while(node!=&routes && node->id<0 && !node->child[0] && !node->child[1]){
                route_node_t *parent=node->parent;if(parent->child[0]==node)parent->child[0]=0;else parent->child[1]=0;sc_net_free(node);node=parent;
            }return -12;
        }node->id=id;((route_entry_t *)objpool_get(&route_entries,id))->node=node;}
        node->destination=target;node->prefix=prefix;node->gateway=make_ip(gateway);node->metric=metric;
    }
    changed();return 0;
}
int network_control(int pid,u32 operation,const u32 in[NET_CONTROL_WORDS])
{
    if(!auth_can_manage(pid))return -13;if(!in || in[0]!=1)return -22;
    if(!ready)return startup_error==2?-126:-19;if(protocol_failed)return -5;
    sc_net_context(1);int result=0;ip4_addr_t addr,mask,gateway;ip_addr_t dns;
    switch(operation){
    case NET_SET_ADDRESS:{
        int prefix=mask_prefix(in[2]);
        if(!unicast(in[1]) || prefix<0 || prefix==0 || (prefix<31 && ((in[1]&~in[2])==0 || (in[1]&~in[2])==~in[2]))
            || (in[3] && (!unicast(in[3]) || (in[3]&in[2])!=(in[1]&in[2]) || in[3]==in[1]))
            || (in[4] && !unicast(in[4])) || (in[5] && !unicast(in[5])) || in[6]<68 || in[6]>1500){result=-22;break;}
        dhcp_release_and_stop(&interface);dhcp_enabled=0;
        addr=make_ip(in[1]);mask=make_ip(in[2]);gateway=make_ip(in[3]);interface.mtu=(u16_t)in[6];
        netif_set_addr(&interface,&addr,&mask,&gateway);
        ip_addr_set_ip4_u32(&dns,lwip_htonl(in[4]));dns_setserver(0,&dns);
        ip_addr_set_ip4_u32(&dns,lwip_htonl(in[5]));dns_setserver(1,&dns);break;
    }
    case NET_UP:admin_up=1;netif_set_up(&interface);if(e1000_state()!=E1000_RUNNING)result=e1000_control(1);break;
    case NET_DOWN:admin_up=0;dhcp_release_and_stop(&interface);netif_set_down(&interface);netif_set_link_down(&interface);result=e1000_control(0);break;
    case NET_DHCP_START:if(!admin_up){result=-100;break;}dhcp_enabled=1;result=sc_net_error(dhcp_start(&interface));break;
    case NET_DHCP_RENEW:if(!dhcp_enabled || !netif_dhcp_data(&interface))result=-22;else result=sc_net_error(dhcp_renew(&interface));break;
    case NET_DHCP_RELEASE:dhcp_release_and_stop(&interface);dhcp_enabled=0;break;
    case NET_RESET:result=e1000_control(1);break;
    case NET_SET_DNS:
        if((in[1] && !unicast(in[1])) || (in[2] && !unicast(in[2]))){result=-22;break;}
        ip_addr_set_ip4_u32(&dns,lwip_htonl(in[1]));dns_setserver(0,&dns);ip_addr_set_ip4_u32(&dns,lwip_htonl(in[2]));dns_setserver(1,&dns);break;
    case NET_SET_HOSTNAME:case NET_SET_DOMAIN:{
        const char *text=(const char *)(in+1);u32 size=0;while(size<64 && text[size])size++;
        if(size>=64 || (!size && operation==NET_SET_HOSTNAME)){result=-22;break;}
        for(u32 i=0;i<size;i++)if(!((text[i]>='a' && text[i]<='z') || (text[i]>='A' && text[i]<='Z') || (text[i]>='0' && text[i]<='9') || text[i]=='-' || (text[i]=='.' && operation==NET_SET_DOMAIN))){result=-22;break;}
        if(result)break;char *to=operation==NET_SET_HOSTNAME?hostname:domain;sc_net_memset(to,0,64);sc_net_memcpy(to,text,size);break;
    }
    case NET_ARP_ADD:{
        struct eth_addr mac;addr=make_ip(in[1]);
        if(!unicast(in[1]) || in[3]>65535u){result=-22;break;}
        for(u32 i=0;i<4;i++)mac.addr[i]=(u8_t)(in[2]>>(8*i));mac.addr[4]=(u8_t)in[3];mac.addr[5]=(u8_t)(in[3]>>8);
        u32 any=0,all=255;for(u32 i=0;i<6;i++){any|=mac.addr[i];all&=mac.addr[i];}
        if(!any || all==255 || (mac.addr[0]&1u)){result=-22;break;}
        result=sc_net_error(etharp_add_static_entry(&addr,&mac));break;
    }
    case NET_ARP_DELETE:addr=make_ip(in[1]);result=sc_net_error(etharp_remove_static_entry(&addr));break;
    case NET_ARP_FLUSH:etharp_cleanup_netif(&interface);break;
    case NET_ROUTE_ADD:case NET_ROUTE_DELETE:result=set_route(in,operation==NET_ROUTE_DELETE);break;
    default:result=-22;break;
    }
    if(!result)changed();sc_net_context(0);return result;
}
int network_info(u32 out[NET_INFO_WORDS])
{
    sc_net_context(1);
    for(u32 i=0;i<NET_INFO_WORDS;i++)out[i]=0;out[0]=1;out[1]=config_epoch;
    out[2]=(ready?1u:0u)|(admin_up?2u:0u)|(e1000_link()?4u:0u)|(dhcp_enabled?8u:0u)|(protocol_failed?16u:0u);
    out[3]=startup_error;out[4]=host_ip(netif_ip4_addr(&interface));out[5]=host_ip(netif_ip4_netmask(&interface));
    out[6]=host_ip(netif_ip4_gw(&interface));
    out[7]=host_ip(ip_2_ip4(dns_getserver(0)));out[8]=host_ip(ip_2_ip4(dns_getserver(1)));out[9]=interface.mtu;
    const u8 *mac=e1000_mac();for(u32 i=0;i<4;i++)out[10]|=(u32)mac[i]<<(8*i);out[11]=mac[4]|((u32)mac[5]<<8);
    struct dhcp *lease=netif_dhcp_data(&interface);
    if(lease){out[12]=lease->state;out[13]=lease->tries;out[14]=lease->offered_t0_lease;out[15]=lease->lease_used*DHCP_COARSE_TIMER_SECS;}
    out[16]=sc_net_socket_count();out[17]=sc_net_socket_rx_bytes();out[18]=sc_net_pages();out[19]=sc_net_peak_pages();out[20]=sc_net_used();
    out[21]=ingress_dropped;out[22]=egress_route_errors;out[23]=service_batches;out[24]=LWIP_VERSION;
    out[25]=lwip_stats.ip.recv;out[26]=lwip_stats.ip.drop;out[27]=lwip_stats.ip.err;
    out[28]=lwip_stats.tcp.recv;out[29]=lwip_stats.tcp.xmit;out[30]=lwip_stats.udp.recv;out[31]=lwip_stats.icmp.recv;
    sc_net_memcpy(out+32,hostname,64);sc_net_memcpy(out+48,domain,64);e1000_snapshot(out+64);sc_net_context(0);return 0;
}
int network_route_page(u32 cursor,u32 *out,u32 words)
{
    if(words<24 || words>528 || cursor>0x7FFFFFFFu)return -22;
    for(u32 i=0;i<words;i++)out[i]=0;out[0]=1;out[1]=config_epoch;out[4]=route_entries.objects;out[5]=8;
    u32 count=0,capacity=(words-16)/8;int id=objpool_next(&route_entries,cursor?(int)cursor-1:-1);
    while(id>=0 && count<capacity){
        route_node_t *node=((route_entry_t *)objpool_get(&route_entries,id))->node;u32 *row=out+16+8*count++;
        row[0]=(u32)id+1;row[1]=node->destination;row[2]=node->prefix;row[3]=host_ip(&node->gateway);row[4]=node->metric;row[5]=1;
        out[3]=(u32)id+1;id=objpool_next(&route_entries,id);
    }
    out[2]=count;if(id<0)out[3]=0;return (int)count;
}
int network_arp_page(u32 cursor,u32 *out,u32 words)
{
    if(words<24 || words>528 || cursor>ARP_TABLE_SIZE)return -22;
    sc_net_context(1);
    for(u32 i=0;i<words;i++)out[i]=0;out[0]=1;out[1]=config_epoch;out[5]=8;
    u32 count=0,capacity=(words-16)/8,index=cursor;
    for(;index<ARP_TABLE_SIZE && count<capacity;index++){
        ip4_addr_t *address;struct netif *netif;struct eth_addr *mac;
        if(!etharp_get_entry(index,&address,&netif,&mac) || netif!=&interface)continue;
        u32 *row=out+16+8*count++;row[0]=index;row[1]=host_ip(address);
        for(u32 i=0;i<4;i++)row[2]|=(u32)mac->addr[i]<<(8*i);row[3]=mac->addr[4]|((u32)mac->addr[5]<<8);row[4]=1;
    }
    out[2]=count;out[3]=index<ARP_TABLE_SIZE?index:0;sc_net_context(0);return (int)count;
}
