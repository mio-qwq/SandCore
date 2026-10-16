/* 仅在streams.c末尾包含；现代引用从动态池取得，不经过父FD_MAX。
 * SYS300在同一IF=0期间预分配票据/引用后spawn，子进程尚不能抢跑。 */
static int streams_native_pipe_new(int pid,u32 refs[2])
{
    int ep=allocate_endpoint();
    if(ep<0)return ep;
    int slot=objpool_alloc(&pipe_pool);
    if(slot<0){
        objpool_release(&endpoint_pool,ep);
        return -4;
    }
    ENDPOINT(ep).type=END_PIPE;
    ENDPOINT(ep).pipe=slot;
    native_reference_t *r=native_allocate(pid);
    if(!r){
        objpool_release(&pipe_pool,slot);
        objpool_release(&endpoint_pool,ep);
        return -4;
    }
    r->descriptor.endpoint=ep;
    r->descriptor.direction=STREAM_READ;
    reference(&r->descriptor,1);
    refs[0]=r->token;
    native_reference_t *w=native_allocate(pid);
    if(!w){
        streams_native_close(pid,refs[0]);
        refs[0]=0;
        return -4;
    }
    w->descriptor.endpoint=ep;
    w->descriptor.direction=STREAM_WRITE;
    reference(&w->descriptor,1);
    refs[1]=w->token;
    return 0;
}
int streams_native_capture(int pid,const char *command,u32 readers[2])
{
    if(pid!=task_pid() || !task_owned(pid))return -5;
    u32 a[2]={
        0,0
    },b[2]={
        0,0
    };
    int result=streams_native_pipe_new(pid,a);
    if(result<0)return result;
    result=streams_native_pipe_new(pid,b);
    if(result<0){
        streams_native_close(pid,a[0]);
        streams_native_close(pid,a[1]);
        return result;
    }
    descriptor_t target[3];
    clear(target,sizeof(target));
    for(int i=0;i<3;i++)target[i].endpoint=-1;
    target[1]=native_reference(pid,a[1])->descriptor;
    target[2]=native_reference(pid,b[1])->descriptor;
    /* EOF stdin是END_NULL的真正READ端点，不能以坏fd的IO错误冒充EOF。 */
    int null=allocate_endpoint();
    if(null<0)result=null;
    else{
        ENDPOINT(null).type=END_NULL;
        descriptor_t input={
            null,STREAM_READ,0,pid,task_generation(pid),0,0,0
        };
        reference(&input,1);
        target[0]=input;
        result=streams_execute(pid,command,target,0);
        reference(&input,0);
    }
    streams_native_close(pid,a[1]);
    streams_native_close(pid,b[1]);
    if(result<1){
        streams_native_close(pid,a[0]);
        streams_native_close(pid,b[0]);
        return result;
    }
    readers[0]=a[0];
    readers[1]=b[0];
    return result;
}
