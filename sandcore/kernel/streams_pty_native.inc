/* 已冻Capture后的streams_execute/native_allocate是真实底座；不再占父旧fd。
 * 创建期间由master临时引用保活，失败只撤本人已经取得的资源。 */
int streams_native_interactive(int pid,const char *command,u32 columns,u32 rows,u32 *master)
{
    if(pid!=task_pid() || !task_owned(pid) || !session_task(pid))return -5;
    if(!columns || columns>512 || !rows || rows>256 || !master)return -1;
    if(!pty_initialized){
        if(objpool_init(&pty_pool,sizeof(pty_t),0))return -4;
        pty_initialized=1;
    }
    int slot=objpool_alloc(&pty_pool);
    if(slot<0)return -4;
    pty_t *p=objpool_get(&pty_pool,slot);
    p->input=p->output=p->master=p->slave=-1;
    int input=objpool_alloc(&pipe_pool),output=input>=0?objpool_alloc(&pipe_pool):-1;
    int me=output>=0?allocate_endpoint():-1,se=me>=0?allocate_endpoint():-1;
    native_reference_t *r=se>=0?native_allocate(pid):0;
    if(!r){
        if(se>=0)objpool_release(&endpoint_pool,se);
        if(me>=0)objpool_release(&endpoint_pool,me);
        if(output>=0)objpool_release(&pipe_pool,output);
        if(input>=0)objpool_release(&pipe_pool,input);
        objpool_release(&pty_pool,slot);
        return -4;
    }
    p->input=input;
    p->output=output;
    p->master=me;
    p->slave=se;
    p->endpoints=2;
    p->columns=columns;
    p->rows=rows;
    p->geometry=1;
    p->owner=pid;
    p->owner_generation=task_generation(pid);
    p->session=session_task(pid);
    ENDPOINT(me).type=END_PTY_MASTER;
    ENDPOINT(me).pipe=slot;
    ENDPOINT(se).type=END_PTY_SLAVE;
    ENDPOINT(se).pipe=slot;
    r->descriptor.endpoint=me;
    r->descriptor.direction=STREAM_READ|STREAM_WRITE;
    reference(&r->descriptor,1);
    descriptor_t slave={
        se,STREAM_READ|STREAM_WRITE,0,pid,task_generation(pid),0,0,0
    };
    reference(&slave,1);
    descriptor_t targets[3]={
        slave,slave,slave
    };
    targets[0].direction=STREAM_READ;
    targets[1].direction=targets[2].direction=STREAM_WRITE;
    int job=streams_execute(pid,command,targets,0);
    if(job>0){
        job_t *j=owned_job(pid,(u32)job);
        /* 同一陷入内孩子尚未运行，使用刚创建票据的实际child/gen，不猜PID。 */
        if(!j || !task_owned(j->child) || task_generation(j->child)!=j->child_generation
        || pty_task_attach(j->child,se)<0){
            if(j){
                int child=j->child;
                u32 generation=j->child_generation;
                release_job(j);
                if(task_owned(child) && task_generation(child)==generation){
                    process_exit(child,130);
                    task_stop(child);
                }
            }
            job=-5;
        }
    }
    reference(&slave,0);
    if(job<1){
        streams_native_close(pid,r->token);
        return job;
    }
    *master=r->token;
    return job;
}
static pty_t *pty_master_reference(int pid,u32 token)
{
    native_reference_t *r=native_reference(pid,token);
    endpoint_t *e=native_endpoint(r);
    pty_t *p=e && e->type==END_PTY_MASTER?pty_of(e):0;
    return p && pty_access(pid,p) && p->owner==pid && p->owner_generation==task_generation(pid)?p:0;
}
int streams_native_terminal_query(int pid,u32 token,u32 out[16])
{
    pty_t *p=pty_master_reference(pid,token);
    if(!p)return -8;
    clear(out,64);
    out[0]=64;
    out[1]=1;
    out[2]=END_PTY_MASTER;
    out[3]=p->columns;
    out[4]=p->rows;
    out[5]=p->geometry;
    out[6]=PIPE(p->input).head-PIPE(p->input).tail;
    out[7]=PIPE(p->output).head-PIPE(p->output).tail;
    out[8]=7|(p->hungup?8:0);
    endpoint_t *s=pty_peer(p,0);
    out[9]=s?s->event:0;
    out[10]=p->readers;
    out[11]=p->writers;
    return 0;
}
int streams_native_terminal_resize(int pid,u32 token,u32 columns,u32 rows)
{
    pty_t *p=pty_master_reference(pid,token);
    if(!p)return -8;
    if(!columns || columns>512 || !rows || rows>256)return -1;
    if(p->hungup)return -7;
    if(columns!=p->columns || rows!=p->rows){
        if(p->geometry==0xffffffffu)return -4;
        p->columns=columns;
        p->rows=rows;
        p->geometry++;
        pty_changed(p);
    }
    return 0;
}
int streams_native_terminal_control(int pid,u32 token,u32 action)
{
    pty_t *p=pty_master_reference(pid,token);
    if(!p)return -8;
    if(action<1 || action>3)return -1;
    endpoint_t *m=pty_peer(p,1),*s=pty_peer(p,0);
    if(action==3){
        if(!p->hungup){
            p->hungup=1;
            if(s)s->event++;
            pty_changed(p);
        }
        return 0;
    }
    if(p->hungup)return -7;
    u8 byte=action==1?3:4;
    return pty_write(pid,m,&byte,1)==1?0:STREAM_AGAIN;
}
