#include "NETCLI.inc"
int main(void)
{const char *usage="[-m MAXTTL] [-w SECONDS] [-q PROBES] HOST  (ICMP echo probes)";int start=net_start("traceroute",usage);if(start)return start>0?0:2;
    u32 maximum=30,seconds=2,probes=3;const char *host=0;for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];if(*p=='-'){
            if(++i>=cli_argc)return 2;u32 *value=equal(p,"-m")?&maximum:equal(p,"-w")?&seconds:equal(p,"-q")?&probes:0;
            if(!value||net_u32(cli_argv[i],value==&seconds?86400u:255u,value)<0||!*value)return 2;
        }else if(!host)host=p;else return net_usage("traceroute",usage);}
    if(!host)return net_usage("traceroute",usage);u32 target;int result=sc_net_resolve(host,&target,1000);if(result<0)return cli_error("traceroute",result);
    int handle=sc_socket(SC_NET_ICMP,1);if(handle<0)return cli_error("traceroute",handle);u32 status[16],address[4];sc_socket_status((u32)handle,status);u32 id=status[14],sequence=0;sc_net_address(address,target,0);
    u8 request[16],reply[2048];net_zero(request,sizeof(request));request[0]=8;int done=0,error=0;
    cli_text(1,"traceroute to ");net_ip(1,target);cli_text(1,"\n");
    for(u32 ttl=1;ttl<=maximum&&!done;ttl++){if(sc_socket_option((u32)handle,1,ttl)<0){error=-22;break;}net_uint(1,ttl);
        for(u32 probe=0;probe<probes;probe++){net_put16(request+6,++sequence);u32 began=(u32)sc_tick();result=net_send((u32)handle,request,sizeof(request),address,seconds*100u);if(result<0){error=result;break;}
            int found=0;u32 source=0,type=0;while(net_remaining(began,seconds*100u)){
                result=net_receive((u32)handle,reply,sizeof(reply),0,net_remaining(began,seconds*100u));if(result<0)break;
                if(net_icmp_reply(reply,(u32)result,id,sequence,&source,&type)){found=1;break;}}
            if(found){cli_text(1,"  ");net_ip(1,source);cli_text(1," ");net_uint(1,((u32)sc_tick()-began)*10u);cli_text(1,"ms");if(type==0&&source==target)done=1;
                else if(type==3||type==12){cli_text(1," !ICMP");net_uint(1,type);done=1;error=-101;}}
            else if(result==-110)cli_text(1,"  *");else if(result<0){error=result;break;}
        }cli_text(1,"\n");if(error)break;
    }
    sc_socket_close((u32)handle);return error?cli_error("traceroute",error):done?0:1;}
