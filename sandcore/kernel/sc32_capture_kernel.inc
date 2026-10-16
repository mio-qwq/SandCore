/* 在sc32_file.c的私有new_handle/description helper之后包含。
 * FILE的native_endpoint字段与standard索引分开，绝不以standard=3
 * 假装新端点，否则owner.standard[3]会越界。 */
static file_handle_t *capture_reader(int pid)
{
    file_handle_t *h=new_handle(pid,FILE_HANDLE);
    if(!h)return 0;
    int slot=objpool_alloc(&descriptions);
    if(slot<0){
        drop(h);
        return 0;
    }
    file_description_t *d=objpool_get(&descriptions,slot);
    d->standard=-1;
    d->native_endpoint=1;
    d->ready=1;
    d->flags=FS_NATIVE_READ;
    attach_description(h,(u32)slot+1);
    return h;
}
static int capture_spawn(int pid,u32 request,u32 output)
{
    Sc32CaptureRequest q;
    int status=sc32_core_input(pid,request,&q,sizeof(q));
    if(status)return status;
    status=sc32_core_output(pid,output,sizeof(ScCaptureInfo));
    if(status)return status;
    if(q.flags || q.reserved[0] || q.reserved[1] || q.reserved[2] || q.reserved[3])return SC32_E_INVALID;
    char command[1088];
    u32 n=0;
    for(;n<sizeof(command);n++){
        if(q.command>0xffffffffu-n || !paging_user_range(TASK(pid).pd,q.command+n,1)
        || paging_user_copy(TASK(pid).pd,q.command+n,command+n,1,0))return SC32_E_INVALID;
        if(!command[n])break;
    }
    if(n==sizeof(command) || !n)return SC32_E_RANGE;
    status=initialize();
    if(status)return status;
    file_handle_t *a=capture_reader(pid);
    if(!a)return SC32_E_MEMORY;
    file_handle_t *b=capture_reader(pid);
    if(!b){
        drop(a);
        return SC32_E_MEMORY;
    }
    u32 refs[2]={
        0,0
    };
    int job=streams_native_capture(pid,command,refs);
    if(job<1){
        drop(b);
        drop(a);
        return job==-2?SC32_E_NOT_FOUND:job==-4||job==-6?SC32_E_MEMORY:job==-5?SC32_E_ACCESS:SC32_E_INVALID;
    }
    a->native_reference=refs[0];
    b->native_reference=refs[1];
    ScCaptureInfo result;
    clear(&result,sizeof(result));
    result.size=sizeof(result);
    result.version=1;
    result.job=(u32)job;
    result.output=a->id;
    result.error=b->id;
    /* 整个out在任何分配前已验RW；同一陷入无用户线程能撤映射。
     * 没有把用户out地址寄存到异步对象或等用户领取才驱动子任务。 */
    return publish(pid,output,&result,sizeof(result));
}
