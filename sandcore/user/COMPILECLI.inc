#include "SCIO.H"
/* 纯CLI编译界面只打印真实阶段，不创建窗口。断开的管道不能回退
 * PUTS把诊断偷偷写到桌面；登记已有终端的取消事件，不取得UART。 */
static int build_view_cancel;
static char build_view_phase[96];
static int build_cli_terminal=-1,build_cli_event=-1;
static void build_cli_text(const char *text)
{
    if(cli_text(1,text)<0)build_view_cancel=1;
}
static void build_view_begin(const char *title,const char *source,const char *target)
{
    build_cli_terminal=sc_stream_open("",8,0);
    if(build_cli_terminal>=0)build_cli_event=sc_stream_event(build_cli_terminal,1);
    build_cli_text(title);build_cli_text("\n");build_cli_text(source);build_cli_text(" -> ");build_cli_text(target);build_cli_text("\n");
}
static int build_view_poll(const char *phase,int done,int total,int force)
{
    (void)done;(void)total;(void)force;
    if(build_cli_event>=0 && sc_stream_event(build_cli_terminal,0)!=build_cli_event)build_view_cancel=1;
    if(!equal(build_view_phase,phase)){copy(build_view_phase,phase,sizeof(build_view_phase));build_cli_text(phase);build_cli_text("\n");}
    return build_view_cancel;
}
static void build_view_result(const char *message,int failed)
{if(cli_text(failed?2:1,message)<0 || cli_text(failed?2:1,"\n")<0)build_view_cancel=1;sc_audio_effect(failed?3:4);}
static void build_view_wait(void){}
/* 旧图形外壳仍用PUTS；CLI的所有阶段/诊断走可重定向字节流。 */
#define sc_puts(text) build_cli_text(text)
