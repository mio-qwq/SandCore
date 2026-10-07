#ifndef SANDCORE_SCNET_H
#define SANDCORE_SCNET_H
#include "SCAPI.H"
/* SandCore专用网络便利层，不提供BSD socket/libc伪兼容。所有等待
 * 先查真实状态，再EVENTWAIT；事件是提示，重试调用决定是否成功。 */
#define SC_NET_SET_ADDRESS 1u
#define SC_NET_UP 2u
#define SC_NET_DOWN 3u
#define SC_NET_DHCP_START 4u
#define SC_NET_DHCP_RENEW 5u
#define SC_NET_DHCP_RELEASE 6u
#define SC_NET_RESET 7u
#define SC_NET_SET_DNS 8u
#define SC_NET_SET_HOSTNAME 9u
#define SC_NET_SET_DOMAIN 10u
#define SC_NET_ARP_ADD 11u
#define SC_NET_ARP_DELETE 12u
#define SC_NET_ARP_FLUSH 13u
#define SC_NET_ROUTE_ADD 14u
#define SC_NET_ROUTE_DELETE 15u
#define SC_NET_NEW 1u
#define SC_NET_BOUND 2u
#define SC_NET_CONNECTING 3u
#define SC_NET_CONNECTED 4u
#define SC_NET_LISTENING 5u
#define SC_NET_FAILED 6u
#define SC_NET_CLOSING 7u
#define SC_NET_RESOLVING 8u
#define SC_NET_OPT_TTL 1u
#define SC_NET_OPT_BROADCAST 2u
#define SC_NET_OPT_KEEPALIVE 3u
#define SC_NET_OPT_NODELAY 4u
#define SC_NET_OPT_REUSEADDR 5u
static inline void sc_net_address(u32 out[4],u32 ipv4,u32 port)
{out[0]=1;out[1]=ipv4;out[2]=port;out[3]=0;}
static inline void sc_net_request(u32 out[32])
{for(int i=0;i<32;i++)out[i]=0;out[0]=1;}
static int sc_net_parse_ip(const char *text,u32 *out)
{
    u32 value=0;for(int part=0;part<4;part++){
        if(*text<'0' || *text>'9')return -1;u32 octet=0,digits=0;
        while(*text>='0' && *text<='9'){if(++digits>3)return -1;octet=octet*10+(u32)(*text++-'0');if(octet>255)return -1;}
        value=(value<<8)|octet;if(part<3){if(*text++!='.')return -1;}else if(*text)return -1;
    }
    *out=value;return 0;
}
static void sc_net_format_ip(u32 value,char out[16])
{
    int at=0;for(int part=0;part<4;part++){
        u32 octet=(value>>(24-part*8))&255u;if(octet>=100)out[at++]=(char)('0'+octet/100);
        if(octet>=10)out[at++]=(char)('0'+(octet/10)%10);out[at++]=(char)('0'+octet%10);
        if(part<3)out[at++]='.';
    }
    out[at]=0;
}
static int sc_net_wait(u32 handle,u32 wanted,u32 ticks)
{
    u32 began=(u32)sc_tick();for(;;){
        u32 status[16];int result=sc_socket_status(handle,status);if(result<0)return result;
        if(status[3]&wanted)return (int)status[3];if(status[4])return (int)status[4];
        if(status[3]&SC_NET_ERROR)return -100;if(status[3]&SC_NET_HUP)return -108;
        u32 elapsed=(u32)sc_tick()-began;if(ticks!=0xFFFFFFFFu && elapsed>=ticks)return -110;
        u32 left=ticks==0xFFFFFFFFu?ticks:ticks-elapsed;
        sc_event_wait(SC_EVENT_NETWORK,left);
    }
}
static int sc_net_resolve(const char *name,u32 *ipv4,u32 ticks)
{
    if(sc_net_parse_ip(name,ipv4)==0)return 0;
    const char *local="localhost";u32 index=0;while(name[index] && name[index]==local[index])index++;
    if(!name[index] && !local[index]){*ipv4=0x7F000001u;return 0;}
    int ticket=sc_dns_start(name);if(ticket<0)return ticket;u32 began=(u32)sc_tick();int result;
    for(;;){
        u32 state[8];result=sc_dns_status((u32)ticket,state);if(result<0)break;
        if(state[1]==2){*ipv4=state[2];result=0;break;}if(state[1]==3){result=state[3]?(int)state[3]:-110;break;}
        u32 elapsed=(u32)sc_tick()-began;if(ticks!=0xFFFFFFFFu && elapsed>=ticks){result=-110;break;}
        sc_event_wait(SC_EVENT_NETWORK,ticks==0xFFFFFFFFu?ticks:ticks-elapsed);
    }
    sc_dns_cancel((u32)ticket);return result;
}
static int sc_net_connect(u32 ipv4,u32 port,u32 ticks)
{
    int handle=sc_socket(SC_NET_TCP,0);if(handle<0)return handle;u32 address[4];sc_net_address(address,ipv4,port);
    int result=sc_socket_connect((u32)handle,address);
    if(result==-115)result=sc_net_wait((u32)handle,SC_NET_WRITE,ticks);
    if(result<0){sc_socket_close((u32)handle);return result;}return handle;
}
static int sc_net_send_all(u32 handle,const void *bytes,u32 count,u32 ticks)
{
    u32 at=0,began=(u32)sc_tick();while(at<count){
        u32 take=count-at;if(take>65536u)take=65536u;int written=sc_socket_send(handle,(const u8 *)bytes+at,take,0);
        if(written==-11){u32 elapsed=(u32)sc_tick()-began;if(ticks!=0xFFFFFFFFu && elapsed>=ticks)return -110;
            /* 就绪不是内存预留：另一个对象或分配失败仍能产生-11。
             * 此时至少让出一个PIT间隔，不能以WRITE位持续自旋。 */
            sc_event_wait(SC_EVENT_NETWORK,1);
            int wait=sc_net_wait(handle,SC_NET_WRITE,ticks==0xFFFFFFFFu?ticks:ticks-elapsed);if(wait<0)return wait;continue;}
        if(written<=0)return written?written:-5;at+=(u32)written;
    }
    return (int)at;
}
#endif
