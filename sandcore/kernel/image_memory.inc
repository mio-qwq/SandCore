/* 插在working之后，调用处均持有IF=0。输入页只有完整封存后才入解码队列，
 * 所以半传输不会阻塞后续路径请求，也不会让服务读到未初始化物理页。 */
static void input_release(image_job_t *job)
{
    if(job->input)pframe_free_run((u32)job->input,job->input_pages);
    job->input=0;
    job->input_pages=job->input_bytes=job->input_received=0;
}
static void pending_append(image_job_t *job,int kind)
{
    int *head=kind==2?&upload_head:&pending_head,*tail=kind==2?&upload_tail:&pending_tail;
    job->previous=*tail;
    job->next=-1;
    job->queued=kind;
    if(*tail>=0)IMAGE_JOB(*tail).next=job->owner;
    else *head=job->owner;
     *tail=job->owner;
    task_kernel_wake();
}
int image_service_memory_begin(int owner,u32 bytes)
{
    if(owner<=0 || !task_owned(owner) || IMAGE_JOB(owner).state)return SC32_E_BUSY;
    if(owner==active_worker && task_generation(owner)==active_generation)return SC32_E_ACCESS;
    if(bytes<8 || bytes>16u*1024u*1024u)return SC32_E_RANGE;
    if(serial==0x7fffffffu)return SC32_E_RANGE;
    u32 pages=(bytes+4095u)/4096u;
    u8 *input=(u8 *)pframe_alloc_run(pages);
    if(!input)return SC32_E_MEMORY;
    image_job_t *job=&IMAGE_JOB(owner);
    job->ticket=++serial;
    job->owner=owner;
    job->generation=task_generation(owner);
    job->started=sc_ticks;
    job->state=5;
    job->worker=job->claimed=job->error=0;
    job->path[0]=0;
    job->source_kind=2;
    job->input=input;
    job->input_pages=pages;
    job->input_bytes=bytes;
    job->input_received=0;
    pending_append(job,2);
    return (int)job->ticket;
}
int image_service_memory_write(int owner,u32 ticket,u32 offset,u32 address,u32 bytes)
{
    image_job_t *job=owned(owner,ticket);
    if(!job || job->state!=5 || job->source_kind!=2)return SC32_E_STALE;
    if(!bytes || bytes>65536u || offset!=job->input_received || bytes>job->input_bytes-offset)return SC32_E_RANGE;
    /* 无跨调用用户指针；每块重新查调用进程页表。最多64KiB复制保持输入及时。
     * 未成功的块不推进received，封存阶段不能发布半成品。 */
    if(paging_user_copy(TASK(owner).pd,address,job->input+offset,bytes,0))return SC32_E_INVALID;
    job->input_received+=bytes;
    return 0;
}
int image_service_memory_seal(int owner,u32 ticket)
{
    image_job_t *job=owned(owner,ticket);
    if(!job || job->state!=5 || job->source_kind!=2)return SC32_E_STALE;
    if(job->input_received!=job->input_bytes)return SC32_E_BUSY;
    pending_remove(job);
    job->state=1;
    pending_append(job,1);
    return 0;
}
int image_service_source_claim(int worker,u32 ticket,u32 *kind,u32 *bytes,char path[64])
{
    image_job_t *job=working(worker,ticket);
    if(!job || job->claimed)return SC32_E_STALE;
     *kind=job->source_kind;
     *bytes=job->source_kind==2?job->input_bytes:0;
    for(u32 n=0;n<64;n++)path[n]=job->source_kind==2?0:job->path[n];
    job->claimed=1;
    return 0;
}
int image_service_memory_read(int worker,u32 ticket,u32 offset,u32 address,u32 bytes)
{
    image_job_t *job=working(worker,ticket);
    if(!job || !job->claimed || job->source_kind!=2)return SC32_E_STALE;
    if(!bytes || bytes>65536u || offset>job->input_bytes || bytes>job->input_bytes-offset)return SC32_E_RANGE;
    /* worker只能拿到自己被指派的封存来源；不能指定owner、代数或内核地址。 */
    return paging_user_copy(TASK(worker).pd,address,job->input+offset,bytes,1)?SC32_E_INVALID:0;
}
