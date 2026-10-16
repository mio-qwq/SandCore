/* 这里只保存字节与控制事实；ANSI、字形和画面全部在SCTERM.SCL。
 * ring与端点均在objpool动态分配，不能让父legacy 16fd成为终端数量上限。 */
typedef struct {
    u32 endpoints,masters,readers,writers,columns,rows,geometry,hungup,session;
    int owner,input,output,master,slave;
    u32 owner_generation;
}
pty_t;
static objpool_t pty_pool;
static int pty_initialized;
static void reference(descriptor_t *,int);
static pty_t *pty_of(endpoint_t *e)
{
    return e && (e->type==END_PTY_MASTER || e->type==END_PTY_SLAVE)
    ?objpool_get(&pty_pool,e->pipe):0;
}
static endpoint_t *pty_peer(pty_t *p,int master)
{
    return p?objpool_get(&endpoint_pool,master?p->master:p->slave):0;
}
static void pty_changed(pty_t *p)
{
    endpoint_t *m=pty_peer(p,1),*s=pty_peer(p,0);
    if(m)events_changed(m);
    if(s)events_changed(s);
}
static int pty_access(int pid,pty_t *p)
{
    /* slave能力由真实spawn或认证继承；数值相同UID不能跨登录会话借用。 */
    return p && task_owned(pid) && p->session && session_task(pid)==p->session;
}
static void pty_reference(endpoint_t *e,descriptor_t *d,int add)
{
    pty_t *p=pty_of(e);
    if(!p)return;
    if(e->type==END_PTY_MASTER){
        if(add)p->masters++;
        else if(p->masters && !--p->masters){
            p->hungup=1;
            endpoint_t *s=pty_peer(p,0);
            if(s)s->event++;
        }
    }
    else{
        if(d->direction&STREAM_READ){
            if(add)p->readers++;
            else p->readers--;
        }
        if(d->direction&STREAM_WRITE){
            if(add)p->writers++;
            else p->writers--;
        }
    }
    if(!add && !e->refs){
        if(e->type==END_PTY_MASTER)p->master=-1;
        else p->slave=-1;
    }
    pty_changed(p);
}
static void pty_endpoint_release(endpoint_t *e)
{
    pty_t *p=pty_of(e);
    if(!p)return;
    if(!--p->endpoints){
        objpool_release(&pipe_pool,p->input);
        objpool_release(&pipe_pool,p->output);
        objpool_release(&pty_pool,e->pipe);
    }
}
static int pty_read(int pid,endpoint_t *e,void *data,u32 length)
{
    pty_t *p=pty_of(e);
    if(!pty_access(pid,p))return -5;
    int master=e->type==END_PTY_MASTER;
    pipe_t *q=&PIPE(master?p->output:p->input);
    u32 count=q->head-q->tail;
    if(count>length)count=length;
    for(u32 i=0;i<count;i++)((u8 *)data)[i]=q->bytes[(q->tail+i)&(PIPE_SIZE-1)];
    q->tail+=count;
    if(count){
        pty_changed(p);
        return (int)count;
    }
    if(master)return !p->writers || p->hungup?0:STREAM_AGAIN;
    if(p->hungup || !p->masters)return 0;
    if(e->eof){
        e->eof--;
        pty_changed(p);
        return 0;
    }
    return STREAM_AGAIN;
}
static int pty_write(int pid,endpoint_t *e,const void *data,u32 length)
{
    pty_t *p=pty_of(e);
    if(!pty_access(pid,p))return -5;
    int master=e->type==END_PTY_MASTER;
    if(p->hungup || !p->masters || (master && !p->readers))return -7;
    pipe_t *q=&PIPE(master?p->input:p->output);
    endpoint_t *slave=pty_peer(p,0);
    u32 count=0;
    for(;count<length;count++){
        u8 c=((const u8 *)data)[count];
        if(master && slave && slave->controls && (c==3 || c==4)){
            if(c==3){
                /* 中断必须越过积压输入，旧粘贴不能在前台退出后变成下一条命令。 */
                q->tail=q->head;
                slave->event++;
            }
            else if(slave->eof<0x7fffffffu)slave->eof++;
            continue;
        }
        if(q->head-q->tail==PIPE_SIZE)break;
        q->bytes[q->head++&(PIPE_SIZE-1)]=c;
    }
    if(count)pty_changed(p);
    return count?(int)count:STREAM_AGAIN;
}
static int pty_clear(int pid,endpoint_t *e)
{
    pty_t *p=pty_of(e);
    if(!pty_access(pid,p))return -5;
    if(e->type!=END_PTY_SLAVE)return -1;
    pipe_t *q=&PIPE(p->output);
    if(p->hungup || !p->masters)return -7;
    /* 清屏与后续frame在相同输出队列排序；空余不足时一次也不写。 */
    if(PIPE_SIZE-(q->head-q->tail)<7)return STREAM_AGAIN;
    return pty_write(pid,e,"\033[2J\033[H",7)==7?0:-1;
}
static int pty_old_info(int pid,endpoint_t *e,u32 out[8])
{
    pty_t *p=pty_of(e);
    if(!pty_access(pid,p))return -5;
    out[2]=p->columns;
    out[3]=p->rows;
    out[4]=3;
    out[6]=p->geometry;
    return 0;
}
static void pty_task_close(int pid)
{
    descriptor_t *d=&STREAM_CONTEXT(pid).control_terminal;
    if(d->endpoint>=0){
        reference(d,0);
        *d=(descriptor_t){
            -1,0
        };
    }
}
static int pty_task_attach(int pid,int index)
{
    endpoint_t *e=objpool_get(&endpoint_pool,index);
    pty_t *p=pty_of(e);
    if(!e || e->type!=END_PTY_SLAVE || !pty_access(pid,p))return -5;
    descriptor_t *d=&STREAM_CONTEXT(pid).control_terminal;
    if(d->endpoint==index)return 0;
    pty_task_close(pid);
    *d=(descriptor_t){
        index,0,0,pid,task_generation(pid),0,0,0
    };
    reference(d,1);
    return 0;
}
static void pty_ready(int pid,endpoint_t *e,u32 *readable,u32 *writable,u32 *eof,int *error)
{
    pty_t *p=pty_of(e);
    if(!pty_access(pid,p)){
        *error=-5;
        return;
    }
    int master=e->type==END_PTY_MASTER;
    pipe_t *r=&PIPE(master?p->output:p->input),*w=&PIPE(master?p->input:p->output);
    *readable=r->head-r->tail;
    *writable=PIPE_SIZE-(w->head-w->tail);
    *eof=p->hungup || (master?!p->writers:e->eof || !p->masters);
    if(p->hungup || !p->masters || (master && !p->readers))*error=-7;
}
