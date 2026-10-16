#include "SOUND2.H"
#include "../systemtools/tools_ui.inc"
#include "sound_jobs.c"
#include "sound_ui.c"
static ScStatus sound_arguments(void)
{
    /* Shell的现代文档参数总是带引号；不能把整个原始命令尾部当路径，
     * 否则文件关联与带空格的名字会只在手动对话框打开时才工作。 */
    char raw[SC32_PATH_CAPACITY*2+4],name[SC32_PATH_CAPACITY],path[SC32_PATH_CAPACITY];
    ScU32 bytes=0;ScStatus status=ScProcessArguments(raw,sizeof(raw),&bytes);if(status<0)return status;
    if(bytes>=sizeof(raw) || strlen(raw)!=bytes)return SC32_E_INVALID;
    const char *p=raw;while(*p==' ' || *p=='\t')p++;if(!*p)return 0;
    ScU32 quoted=*p=='"',used=0;if(quoted)p++;
    while(*p && (quoted?*p!='"':*p!=' ' && *p!='\t')){
        char c=*p++;if(quoted && c=='\\'){
            if(*p!='\\' && *p!='"')return SC32_E_INVALID;
            c=*p++;
        }
        if(used+1>=sizeof(name))return SC32_E_RANGE;
        name[used++]=c;
    }
    if(quoted){if(*p!='"')return SC32_E_INVALID;p++;}
    while(*p==' ' || *p=='\t')p++;
    if(*p || !used)return SC32_E_INVALID;
    name[used]=0;
    status=ScPathResolve(name,path,sizeof(path),&bytes);
    return status<0?status:sound_begin(path,0,0);
}
int main(void)
{
    sound_app.volume=224;sound_app.last_position=sound_app.last_total=0xffffffffu;
    sound_app.identity.size=sizeof(sound_app.identity);sound_app.identity.version=1;
    sound_app.self.size=sizeof(sound_app.self);sound_app.self.version=1;
    ScStatus status=ScIdentityQuery(&sound_app.identity);
    if(!status)status=ScProcessSelf(&sound_app.self);
    if(!status && (sound_app.identity.uid<0 || sound_app.identity.realm!=SC32_BROKER_REALM_NORMAL))status=SC32_E_ACCESS;
    if(!status)status=sound_ui();
    if(status<0){tool_close();return 1;}
    status=sound_arguments();if(status<0)tool_error(status,"打开音频");
    ScU32 events=SC32_EVENT_IMAGE;
    for(;;){
        ScStatus ui=ScUiContextStep(tool.ui,0);if(ui<0){tool.quit=1;status=ui;}
        int more=sound_jobs_step();
        if(tool.quit)sound_image_cancel();
        if(sound_app.image_more || (sound_app.image_wait&events) || !sound_app.image){
            ScStatus cover=sound_cover_step();if(cover<0)tool_status("封面无法显示，音频播放继续。");}
        if(tool.quit && !sound_app.current && !sound_app.candidate && !sound_app.retiring && !sound_app.image)break;
        ScU32 timeout=SC32_INFINITE;
        if(more || sound_app.image_more)timeout=0;
        if(sound_app.current && sound_app.current->fallback==1 && !sound_app.current->inflight)timeout=0;
        if(sound_app.candidate && sound_app.candidate->phase==S2_CONNECTING && timeout>25)timeout=25;
        if(tool.quit && timeout>25)timeout=25;
        ScUiContextWaitTimeout(tool.ui,timeout,&timeout);
        if(!timeout){ScProcessYield();events=0;continue;}
        ScBrokerWaitDesc wait;memset(&wait,0,sizeof(wait));wait.size=sizeof(wait);wait.version=1;
        wait.event_mask=SC32_EVENT_INPUT|SC32_EVENT_VISIBILITY|SC32_EVENT_STREAM|SC32_EVENT_JOB|sound_app.image_wait;
        wait.timeout=timeout;events=0;
        ScStatus slept=ScBrokerWait(&wait,&events);if(slept<0){status=slept;tool.quit=1;}
    }
    ScUiDialogRelease(sound_app.dialog);ScGfxCanvasRelease(sound_app.canvas);
    ScGfxSurfaceRelease(sound_app.cover);tool_close();
    return status<0?1:0;
}
