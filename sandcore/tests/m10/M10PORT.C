#include "SCIO.H"
#include "SCNET.H"
/* 地址复用不能改变旧socket默认绑定规则，更不能产生两个相同的
 * TCP监听者。本探针在真实协议栈上区分bind与listen的生命周期。 */
static int failures;
static void check(int okay,const char *message)
{cli_text(1,okay?"PASS ":"FAIL ");cli_text(1,message);cli_text(1,"\n");if(!okay)failures++;}
static void ports(u32 type,u32 port)
{
    int a=sc_socket(type,0),b=sc_socket(type,0);u32 address[4];
    sc_net_address(address,0,port);
    check(a>0 && b>0,"address reuse sockets are real distinct objects");
    if(a<1 || b<1)goto done;
    check(sc_socket_option((u32)a,SC_NET_OPT_REUSEADDR,2)==-22,"address reuse rejects values outside zero or one");
    check(sc_socket_bind((u32)a,address)==0,"first default bind succeeds");
    check(sc_socket_bind((u32)b,address)==-98,"old default rejects an occupied address");
    check(sc_socket_option((u32)b,SC_NET_OPT_REUSEADDR,1)==0
        && sc_socket_bind((u32)b,address)==-98,"one endpoint cannot override another default binding");
    check(sc_socket_option((u32)a,SC_NET_OPT_REUSEADDR,1)==0
        && sc_socket_bind((u32)b,address)==0,"explicit reuse on both endpoints allows shared bind");
    if(type==SC_NET_TCP){
        check(sc_socket_listen((u32)a,1)==0,"first reused TCP address listens");
        check(sc_socket_listen((u32)b,1)==-98,"address reuse still rejects duplicate TCP listeners");
    }
done:
    if(a>0)sc_socket_close((u32)a);if(b>0)sc_socket_close((u32)b);
    if(a>0)check(sc_socket_option((u32)a,SC_NET_OPT_REUSEADDR,1)==-9,"closed handle cannot alter options");
}
int main(void)
{
    if(cli_parse()<0)return 2;
    ports(SC_NET_TCP,18020);ports(SC_NET_UDP,18021);
    int handle=sc_socket(SC_NET_TCP,0);
    check(handle>0,"old option probe socket opens");
    if(handle>0){
        check(sc_socket_option((u32)handle,SC_NET_OPT_TTL,64)==0
            && sc_socket_option((u32)handle,SC_NET_OPT_TTL,0)==-22
            && sc_socket_option((u32)handle,SC_NET_OPT_TTL,256)==-22,"old TTL option keeps its range");
        check(sc_socket_option((u32)handle,SC_NET_OPT_KEEPALIVE,1)==0
            && sc_socket_option((u32)handle,SC_NET_OPT_NODELAY,1)==0,"old TCP options remain usable");
        check(sc_socket_option((u32)handle,SC_NET_OPT_REUSEADDR,1)==0
            && sc_socket_option((u32)handle,SC_NET_OPT_REUSEADDR,0)==0,"address reuse can be disabled before bind");
        sc_socket_close((u32)handle);
    }
    handle=sc_socket(SC_NET_ICMP,0);
    if(handle>0){check(sc_socket_option((u32)handle,SC_NET_OPT_REUSEADDR,1)==-22,"address reuse rejects ICMP sockets");sc_socket_close((u32)handle);}
    else check(0,"ICMP option probe socket opens");
    cli_text(1,"port_failures=");cli_number(1,failures);cli_text(1,"\n");return failures?1:0;
}
