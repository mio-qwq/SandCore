#include "SCIO.H"
#include "SCNET.H"
/* 只用公开管理/快照和真实PIT，不改内核时钟、不主动RENEW来冒充
 * 自动续期。宿主在BOUND/EXPIRED标记后改变隔离线缆应答策略。 */
static int stage(const char *label)
{
    cli_text(1,"M10DHCP_STAGE ");cli_text(1,label);cli_text(1,"\n");
    char answer;return cli_read(0,&answer,1)==1 && answer=='\n'?0:-1;
}
static int bound(u32 *info,u32 ticks)
{
    u32 began=(u32)sc_tick();
    do{if(sc_net_info(info)<0)return -1;
        if(info[12]==10 && info[4]==0x0A170002u && info[5]==0xFFFFFF00u
            && info[6]==0x0A170001u)return 0;
        sc_event_wait(SC_EVENT_NETWORK,1);
    }while((u32)sc_tick()-began<ticks);
    return -1;
}
int main(void)
{
    if(cli_parse()<0 || cli_argc!=1)return 2;
    int renew=equal(cli_argv[0],"renew"),rebind=equal(cli_argv[0],"rebind"),expire=equal(cli_argv[0],"expire");
    if(!renew && !rebind && !expire)return 2;
    u32 control[32],info[96];sc_net_request(control);
    if(sc_net_control(SC_NET_DHCP_RELEASE,control)<0
        || sc_net_control(SC_NET_DHCP_START,control)<0 || bound(info,2000u)<0
        || info[14]!=60u || stage("bound")<0)return 1;
    u32 began=(u32)sc_tick(),peak=0,limit=renew?3500u:rebind?5000u:7000u;
    int renewed=0,expired=0,saw_renewing=0;
    do{
        if(sc_net_info(info)<0)return 1;
        if(info[12]==5)saw_renewing=1;
        if(info[15]>peak)peak=info[15];
        if(!info[4]){expired=1;break;}
        if(info[12]==10 && peak>=(rebind?25u:10u) && info[15]<peak){renewed=1;break;}
        sc_event_wait(SC_EVENT_NETWORK,1);
    }while((u32)sc_tick()-began<limit);
    cli_text(1,"dhcp_elapsed_ticks=");cli_number(1,(int)((u32)sc_tick()-began));
    cli_text(1," peak_lease_seconds=");cli_number(1,(int)peak);cli_text(1,"\n");
    if(expire){
        if(!expired || info[4] || info[5] || info[6] || info[12]==10 || !(info[2]&2u))return 1;
        cli_text(1,"PASS expired lease withdraws address mask and default gateway while interface stays up\n");
        if(stage("expired")<0 || bound(info,2000u)<0 || info[14]!=60u)return 1;
        cli_text(1,"PASS server recovery automatically obtains a fresh lease without reset or explicit renewal\n");
    }else{
        if(expired || !renewed || (rebind && !saw_renewing))return 1;
        cli_text(1,rebind?"PASS unanswered renewal reaches automatic rebind before address expiry\n":
                          "PASS short lease renews automatically before address expiry\n");
    }
    cli_text(1,"dhcp_lifecycle_failures=0\n");return 0;
}
