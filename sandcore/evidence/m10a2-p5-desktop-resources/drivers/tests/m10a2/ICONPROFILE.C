#include <SCSHELL.H>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 只量本人冷资源cache的实际图标装载；没有窗口/合成/暂停观察，
 * 不把这个分项耗时冒充整机或完整桌面启动时长。 */
typedef struct {char *spec,*target;} IconRow;
static IconRow *items;
static ScU32 count,capacity;
static ScDirectory directory;
static ScUiContext *ui;
static ScJob child_job;
static ScShell *shell;
static ScLibrary measured_library;
static ScStatus (*profile_icon_load)(ScUiContext *,const char *,const char *,ScUiIcon **);
static ScStatus (*profile_cache_query)(ScCacheInfo *);
static ScStatus (*profile_shortcut_read)(const char *,ScShellShortcut **);
static ScStatus (*profile_shortcut_describe)(const ScShellShortcut *,ScShellShortcutDesc *);
static void (*profile_shortcut_release)(ScShellShortcut *);
#define ScShellIconLoad profile_icon_load
#define ScShellIconCacheQuery profile_cache_query
#define ScShellShortcutRead profile_shortcut_read
#define ScShellShortcutDescribe profile_shortcut_describe
#define ScShellShortcutRelease profile_shortcut_release
static void record(void *p,ScU32 bytes){memset(p,0,bytes);((ScU32 *)p)[0]=bytes;((ScU32 *)p)[1]=1;}
static char *copy(const char *text)
{if(!text)text="";ScU32 bytes=(ScU32)strlen(text)+1;char *p=malloc(bytes);if(p)memcpy(p,text,bytes);return p;}
static ScStatus collect(void)
{
    ScStatus status=ScDirectoryOpen("/DESK",&directory);if(status<0)return status;
    ScDirectoryPage page;ScDirectoryEntry rows[16];
    do{
        record(&page,sizeof(page));status=ScDirectoryNext(directory,rows,16,&page);if(status<0)return status;
        for(ScU32 i=0;i<page.count;i++){
            ScU32 bytes=(ScU32)strlen(rows[i].name);if(bytes<4 || rows[i].kind!=SC32_PATH_FILE)continue;
            const char *tail=rows[i].name+bytes-4;
            if(strcmp(tail,".lnk") && strcmp(tail,".LNK"))continue;
            char path[68];int size=snprintf(path,sizeof(path),"/DESK/%s",rows[i].name);
            if(size<0 || (ScU32)size>=sizeof(path))return SC32_E_RANGE;
            ScShellShortcut *shortcut=0;status=ScShellShortcutRead(path,&shortcut);if(status<0)return status;
            ScShellShortcutDesc desc;record(&desc,sizeof(desc));status=ScShellShortcutDescribe(shortcut,&desc);
            if(status>=0 && count==capacity){
                ScU32 next=capacity?capacity*2:16;
                if(next<capacity || next>0xffffffffu/(ScU32)sizeof(*items))status=SC32_E_RANGE;
                else{IconRow *candidate=realloc(items,next*sizeof(*items));
                    if(!candidate)status=SC32_E_MEMORY;else{items=candidate;capacity=next;}}
            }
            if(status>=0){IconRow row;row.spec=copy(desc.icon);row.target=copy(desc.target);
                if(!row.spec || !row.target){free(row.spec);free(row.target);status=SC32_E_MEMORY;}
                else items[count++]=row;
            }
            ScShellShortcutRelease(shortcut);if(status<0)return status;
        }
    }while(!(page.flags&SC32_DIRECTORY_FINISHED));
    status=ScDirectoryClose(directory);directory=0;return status;
}
static ScStatus profile(ScU32 baseline)
{
    ScIdentityInfo self;record(&self,sizeof(self));ScStatus status=ScIdentityQuery(&self);
    if(status<0)return status;if(self.uid!=1000 || self.realm || self.flags)return SC32_E_ACCESS;
    /* 仅库名元数据重命名，生产机器码/重定位/数据原字节不变；依赖
     * 仍用本进程同一套原生SCUI/SC32。新进程保证两次独立冷cache。 */
    ScLibraryDesc load;record(&load,sizeof(load));load.name=baseline?"BASE.SCL":"FAST.SCL";load.abi_major=1;load.min_version=1;
    status=ScLoadLibrary(&load,&measured_library);if(status<0)return status;
    status=ScGetSymbol(measured_library,"ScShellIconLoad",SC32_SYMBOL_FUNCTION,1,(void **)&profile_icon_load);if(status<0)return status;
    status=ScGetSymbol(measured_library,"ScShellIconCacheQuery",SC32_SYMBOL_FUNCTION,1,(void **)&profile_cache_query);if(status<0)return status;
    status=ScGetSymbol(measured_library,"ScShellShortcutRead",SC32_SYMBOL_FUNCTION,1,(void **)&profile_shortcut_read);if(status<0)return status;
    status=ScGetSymbol(measured_library,"ScShellShortcutDescribe",SC32_SYMBOL_FUNCTION,1,(void **)&profile_shortcut_describe);if(status<0)return status;
    status=ScGetSymbol(measured_library,"ScShellShortcutRelease",SC32_SYMBOL_FUNCTION,1,(void **)&profile_shortcut_release);if(status<0)return status;
    status=collect();if(status<0)return status;if(!count)return SC32_E_NOT_FOUND;
    ScUiContextDesc context;record(&context,sizeof(context));context.cache_budget_bytes=262144;context.scheduling_quantum=64;
    status=ScUiContextCreate(&context,&ui);if(status<0)return status;
    ScProcessRef self_ref;record(&self_ref,sizeof(self_ref));status=ScProcessSelf(&self_ref);if(status<0)return status;
    ScProcessInfo cpu_begin,cpu_end;record(&cpu_begin,sizeof(cpu_begin));record(&cpu_end,sizeof(cpu_end));
    status=ScProcessQuery(&self_ref,&cpu_begin);if(status<0)return status;
    ScMonotonicClock begin,end;record(&begin,sizeof(begin));record(&end,sizeof(end));
    printf("ICONPROFILE BEGIN\n");fflush(0);
    status=ScSystemMonotonic(&begin);if(status<0)return status;
    ScU32 pixels=0;
    for(ScU32 i=0;i<count;i++){
        ScUiIcon *icon=0;status=ScShellIconLoad(ui,items[i].spec,items[i].target,&icon);if(status<0)return status;
        ScUiIconInfo info;record(&info,sizeof(info));status=ScUiIconQuery(icon,&info);
        ScUiIconRelease(icon);if(status<0)return status;
        if(!info.width || !info.height || info.width>128 || info.height>128 || info.bytes!=info.width*info.height*4)return SC32_E_FAILED;
        pixels+=info.bytes;
    }
    status=ScSystemMonotonic(&end);if(status<0)return status;
    status=ScProcessQuery(&self_ref,&cpu_end);if(status<0)return status;
    ScCacheInfo cache;record(&cache,sizeof(cache));status=ScShellIconCacheQuery(&cache);if(status<0)return status;
    if(cache.misses.hi || cache.misses.lo!=count || cache.hits.hi || cache.hits.lo)return SC32_E_FAILED;
    printf("ICONPROFILE RESULT icons=%u ticks=%u hz=%u pixels_bytes=%u entries=%u misses=%u hits=%u samples=%u dispatches=%u\n",
        count,end.ticks.lo-begin.ticks.lo,begin.hz,pixels,cache.entries,cache.misses.lo,cache.hits.lo,
        cpu_end.running_samples-cpu_begin.running_samples,cpu_end.dispatches-cpu_begin.dispatches);fflush(0);
    return 0;
}
static ScStatus expired(ScMonotonicClock *begin)
{
    ScMonotonicClock now;record(&now,sizeof(now));ScStatus status=ScSystemMonotonic(&now);
    if(status<0)return status;
    if(now.ticks.hi!=begin->ticks.hi || now.ticks.lo-begin->ticks.lo>120u*begin->hz)return SC32_E_FAILED;
    return 0;
}
static ScStatus measure_child(ScU32 baseline)
{
    ScStatus status=ScProcessSpawn(baseline?"/HOME/M10SHELL/GENERATED/ICONPROFILE.SCX base":"/HOME/M10SHELL/GENERATED/ICONPROFILE.SCX fast",0,0,&child_job);
    if(status<0)return status;
    ScJobInfo result;record(&result,sizeof(result));status=ScJobWait(child_job,SC32_INFINITE,&result);
    if(status<0)return status;child_job=0;
    if(result.state!=SC32_JOB_FINISHED || result.exit_code || !result.original_pid || !result.child_generation)return SC32_E_FAILED;
    printf("ICONPROFILE CHILD pid=%u/%u exit=%d\n",result.original_pid,result.child_generation,result.exit_code);fflush(0);return 0;
}
static ScStatus controller(void)
{
    ScIdentityInfo identity;record(&identity,sizeof(identity));ScStatus status=ScIdentityQuery(&identity);
    if(status<0)return status;if(identity.uid!=1000 || identity.realm || identity.flags)return SC32_E_ACCESS;
    ScMonotonicClock begin;record(&begin,sizeof(begin));status=ScSystemMonotonic(&begin);if(status<0)return status;
    ScShellHostInfo host;
    for(;;){
        record(&host,sizeof(host));status=ScShellHostQuery(&host);if(status<0)return status;
        if(host.state==SC32_SHELL_PREPARED)break;
        if(host.state==SC32_SHELL_ACTIVE || host.state==SC32_SHELL_RETRY_EXHAUSTED)return SC32_E_FAILED;
        status=expired(&begin);if(status<0)return status;
        ScU32 events=0;status=ScSystemWait(SC32_EVENT_VISIBILITY|SC32_EVENT_STREAM,2,&events);if(status<0)return status;
    }
    ScShellConnectDesc connect;record(&connect,sizeof(connect));connect.expected_host_epoch=host.host_epoch;
    status=ScShellConnect(&connect,&shell);if(status<0)return status;
    for(;;){
        ScShellInfo info;record(&info,sizeof(info));status=ScShellQuery(shell,&info);if(status<0)return status;
        if(info.state==SC_SHELL_CONNECTED){
            if(info.peer.pid!=host.pid || info.peer.generation!=host.generation)return SC32_E_FAILED;break;
        }
        if(info.state==SC_SHELL_DISCONNECTED)return SC32_E_BROKEN_PIPE;
        status=expired(&begin);if(status<0)return status;status=ScShellStep(shell,2);if(status<0)return status;
    }
    ScShellClose(shell);shell=0;
    printf("ICONPROFILE CONTROLLER sid=%u host=%u/%u epoch=%u\n",
        identity.session,host.pid,host.generation,host.host_epoch);fflush(0);
    status=measure_child(1);if(status<0)return status;
    printf("ICONPROFILE BASELINE_DONE\n");fflush(0);
    status=measure_child(0);if(status<0)return status;
    printf("ICONPROFILE COMPARISON_DONE sid=%u\n",identity.session);return 0;
}
int main(void)
{
    char arguments[32];ScU32 bytes=0;
    ScStatus status=ScProcessArguments(arguments,sizeof(arguments),&bytes);if(status<0)return 2;
    ScU32 controlled=!strcmp(arguments,"control");
    status=controlled?controller():profile(!strcmp(arguments,"base"));
    if(directory)ScDirectoryClose(directory);
    ScUiContextRelease(ui);
    for(ScU32 i=0;i<count;i++){free(items[i].spec);free(items[i].target);}free(items);
    ScShellClose(shell);if(child_job)ScJobClose(child_job);
    if(measured_library)ScFreeLibrary(measured_library);
    if(status<0){printf("ICONPROFILE FAIL status=%d\n",status);return 1;}
    printf(controlled?"ICONPROFILE PASS same_vm_cold_resource_comparison\n":"ICONPROFILE PASS actual_native_cold_icon_load\n");return 0;
}
