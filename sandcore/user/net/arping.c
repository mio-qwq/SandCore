#include "NETCLI.inc"
int main(void)
{const char *usage="[-I en0] [-c COUNT] [-w SECONDS] [-D] IPv4  (ARP request; -D duplicate-address probe)";int start=net_start("arping",usage);if(start)return start>0?0:2;
    u32 count=3,seconds=1,target=0;int dad=0,has_target=0;
    for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];if(equal(p,"-D"))dad=1;
        else if(equal(p,"-I")){if(++i>=cli_argc||!net_interface(cli_argv[i]))return 2;}
        else if(equal(p,"-c")||equal(p,"-w")){if(++i>=cli_argc)return 2;u32 *v=equal(p,"-c")?&count:&seconds;if(net_u32(cli_argv[i],v==&count?0xFFFFFFFFu:86400u,v)<0||!*v)return 2;}
        else if(!has_target&&sc_net_parse_ip(p,&target)==0)has_target=1;else return net_usage("arping",usage);}
    if(!has_target || !target)return net_usage("arping",usage);u32 info[96];int result=sc_net_info(info);if(result<0)return cli_error("arping",result);if(!dad&&!info[4])return cli_error("arping",-99);
    int handle=sc_socket(SC_NET_ARP,0x0806);if(handle<0)return cli_error("arping",handle);u8 request[42],reply[1514];net_zero(request,sizeof(request));
    for(u32 i=0;i<6;i++){request[i]=255;request[6+i]=i<4?(u8)(info[10]>>(i*8)):(u8)(info[11]>>((i-4)*8));request[22+i]=request[6+i];}
    net_put16(request+12,0x0806);net_put16(request+14,1);net_put16(request+16,0x0800);request[18]=6;request[19]=4;net_put16(request+20,1);
    net_put32(request+28,dad?0:info[4]);net_put32(request+38,target);u32 answered=0;
    for(u32 probe=0;probe<count;probe++){u32 began=(u32)sc_tick();result=net_send((u32)handle,request,sizeof(request),0,seconds*100u);if(result<0)break;
        int found=0;while(net_remaining(began,seconds*100u)){
            result=net_receive((u32)handle,reply,sizeof(reply),0,net_remaining(began,seconds*100u));if(result<0)break;
            if(result<42 || net_be16(reply+12)!=0x0806 || net_be16(reply+14)!=1 || net_be16(reply+16)!=0x0800 || reply[18]!=6 || reply[19]!=4)continue;
            int own=1;for(u32 i=0;i<6;i++)if(reply[22+i]!=request[22+i])own=0;if(own)continue;
            u32 op=net_be16(reply+20),source=net_be32(reply+28);if((op==2&&source==target) || (dad&&op==1&&source==0&&net_be32(reply+38)==target)){
                u32 low=0;for(u32 i=0;i<4;i++)low|=(u32)reply[22+i]<<(8*i);cli_text(1,"Reply from ");net_ip(1,target);cli_text(1," [");net_mac(1,low,reply[26]|((u32)reply[27]<<8));cli_text(1,"]\n");answered++;found=1;break;}}
        if(!found&&result==-110){cli_text(1,"No ARP reply\n");result=0;}else if(result<0)break;if(dad&&found)break;
        if(probe+1<count){u32 left=net_remaining(began,seconds*100u);if(left)net_delay(left);}}
    sc_socket_close((u32)handle);if(result<0)return cli_error("arping",result);return dad?(answered?1:0):(answered?0:1);}
