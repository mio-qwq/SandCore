#include "NETCLI.inc"
int main(void)
{const char *usage="[-c COUNT] [-W SECONDS] [-i SECONDS] [-s BYTES] HOST  (default 4 probes; -c 0 continues)";int start=net_start("ping",usage);if(start)return start>0?0:2;
    u32 count=4,seconds=2,interval=1,payload=56;const char *host=0;
    for(int i=0;i<cli_argc;i++){const char *p=cli_argv[i];if(*p=='-'){if(++i>=cli_argc)return net_usage("ping",usage);
            u32 *target=equal(p,"-c")?&count:equal(p,"-W")?&seconds:equal(p,"-i")?&interval:equal(p,"-s")?&payload:0;
            u32 limit=target==&payload?65499u:target==&count?0xFFFFFFFFu:86400u;if(!target||net_u32(cli_argv[i],limit,target)<0)return 2;
        }else if(!host)host=p;else return net_usage("ping",usage);}
    if(!host || !seconds)return net_usage("ping",usage);u32 target;int result=sc_net_resolve(host,&target,1000);if(result<0)return cli_error("ping",result);
    int handle=sc_socket(SC_NET_ICMP,1);if(handle<0)return cli_error("ping",handle);u32 status[16],address[4];sc_socket_status((u32)handle,status);u32 id=status[14];sc_net_address(address,target,0);
    u8 *request=sc_alloc(payload+8),*reply=sc_alloc(65536);if(!request||!reply){if(request)sc_free(request);if(reply)sc_free(reply);sc_socket_close((u32)handle);return cli_error("ping",-12);}
    net_zero(request,payload+8);request[0]=8;for(u32 i=8;i<payload+8;i++)request[i]=(u8)i;
    cli_text(1,"PING ");cli_text(1,host);cli_text(1," (");net_ip(1,target);cli_text(1,")\n");u32 sent=0,received=0,min_ticks=0xFFFFFFFFu,max_ticks=0,sum_ticks=0;
    for(u32 sequence=0;!count || sequence<count;sequence++){
        u32 began=(u32)sc_tick();net_put16(request+6,sequence&65535u);result=net_send((u32)handle,request,payload+8,address,seconds*100u);if(result<0)break;sent++;
        int matched=0;u32 source=0,type=0;
        while(net_remaining(began,seconds*100u)){
            result=net_receive((u32)handle,reply,65536,0,net_remaining(began,seconds*100u));if(result<0)break;
            if(net_icmp_reply(reply,(u32)result,id,sequence&65535u,&source,&type)){matched=1;break;}}
        if(matched&&type==0){u32 ticks=(u32)sc_tick()-began;received++;sum_ticks+=ticks;if(ticks<min_ticks)min_ticks=ticks;if(ticks>max_ticks)max_ticks=ticks;
            net_uint(1,(u32)result);cli_text(1," bytes from ");net_ip(1,source);cli_text(1," seq=");net_uint(1,sequence);cli_text(1," time=");net_uint(1,ticks*10u);cli_text(1," ms\n");}
        else if(matched){cli_text(1,"ICMP error ");net_uint(1,type);cli_text(1," from ");net_ip(1,source);cli_text(1,"\n");}
        else if(result==-110){cli_text(1,"Request timed out\n");result=0;}else if(result<0)break;
        if(count&&sequence+1==count)break;u32 left=net_remaining(began,interval*100u);if(left)net_delay(left);
    }
    sc_socket_close((u32)handle);sc_free(request);sc_free(reply);cli_text(1,"Sent ");net_uint(1,sent);cli_text(1,", received ");net_uint(1,received);
    if(received){cli_text(1,", RTT min/avg/max ");net_uint(1,min_ticks*10u);cli_text(1,"/");net_uint(1,(sum_ticks/received)*10u);cli_text(1,"/");net_uint(1,max_ticks*10u);cli_text(1," ms (PIT 10 ms resolution)");}cli_text(1,"\n");
    return result<0?cli_error("ping",result):received?0:1;}
