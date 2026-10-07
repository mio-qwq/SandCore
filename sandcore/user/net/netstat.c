#include "NETCLI.inc"
static const char *net_state(u32 state)
{return state==1?"NEW":state==2?"BOUND":state==3?"CONNECTING":state==4?"CONNECTED":state==5?"LISTEN":state==6?"ERROR":state==7?"CLOSING":state==8?"DNS":"?";}
int main(void)
{const char *usage="[-a] [-n] [-l] [-t|-u]  (same-account sockets; admin sees all)";int start=net_start("netstat",usage);if(start)return start>0?0:2;
    int listening=0,filter=0;for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];if(*p++!='-')return net_usage("netstat",usage);while(*p){if(*p=='l')listening=1;else if(*p=='t')filter=1;else if(*p=='u')filter=2;else if(*p!='a'&&*p!='n')return net_usage("netstat",usage);p++;}}
    u32 page[16+16*32],cursor=0;cli_text(1,"Proto Local address Remote address State PID Handle RX bytes\n");
    do{int result=sc_socket_page(cursor,page,sizeof(page)/4);if(result<0)return cli_error("netstat",result);
        for(u32 i=0;i<page[2];i++){u32 *r=page+16+16*i;if((filter&&r[3]!=(u32)filter)||(listening&&r[4]!=5))continue;
            cli_text(1,r[3]==1?"tcp  ":r[3]==2?"udp  ":r[3]==3?"icmp ":r[3]==4?"arp  ":"dns  ");net_ip(1,r[5]);cli_text(1,":");net_uint(1,r[6]);
            cli_text(1," ");net_ip(1,r[7]);cli_text(1,":");net_uint(1,r[8]);cli_text(1," ");cli_text(1,net_state(r[4]));
            cli_text(1," ");net_uint(1,r[1]);cli_text(1," ");net_uint(1,r[0]);cli_text(1," ");net_uint(1,r[9]);cli_text(1,"\n");}cursor=page[3];}while(cursor);return 0;}
