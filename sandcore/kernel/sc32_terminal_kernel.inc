/* 私有FILE分发器中的增量，capture_reader已预留现代native描述。
 * 所有入参/输出映射在spawn前验证，失败不留下无人能领票的孩子。 */
static int terminal_command(int pid,u32 address,char out[1088])
{
    if(!address)return SC32_E_INVALID;
    for(u32 n=0;n<1088;n++){
        if(address>0xffffffffu-n || !paging_user_range(TASK(pid).pd,address+n,1)
        || paging_user_copy(TASK(pid).pd,address+n,out+n,1,0))return SC32_E_INVALID;
        if(!out[n])return n?0:SC32_E_INVALID;
    }
    return SC32_E_RANGE;
}
static int terminal_spawn(int pid,u32 request,u32 output)
{
    Sc32TerminalSpawnRequest q;
    int status=sc32_core_input(pid,request,&q,sizeof(q));
    if(status)return status;
    status=sc32_core_output(pid,output,sizeof(ScInteractiveInfo));
    if(status)return status;
    if(q.flags || q.reserved[0] || q.reserved[1] || !q.columns || q.columns>512 || !q.rows || q.rows>256)return SC32_E_INVALID;
    char command[1088];
    status=terminal_command(pid,q.command,command);
    if(status)return status;
    status=initialize();
    if(status)return status;
    file_handle_t *h=capture_reader(pid);
    if(!h)return SC32_E_MEMORY;
    file_description_t *d=description(h);
    d->flags=FS_NATIVE_READ|FS_NATIVE_WRITE;
    u32 ref=0;
    int job=streams_native_interactive(pid,command,q.columns,q.rows,&ref);
    if(job<1){
        drop(h);
        return job==-2?SC32_E_NOT_FOUND:job==-4||job==-6?SC32_E_MEMORY:job==-5?SC32_E_ACCESS:SC32_E_INVALID;
    }
    h->native_reference=ref;
    ScInteractiveInfo r;
    clear(&r,sizeof(r));
    r.size=sizeof(r);
    r.version=1;
    r.job=(u32)job;
    r.master=h->id;
    r.flags=7;
    return publish(pid,output,&r,sizeof(r));
}
static int terminal_call(int pid,u32 operation,u32 request,u32 output)
{
    if(operation==SC32_T_SPAWN)return terminal_spawn(pid,request,output);
    Sc32TerminalRequest q;
    int status=sc32_core_input(pid,request,&q,sizeof(q));
    if(status)return status;
    if(q.reserved[0] || q.reserved[1] || q.reserved[2])return SC32_E_INVALID;
    if(operation==SC32_T_QUERY){
        if(q.a || q.b)return SC32_E_INVALID;
        status=sc32_core_output(pid,output,sizeof(ScTerminalInfo));
        if(status)return status;
    }
    else if(output)return SC32_E_INVALID;
    if(!initialized)return SC32_E_STALE;
    file_handle_t *h=handle(pid,q.handle,FILE_HANDLE);
    file_description_t *d=description(h);
    if(!h || !d || !d->native_endpoint || !h->native_reference || h->closing)return SC32_E_STALE;
    int raw;
    if(operation==SC32_T_QUERY){
        u32 words[16];
        raw=streams_native_terminal_query(pid,h->native_reference,words);
        if(raw<0)return stream_status(raw);
        return publish(pid,output,words,sizeof(words));
    }
    if(operation==SC32_T_RESIZE)raw=streams_native_terminal_resize(pid,h->native_reference,q.a,q.b);
    else if(operation==SC32_T_CONTROL){
        if(q.b)return SC32_E_INVALID;
        raw=streams_native_terminal_control(pid,h->native_reference,q.a);
    }
    else return SC32_E_NOT_SUPPORTED;
    return stream_status(raw);
}
