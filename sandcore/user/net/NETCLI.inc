#ifndef SANDCORE_NETCLI_INC
#define SANDCORE_NETCLI_INC
#include "../SCIO.H"
#include "../SCNET.H"
/* 各命令有自己的main/SCX，只共享参数与协议字节助手。整数IPv4是
 * SCNET地址合同；ARP/ICMP/TFTP的线缆字段必须显式大端，不借C布局。 */
static void net_uint(int fd,u32 value)
{char out[11];int n=0;do{out[n++]=(char)('0'+value%10);value/=10;}while(value);for(int i=0;i<n/2;i++){char c=out[i];out[i]=out[n-1-i];out[n-1-i]=c;}out[n]=0;cli_text(fd,out);}
static int net_u32(const char *text,u32 limit,u32 *out)
{if(!*text)return -1;u32 value=0;while(*text){if(*text<'0'||*text>'9')return -1;u32 d=(u32)(*text++-'0');if(d>limit || value>(limit-d)/10)return -1;value=value*10+d;}*out=value;return 0;}
static int net_start(const char *name,const char *usage)
{if(cli_parse()<0)return -1;if(cli_argc && (equal(cli_argv[0],"--help")||equal(cli_argv[0],"-h"))){cli_text(1,name);cli_text(1,": ");cli_text(1,usage);cli_text(1,"\n");return 1;}return 0;}
static int net_usage(const char *name,const char *usage)
{cli_text(2,name);cli_text(2,": ");cli_text(2,usage);cli_text(2,"\n");return 2;}
static u16 net_be16(const u8 *p){return (u16)(((u16)p[0]<<8)|p[1]);}
static u32 net_be32(const u8 *p){return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];}
static void net_put16(u8 *p,u32 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static void net_put32(u8 *p,u32 v){p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;}
static void net_zero(void *data,u32 n){u8 *p=data;for(u32 i=0;i<n;i++)p[i]=0;}
static void net_copy(void *to,const void *from,u32 n){u8 *p=to;const u8 *q=from;for(u32 i=0;i<n;i++)p[i]=q[i];}
static char *net_find(char *text,char c){while(*text){if(*text==c)return text;text++;}return 0;}
static int net_ci_equal(const char *a,const char *b)
{for(;;){int x=*a++,y=*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return 0;if(!x)return 1;}}
static void net_ip(int fd,u32 address){char text[16];sc_net_format_ip(address,text);cli_text(fd,text);}
static u32 net_mask(u32 prefix){return prefix?0xFFFFFFFFu<<(32-prefix):0;}
static int net_prefix(u32 mask)
{u32 p=0;while(p<32 && (mask&(1u<<(31-p))))p++;return net_mask(p)==mask?(int)p:-1;}
static int net_cidr(const char *text,u32 *address,u32 *prefix,int host)
{char ip[16];u32 n=0;while(text[n] && text[n]!='/'){if(n>=15)return -1;ip[n]=text[n];n++;}ip[n]=0;
    if(sc_net_parse_ip(ip,address)<0)return -1;*prefix=32;if(text[n] && net_u32(text+n+1,32,prefix)<0)return -1;
    if(!host)*address&=net_mask(*prefix);return 0;}
static int net_hex(char c)
{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
static int net_mac_parse(const char *text,u8 out[6])
{for(int i=0;i<6;i++){if(!text[0] || !text[1])return -1;int a=net_hex(text[0]),b=net_hex(text[1]);if(a<0||b<0)return -1;out[i]=(u8)(16*a+b);text+=2;if(i<5){if(*text++!=':')return -1;}else if(*text)return -1;}return 0;}
static void net_mac(int fd,u32 low,u32 high)
{const char *digits="0123456789abcdef";char text[18];for(u32 i=0;i<6;i++){u32 v=i<4?(low>>(8*i))&255u:(high>>(8*(i-4)))&255u;text[i*3]=digits[v>>4];text[i*3+1]=digits[v&15];text[i*3+2]=i==5?0:':';}cli_text(fd,text);}
static int net_interface(const char *name){return equal(name,"en0");}
static int net_simple_control(u32 operation){u32 request[32];sc_net_request(request);return sc_net_control(operation,request);}
static int net_text_control(u32 operation,const char *text)
{int n=length(text);if(n>63)return -36;u32 request[32];sc_net_request(request);net_copy(request+1,text,(u32)n+1);return sc_net_control(operation,request);}
static int net_static(u32 address,u32 mask,u32 gateway,u32 mtu)
{u32 info[96],request[32];if(sc_net_info(info)<0)return -19;sc_net_request(request);
    request[1]=address;request[2]=mask;request[3]=gateway;request[4]=info[7];request[5]=info[8];request[6]=mtu?mtu:1500;
    return sc_net_control(SC_NET_SET_ADDRESS,request);}
static int net_show_interface(void)
{u32 info[96];int result=sc_net_info(info);if(result<0)return result;
    cli_text(1,"en0  e1000  ");cli_text(1,(info[2]&2)?"UP":"DOWN");cli_text(1,(info[2]&4)?"  LINK\n":"  NO-CARRIER\n");
    cli_text(1,"  inet ");net_ip(1,info[4]);cli_text(1,"/");int prefix=net_prefix(info[5]);if(prefix<0)cli_text(1,"?");else net_uint(1,(u32)prefix);
    cli_text(1,"  gateway ");net_ip(1,info[6]);cli_text(1,"  mtu ");net_uint(1,info[9]);cli_text(1,"\n  ether ");net_mac(1,info[10],info[11]);
    cli_text(1,"  dns ");net_ip(1,info[7]);cli_text(1," ");net_ip(1,info[8]);
    cli_text(1,"\n  RX packets/bytes/dropped ");net_uint(1,info[75]);cli_text(1,"/");net_uint(1,info[76]);cli_text(1,"/");net_uint(1,info[77]);
    cli_text(1,"  TX packets/bytes ");net_uint(1,info[80]);cli_text(1,"/");net_uint(1,info[81]);
    cli_text(1,"\n  DHCP state ");net_uint(1,info[12]);cli_text(1,"  startup error ");net_uint(1,info[3]);cli_text(1,"\n");return 0;}
static void net_route_row(u32 destination,u32 prefix,u32 gateway,u32 metric,const char *kind)
{net_ip(1,destination);cli_text(1,"/");net_uint(1,prefix);cli_text(1," via ");net_ip(1,gateway);cli_text(1," dev en0 metric ");net_uint(1,metric);cli_text(1," ");cli_text(1,kind);cli_text(1,"\n");}
static int net_show_routes(void)
{u32 info[96],page[16+8*32],cursor=0;if(sc_net_info(info)<0)return -19;int p=net_prefix(info[5]);
    if(info[4]&&p>0)net_route_row(info[4]&info[5],(u32)p,0,0,"connected");if(info[6])net_route_row(0,0,info[6],0,"interface-default");
    do{int result=sc_route_page(cursor,page,sizeof(page)/4);if(result<0)return result;
        for(u32 i=0;i<page[2];i++){u32 *r=page+16+i*8;net_route_row(r[1],r[2],r[3],r[4],"static");}cursor=page[3];}while(cursor);return 0;}
static int net_route_command(int first,int ip_style)
{if(first>=cli_argc || equal(cli_argv[first],"show") || equal(cli_argv[first],"-n"))return first+1<cli_argc?-22:net_show_routes();
    int remove=equal(cli_argv[first],"del");if(!remove&&!equal(cli_argv[first],"add"))return -22;first++;
    int host=0;if(first<cli_argc&&(equal(cli_argv[first],"-net")||equal(cli_argv[first],"-host"))){host=equal(cli_argv[first],"-host");first++;}
    if(first>=cli_argc)return -22;u32 request[32];sc_net_request(request);
    if(equal(cli_argv[first],"default")){request[1]=request[2]=0;}else if(net_cidr(cli_argv[first],request+1,request+2,0)<0)return -22;
    if(host)request[2]=32;first++;
    while(first<cli_argc){const char *option=cli_argv[first++];if(first>=cli_argc)return -22;const char *value=cli_argv[first++];
        if(equal(option,ip_style?"via":"gw")){if(sc_net_parse_ip(value,request+3)<0)return -22;}
        else if(equal(option,"metric")){if(net_u32(value,0xFFFFFFFFu,request+4)<0)return -22;}
        else if(equal(option,"dev")){if(!net_interface(value))return -19;}else return -22;}
    return sc_net_control(remove?SC_NET_ROUTE_DELETE:SC_NET_ROUTE_ADD,request);}
static int net_show_arp(void)
{u32 page[16+8*32],cursor=0;cli_text(1,"Address          HWaddress          Device\n");
    do{int result=sc_arp_page(cursor,page,sizeof(page)/4);if(result<0)return result;for(u32 i=0;i<page[2];i++){u32 *r=page+16+i*8;net_ip(1,r[1]);cli_text(1,"  ");net_mac(1,r[2],r[3]);cli_text(1,"  en0\n");}cursor=page[3];}while(cursor);return 0;}
static int net_arp_set(const char *address,const char *mac,int remove)
{u32 request[32];sc_net_request(request);if(sc_net_parse_ip(address,request+1)<0)return -22;
    if(!remove){u8 bytes[6];if(net_mac_parse(mac,bytes)<0)return -22;for(u32 i=0;i<4;i++)request[2]|=(u32)bytes[i]<<(8*i);request[3]=bytes[4]|((u32)bytes[5]<<8);}
    return sc_net_control(remove?SC_NET_ARP_DELETE:SC_NET_ARP_ADD,request);}
static u32 net_remaining(u32 began,u32 ticks)
{if(ticks==0xFFFFFFFFu)return ticks;u32 elapsed=(u32)sc_tick()-began;return elapsed>=ticks?0:ticks-elapsed;}
static int net_send(u32 handle,const void *data,u32 count,const u32 *address,u32 ticks)
{u32 began=(u32)sc_tick();for(;;){int n=sc_socket_send(handle,data,count,address);if(n!=-11)return n;u32 left=net_remaining(began,ticks);if(!left)return -110;
    sc_event_wait(SC_EVENT_NETWORK,1);int ready=sc_net_wait(handle,SC_NET_WRITE,left);if(ready<0)return ready;}}
static int net_receive(u32 handle,void *data,u32 count,u32 *address,u32 ticks)
{u32 began=(u32)sc_tick();for(;;){int n=sc_socket_receive(handle,data,count,address);if(n!=-11)return n;u32 left=net_remaining(began,ticks);if(!left)return -110;
    int ready=sc_net_wait(handle,SC_NET_READ,left);if(ready<0)return ready;}}
static void net_delay(u32 ticks)
{u32 began=(u32)sc_tick();while(net_remaining(began,ticks))sc_event_wait(0,net_remaining(began,ticks));}
static int net_wait_address(u32 ticks)
{u32 began=(u32)sc_tick();for(;;){u32 info[96];int result=sc_net_info(info);if(result<0)return result;if(info[4]&&(info[2]&4))return 0;
    if(!(info[2]&1))return -19;if(info[2]&16)return -5;u32 left=net_remaining(began,ticks);if(!left)return -110;
    /* DHCP配置变化通知现有socket；本命令没有socket时也按20tick
     * 周期查询，不能无限等待一个未订阅的网络事件。 */
    sc_event_wait(SC_EVENT_NETWORK,left>20?20:left);}}
static int net_icmp_reply(const u8 *packet,u32 bytes,u32 id,u32 sequence,u32 *source,u32 *type)
{if(bytes<28 || (packet[0]>>4)!=4)return 0;u32 ihl=(packet[0]&15u)*4u;if(ihl<20 || bytes<ihl+8 || packet[9]!=1)return 0;
    const u8 *icmp=packet+ihl;*source=net_be32(packet+12);*type=icmp[0];
    if(icmp[0]==0)return net_be16(icmp+4)==id && net_be16(icmp+6)==sequence;
    if(icmp[0]!=3 && icmp[0]!=11 && icmp[0]!=12)return 0;u32 inner=ihl+8;if(bytes<inner+20)return 0;
    const u8 *quote=packet+inner;u32 qhl=(quote[0]&15u)*4u;if(qhl<20 || bytes<inner+qhl+8 || quote[9]!=1)return 0;
    return quote[qhl]==8 && net_be16(quote+qhl+4)==id && net_be16(quote+qhl+6)==sequence;}
#endif
