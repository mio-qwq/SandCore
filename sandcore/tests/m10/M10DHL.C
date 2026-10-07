#include "SCIO.H"
#include "SCNET.H"
/* 真实对端提供长/无限租约；不改时钟或内核计数。外部调试器只读
 * 实际timer字段与32位布局，公开快照继续承担地址与生命周期检查。 */
static int marker(const char *label)
{
    cli_text(1,"M10DHL_STAGE ");cli_text(1,label);cli_text(1,"\n");
    char reply;return cli_read(0,&reply,1)==1 && reply=='\n'?0:-1;
}
static int bound(u32 *info,u32 ticks)
{
    u32 began=(u32)sc_tick();
    do{
        if(sc_net_info(info)<0)return -1;
        if(info[12]==10 && info[4]==0x0A170002u && info[5]==0xFFFFFF00u
            && info[6]==0x0A170001u)return 0;
        sc_event_wait(0,10);
    }while((u32)sc_tick()-began<ticks);
    return -1;
}
int main(void)
{
    if(cli_parse()<0 || cli_argc!=1)return 2;
    int transition=equal(cli_argv[0],"transition");
    u32 control[32],info[96];sc_net_request(control);
    if(sc_net_control(SC_NET_DHCP_RELEASE,control)<0
        || sc_net_control(SC_NET_DHCP_START,control)<0 || bound(info,2000u)<0
        || (transition && info[14]!=60u) || marker("bound")<0)return 1;
    if(transition){
        u32 began=(u32)sc_tick();
        do{
            if(sc_net_info(info)<0 || !info[4])return 1;
            if(info[12]==10 && info[14]==0xFFFFFFFFu)break;
            sc_event_wait(0,10);
        }while((u32)sc_tick()-began<3500u);
        if(info[12]!=10 || info[14]!=0xFFFFFFFFu || marker("infinite")<0)return 1;
    }
    u32 began=(u32)sc_tick(),duration=transition?6500u:400u;
    do{
        if(sc_net_info(info)<0 || info[12]!=10 || info[4]!=0x0A170002u
            || info[5]!=0xFFFFFF00u || info[6]!=0x0A170001u)return 1;
        if(transition && info[14]!=0xFFFFFFFFu)return 1;
        sc_event_wait(0,20);
    }while((u32)sc_tick()-began<duration);
    cli_text(1,"dhcp_hold_ticks=");cli_number(1,(int)((u32)sc_tick()-began));
    cli_text(1," dhcp_long_failures=0\n");return 0;
}
