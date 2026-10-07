#include "SCIO.H"
/* 只用外部管理员已经拥有的公开票据路径启动原样GUI，不给应用
 * 注入身份/测试状态。窗口和会话都从分页实际找到，不能猜句柄。 */
static void number(const char *label,u32 value)
{cli_text(1,label);cli_number(1,(int)value);cli_text(1,"\n");}
int main(void)
{
    if(cli_parse()<0 || cli_argc!=2)return 2;
    u32 identity[8],page[528],info[8],began=(u32)sc_tick(),original;
    if(sc_auth_info(identity)<0 || identity[4]!=1 || sc_session_page(page,528,0xFFFFFFFFu)<0)return 1;
    original=page[6];
    if(sc_permissions(cli_argv[0],-2,-2,63)<0 || sc_permissions(cli_argv[1],-2,-2,63)<0)return 1;
    int ticket;
    do{ticket=sc_auth_login("mio","");if(ticket!=-6)break;sc_event_wait(SC_EVENT_AUTH,10);}
    while((u32)sc_tick()-began<2000u);
    if(ticket<1)return cli_error("GUI login",ticket);
    for(;;){int state=sc_auth_status((u32)ticket,info);if(!state)break;
        if(state!=1 || (u32)sc_tick()-began>=2000u){sc_auth_cancel((u32)ticket);return 1;}
        sc_event_wait(SC_EVENT_AUTH,10);}
    char command[160];copy(command,cli_argv[0],sizeof(command));append(command," ",sizeof(command));append(command,cli_argv[1],sizeof(command));
    int pid=sc_auth_exec((u32)ticket,command,0);sc_auth_cancel((u32)ticket);
    if(pid<1)return cli_error("GUI child",pid);
    u32 window=0,session=0;began=(u32)sc_tick();
    do{u32 cursor=0xFFFFFFFFu;
        do{if(sc_window_page(page,528,cursor)<0)goto fail;
            for(u32 i=0;i<page[3];i++){u32 *row=page+16+i*16;
                if(row[1]==(u32)pid){window=row[0];session=row[3];}}
            cursor=page[4];}while(!window && cursor!=0xFFFFFFFFu);
        if(window)break;sc_event_wait(SC_EVENT_JOB|SC_EVENT_VISIBILITY,10);
    }while((u32)sc_tick()-began<2000u);
    if(!window || !session || sc_session_control(session,0)<0)goto fail;
    number("gui_pid=",(u32)pid);number("gui_window=",window);
    number("gui_session=",session);number("gui_original=",original);return 0;
fail:
    sc_kill2(pid);return 1;
}
