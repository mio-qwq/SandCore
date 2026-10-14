#include "SC32PRIVATE.H"

ScStatus ScIdentityQuery(ScIdentityInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScIdentityInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_IDENTITY);
    ScU32 raw[8];ScI32 result=sc32_call(0x200,(ScI32)raw,0,0,0,0,0);if(result<0)return sc32_legacy(result,SC32_OP_IDENTITY);
    ScProcessRef request={16,1,0,0};ScProcessInfo process;sc32_zero(&process,sizeof(process));process.size=sizeof(process);process.version=1;
    result=sc32_call(SC32_WIRE_GATE,SC32_WIRE_PROCESS,(ScI32)&request,(ScI32)&process,0,0,0);if(result<0)return sc32_native(result,SC32_OP_IDENTITY);
    ScIdentityInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.uid=(ScI32)raw[1];value.gid=(ScI32)raw[2];value.realm=raw[3];value.task_generation=raw[5];value.session=process.session;
    if(value.uid==0 || raw[4])value.flags|=SC32_IDENTITY_MANAGE;
    if(raw[4])value.flags|=SC32_IDENTITY_MODULES;
    sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScSessionEnumerate(ScSessionPage *page,ScSessionInfo *rows,ScU32 capacity)
{
    ScStatus checked=sc32_record_out(page,sizeof(ScSessionPage));if(checked<0)return sc32_invalid(checked,SC32_OP_SESSION_PAGE);
    if(!capacity || capacity>64)return sc32_invalid(SC32_E_RANGE,SC32_OP_SESSION_PAGE);
    checked=sc32_out(rows,capacity*sizeof(ScSessionInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_SESSION_PAGE);
    if(sc32_overlap(page,page->size,rows,capacity*sizeof(ScSessionInfo)))return sc32_invalid(SC32_E_INVALID,SC32_OP_SESSION_PAGE);
    ScU32 raw[1040];ScI32 status=sc32_call(0x242,(ScI32)raw,(ScI32)(16+capacity*16),(ScI32)page->cursor,0,0,0);
    if(status<0)return sc32_legacy(status,SC32_OP_SESSION_PAGE);
    ScU32 count=raw[3];if(count>capacity)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,status,SC32_OP_SESSION_PAGE);
    for(ScU32 i=0;i<count;i++){
        const ScU32 *r=raw+16+i*16;ScSessionInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
        value.id=r[0];value.uid=(ScI32)r[1];value.gid=(ScI32)r[2];value.kind=r[3];value.visible=r[4];
        value.closing=r[5];value.members=r[6];value.generation=r[7];sc32_copy(value.name,r+8,32);
        sc32_copy(rows+i,&value,sizeof(value));
    }
    ScSessionPage value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.cursor=raw[4];value.count=count;value.current_session=raw[5];value.visible_session=raw[6];sc32_copy(page,&value,sizeof(value));return 0;
}
ScStatus ScSessionControl(ScU32 session,ScU32 action)
{
    if(action>2)return sc32_invalid(SC32_E_INVALID,SC32_OP_SESSION_CONTROL);
    ScI32 raw=sc32_call(0x243,(ScI32)session,(ScI32)action,0,0,0,0);
    return raw==-4?sc32_fail(SC32_E_MEMORY,SC32_DOMAIN_LEGACY,raw,SC32_OP_SESSION_CONTROL):sc32_legacy(raw,SC32_OP_SESSION_CONTROL);
}
ScStatus ScSessionPath(const char *leaf,char *out,ScU32 capacity,ScU32 *length)
{
    ScStatus checked=sc32_text_buffers(out,capacity,length);if(checked<0)return sc32_invalid(checked,SC32_OP_SESSION_PATH);
    checked=sc32_string(leaf,63,0);if(checked<0)return sc32_invalid(checked,SC32_OP_SESSION_PATH);
    char relative[64],absolute[65];ScI32 raw=sc32_call(0x246,(ScI32)leaf,(ScI32)relative,64,0,0,0);
    if(raw<0)return sc32_legacy(raw,SC32_OP_SESSION_PATH);
    if(raw>63)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,raw,SC32_OP_SESSION_PATH);
    absolute[0]='/';sc32_copy(absolute+1,relative,(ScU32)raw+1);
    return sc32_text_output(absolute,(ScU32)raw+1,out,capacity,length,SC32_OP_SESSION_PATH);
}
ScStatus ScThemeQuery(ScThemeInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScThemeInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_THEME);
    ScU32 raw[32];ScI32 result=sc32_call(0x2c,(ScI32)raw,0,0,0,0,0);if(result<0)return sc32_legacy(result,SC32_OP_THEME);
    ScThemeInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.style=raw[1];value.generation=raw[2];value.title_height=raw[3];value.taskbar_height=raw[4];value.radius=raw[5];value.border=raw[6];
    for(ScU32 i=0;i<24;i++)value.colors[i]=raw[8+i];
    sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScThemeApply(const char *path,ScU32 action)
{
    if(action>2)return sc32_invalid(SC32_E_INVALID,SC32_OP_THEME_APPLY);
    char absolute[65];if(action!=SC32_THEME_RELOAD){ScStatus checked=sc32_absolute(path,absolute,SC32_OP_THEME_APPLY);if(checked<0)return checked;}
    ScI32 raw=sc32_call(0x2d,action==SC32_THEME_RELOAD?0:(ScI32)absolute,(ScI32)action,0,0,0,0);
    /* 主题-2明确为读取、-3明确为保存；它们不是通用FS的混合-1。 */
    if(raw==-2 || raw==-3)return sc32_fail(SC32_E_IO,SC32_DOMAIN_LEGACY,raw,SC32_OP_THEME_APPLY);
    if(raw==-4)return sc32_fail(SC32_E_MEMORY,SC32_DOMAIN_LEGACY,raw,SC32_OP_THEME_APPLY);
    return sc32_legacy(raw,SC32_OP_THEME_APPLY);
}
ScStatus ScThemePath(ScU32 kind,char *out,ScU32 capacity,ScU32 *length)
{
    ScStatus checked=sc32_text_buffers(out,capacity,length);if(checked<0)return sc32_invalid(checked,SC32_OP_THEME_PATH);
    if(kind>2)return sc32_invalid(SC32_E_INVALID,SC32_OP_THEME_PATH);
    char text[64],absolute[65];ScI32 raw=sc32_call(0x2e,(ScI32)kind,(ScI32)text,64,0,0,0);
    if(raw<0)return sc32_legacy(raw,SC32_OP_THEME_PATH);
    if(raw>63)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_LEGACY,raw,SC32_OP_THEME_PATH);
    /* 名称是文本，空资源路径仍为空；真实资源路径统一成根路径。 */
    if(kind!=SC32_THEME_NAME && raw){absolute[0]='/';sc32_copy(absolute+1,text,(ScU32)raw+1);
        return sc32_text_output(absolute,(ScU32)raw+1,out,capacity,length,SC32_OP_THEME_PATH);}
    return sc32_text_output(text,(ScU32)raw,out,capacity,length,SC32_OP_THEME_PATH);
}
ScStatus ScDisplayQuery(ScDisplayInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScDisplayInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_DISPLAY);
    ScU32 raw[8];ScI32 result=sc32_call(0x20,(ScI32)raw,0,0,0,0,0);if(result<0)return sc32_legacy(result,SC32_OP_DISPLAY);
    ScDisplayInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.width=raw[0];value.height=raw[1];value.scale_percent=raw[2];value.backend=raw[3];value.generation=raw[4];
    value.supported_modes=raw[5];value.preview_seconds=raw[6];value.max_width=raw[7];sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScDisplayApply(ScU32 width,ScU32 height,ScU32 scale,ScU32 action)
{
    if(action>2)return sc32_invalid(SC32_E_INVALID,SC32_OP_DISPLAY_APPLY);
    ScI32 raw=sc32_call(0x21,(ScI32)width,(ScI32)height,(ScI32)scale,(ScI32)action,0,0);
    if(raw==-2)return sc32_fail(SC32_E_MEMORY,SC32_DOMAIN_LEGACY,raw,SC32_OP_DISPLAY_APPLY);
    if(raw==-3)return sc32_fail(SC32_E_IO,SC32_DOMAIN_LEGACY,raw,SC32_OP_DISPLAY_APPLY);
    return sc32_legacy(raw,SC32_OP_DISPLAY_APPLY);
}
ScStatus ScLoadLibrary(const ScLibraryDesc *desc,ScLibrary *out)
{
    ScStatus checked=sc32_record_in(desc,sizeof(ScLibraryDesc));if(checked<0)return sc32_invalid(checked,SC32_OP_LOAD_LIBRARY);
    checked=sc32_out(out,4);if(checked<0)return sc32_invalid(checked,SC32_OP_LOAD_LIBRARY);
    if(desc->reserved[0] || desc->reserved[1] || !desc->abi_major)return sc32_invalid(SC32_E_INVALID,SC32_OP_LOAD_LIBRARY);
    checked=sc32_string(desc->name,35,0);if(checked<0)return sc32_invalid(checked,SC32_OP_LOAD_LIBRARY);
    ScRuntime *runtime=ScApplicationRuntime();if(!runtime)return sc32_invalid(SC32_E_NOT_SUPPORTED,SC32_OP_LOAD_LIBRARY);
    ScModule module;ScI32 raw=ScRuntimeOpen(runtime,desc->name,desc->abi_major,desc->min_minor,desc->min_version,&module);
    if(raw)return sc32_runtime_error(raw,SC32_OP_LOAD_LIBRARY);
    /* init属于普通Ring3代码，可能改变本进程映射。回调返回后再次验证
     * 最终out，失败撤销这次外部引用；不能把旧Probe当永久写能力。 */
    checked=sc32_out(out,4);
    if(checked<0){ScRuntimeClose(runtime,module);return sc32_invalid(checked,SC32_OP_LOAD_LIBRARY);}
    *out=module;return 0;
}
ScStatus ScGetSymbol(ScLibrary library,const char *name,ScU32 kind,ScU32 minimum_size,void **out)
{
    ScStatus checked=sc32_out(out,4);if(checked<0)return sc32_invalid(checked,SC32_OP_SYMBOL);
    checked=sc32_string(name,127,0);if(checked<0)return sc32_invalid(checked,SC32_OP_SYMBOL);
    if((kind!=1 && kind!=2) || !minimum_size)return sc32_invalid(SC32_E_INVALID,SC32_OP_SYMBOL);
    ScRuntime *runtime=ScApplicationRuntime();if(!runtime)return sc32_invalid(SC32_E_NOT_SUPPORTED,SC32_OP_SYMBOL);
    void *address;ScI32 raw=ScRuntimeResolve(runtime,library,name,kind,minimum_size,&address);
    if(raw)return sc32_runtime_error(raw,SC32_OP_SYMBOL);
    *out=address;return 0;
}
ScStatus ScFreeLibrary(ScLibrary library)
{
    ScRuntime *runtime=ScApplicationRuntime();if(!runtime)return sc32_invalid(SC32_E_NOT_SUPPORTED,SC32_OP_FREE_LIBRARY);
    return sc32_runtime_error(ScRuntimeClose(runtime,library),SC32_OP_FREE_LIBRARY);
}
ScStatus ScLibraryQuery(ScLibraryInfo *out)
{
    ScStatus checked=sc32_record_out(out,sizeof(ScLibraryInfo));if(checked<0)return sc32_invalid(checked,SC32_OP_LIBRARY_QUERY);
    ScRuntime *runtime=ScApplicationRuntime();if(!runtime)return sc32_invalid(SC32_E_NOT_SUPPORTED,SC32_OP_LIBRARY_QUERY);
    ScRuntimeStats stats;sc32_zero(&stats,sizeof(stats));stats.size=sizeof(stats);ScI32 raw=ScRuntimeQuery(runtime,&stats);
    if(raw)return sc32_runtime_error(raw,SC32_OP_LIBRARY_QUERY);
    ScLibraryInfo value;sc32_zero(&value,sizeof(value));value.size=sizeof(value);value.version=1;
    value.modules=stats.modules;value.image_bytes=stats.image_bytes;value.metadata_bytes=stats.metadata_bytes;value.pinned_modules=stats.pinned_modules;
    value.loads=stats.loads;value.reuses=stats.reuses;value.relocations=stats.relocations;sc32_copy(out,&value,sizeof(value));return 0;
}
ScStatus ScCallbackPin(const void *function_address)
{
    if(!function_address)return sc32_invalid(SC32_E_INVALID,SC32_OP_CALLBACK_PIN);
    /* ROOT知道真实应用正文和本人READY/X段，不用“可读”冒充函数资格；
     * 固定provider和依赖闭包后，长期容器才可提交保存这个地址。 */
    ScI32 raw=ScApplicationPinCallback((void (*)(void))function_address);
    return sc32_runtime_error(raw,SC32_OP_CALLBACK_PIN);
}
