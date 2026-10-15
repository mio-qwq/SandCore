#include <SCSHELL.H>
#include <stdio.h>
#include <string.h>

/* 只使用公开对象观察可信host和真实IPC；不写内核状态、不代发桌面票。 */
static ScShell *client;
static ScShellRequest *request;
static ScShellCatalog *catalog;
static ScMonotonicClock begin;

static void record(void *value,ScU32 bytes)
{
    memset(value,0,bytes);
    ((ScU32 *)value)[0]=bytes;
    ((ScU32 *)value)[1]=1;
}
static ScStatus deadline(void)
{
    ScMonotonicClock now;
    record(&now,sizeof(now));
    ScStatus status=ScSystemMonotonic(&now);
    if(status<0)return status;
    if(now.ticks.hi!=begin.ticks.hi || now.ticks.lo-begin.ticks.lo>15u*begin.hz)return SC32_E_FAILED;
    return 0;
}
static ScStatus host_ready(ScShellHostInfo *host,ScU32 drawn)
{
    ScU32 previous=0xffffffffu;
    for(;;){
        record(host,sizeof(*host));
        ScStatus status=ScShellHostQuery(host);
        if(status<0)return status;
        if(host->state!=previous){
            printf("SHELLPROBE HOST state=%u sid=%u pid=%u/%u epoch=%u\n",
                host->state,host->session,host->pid,host->generation,host->host_epoch);
            fflush(0);previous=host->state;
        }
        if(host->state==SC32_SHELL_ACTIVE || (!drawn && host->state==SC32_SHELL_PREPARED))return 0;
        if(host->state==SC32_SHELL_RETRY_EXHAUSTED)return SC32_E_FAILED;
        status=deadline();if(status<0)return status;
        ScU32 events=0;
        status=ScSystemWait(SC32_EVENT_VISIBILITY|SC32_EVENT_STREAM,2,&events);
        if(status<0)return status;
    }
}
static ScStatus connected(ScShellInfo *info)
{
    for(;;){
        record(info,sizeof(*info));
        ScStatus status=ScShellQuery(client,info);
        if(status<0)return status;
        if(info->state==SC_SHELL_CONNECTED)return 0;
        if(info->state==SC_SHELL_DISCONNECTED)return SC32_E_BROKEN_PIPE;
        status=deadline();if(status<0)return status;
        status=ScShellStep(client,2);if(status<0)return status;
    }
}
static ScStatus probe(void)
{
    ScIdentityInfo identity;
    record(&identity,sizeof(identity));
    ScStatus status=ScIdentityQuery(&identity);
    if(status<0)return status;
    if(identity.uid!=1000 || identity.realm!=SC32_BROKER_REALM_NORMAL || identity.flags)return SC32_E_ACCESS;
    record(&begin,sizeof(begin));
    status=ScSystemMonotonic(&begin);if(status<0)return status;
    ScShellHostInfo host;
    status=host_ready(&host,0);if(status<0)return status;
    ScU32 first_pid=host.pid,first_generation=host.generation,first_epoch=host.host_epoch;
    if(!host.pid || !host.generation || !host.endpoint || !host.desktop || !host.taskbar)return SC32_E_FAILED;
    ScShellHostInfo rejected;
    record(&rejected,sizeof(rejected));
    status=ScShellHostActivate(host.host_epoch,&rejected);
    if(status!=SC32_E_ACCESS)return status<0?status:SC32_E_FAILED;
    printf("SHELLPROBE NONHOST_ACTIVATE_REJECTED status=%d\n",status);
    ScShellConnectDesc connect;
    record(&connect,sizeof(connect));connect.expected_host_epoch=host.host_epoch;
    status=ScShellConnect(&connect,&client);if(status<0)return status;
    ScShellInfo info;
    status=connected(&info);if(status<0)return status;
    if(info.host.pid!=host.pid || info.host.generation!=host.generation
       || info.host.session!=host.session || info.peer.pid!=host.pid
       || info.peer.generation!=host.generation || info.peer.session!=host.session)return SC32_E_FAILED;
    printf("SHELLPROBE HELLO actual_peer=%u/%u sid=%u epoch=%u\n",
        info.peer.pid,info.peer.generation,info.peer.session,info.epoch);
    status=ScShellQueryCatalog(client,info.epoch,&request);if(status<0)return status;
    for(;;){
        ScShellRequestInfo result;
        record(&result,sizeof(result));status=ScShellRequestQuery(request,&result);
        if(status<0)return status;
        if(result.state==SC_SHELL_REQUEST_COMPLETE){if(result.status<0)return result.status;break;}
        if(result.state>=SC_SHELL_REQUEST_UNKNOWN)return SC32_E_FAILED;
        status=deadline();if(status<0)return status;
        status=ScShellStep(client,2);if(status<0)return status;
    }
    status=ScShellRequestCatalog(request,&catalog);if(status<0)return status;
    ScShellCatalogInfo contents;
    record(&contents,sizeof(contents));status=ScShellCatalogQuery(catalog,&contents);
    if(status<0)return status;
    if(!contents.epoch || !contents.applications)return SC32_E_FAILED;
    printf("SHELLPROBE CATALOG applications=%u associations=%u layout=%u epoch=%u\n",
        contents.applications,contents.associations,contents.layout_items,contents.epoch);
    printf("SHELLPROBE HIDDEN_CATALOG_READY host_state=%u\n",host.state);fflush(0);
    /* 先在真实隐藏会话中服务IPC，再由本人公开控制请求切换显示。
     * 这段200PIT观察不把时钟当FPS，也不借System改内核显示状态。 */
    ScMonotonicClock hidden;record(&hidden,sizeof(hidden));
    status=ScSystemMonotonic(&hidden);if(status<0)return status;
    for(;;){
        ScMonotonicClock now;record(&now,sizeof(now));status=ScSystemMonotonic(&now);if(status<0)return status;
        if(now.ticks.hi!=hidden.ticks.hi || now.ticks.lo-hidden.ticks.lo>=200u)break;
        ScU32 events=0;status=ScSystemWait(SC32_EVENT_AUTH,10,&events);if(status<0)return status;
    }
    status=ScSessionControl(identity.session,SC32_SESSION_SHOW);if(status<0)return status;
    status=host_ready(&host,1);if(status<0)return status;
    if(host.pid!=first_pid || host.generation!=first_generation || host.host_epoch!=first_epoch)return SC32_E_STALE;
    printf("SHELLPROBE VISIBLE_ACTIVE sid=%u pid=%u/%u epoch=%u\n",host.session,host.pid,host.generation,host.host_epoch);
    return 0;
}
int main(void)
{
    ScStatus status=probe();
    ScShellCatalogRelease(catalog);
    ScShellRequestClose(request);
    ScShellClose(client);
    if(status<0){printf("SHELLPROBE FAIL status=%d\n",status);return 1;}
    printf("SHELLPROBE PASS ordinary_native_host_and_real_catalog_IPC\n");
    return 0;
}
