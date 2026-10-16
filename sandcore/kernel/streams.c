#include "streams.h"
#include "task.h"
#include "auth.h"
#include "session.h"
#include "userspace.h"
#include "fs.h"
#include "process.h"
#include "wm.h"
#include "management.h"
#include "memory.h"
#include "interrupts.h"
#include "objpool.h"
#include "ata.h"
#define FD_MAX 16
#define PIPE_SIZE 4096u
enum { END_FILE=1,END_REPLACE,END_PIPE,END_SERIAL,END_TTY,END_NULL,END_MEMORY,END_PTY_MASTER,END_PTY_SLAVE };
typedef struct descriptor descriptor_t;
typedef struct {
    u32 refs,type,position,generation,token,controls,event,eof,key_head,key_tail;
    u8 keys[256];
    int owner,pipe,window;char path[64];
    u32 memory,bytes,pages;
    u8 utf8[4];u32 utf8_used,utf8_needed,utf8_writer,utf8_generation;
    descriptor_t *members,*event_cursor;
    u32 event_epoch,event_walk_epoch;
    int index,event_previous,event_next,event_queued,owner_previous,owner_next;
} endpoint_t;
struct descriptor {int endpoint;u32 direction,controls;int owner;u32 generation;descriptor_t *previous,*next;u32 weak;};
typedef struct { u8 bytes[PIPE_SIZE];u32 head,tail,readers,writers; } pipe_t;
typedef struct {
    u32 ticket,parent_generation,child_generation;
    int parent,child,result,original_child,parent_prev,parent_next;
} job_t;
static objpool_t endpoint_pool,pipe_pool,job_pool,native_pool;
#define ENDPOINT(index) (*(endpoint_t *)objpool_get(&endpoint_pool,index))
#define PIPE(index) (*(pipe_t *)objpool_get(&pipe_pool,index))
typedef struct {descriptor_t fds[FD_MAX];char arguments[1024];int jobs_head,owned_job,terminal_head,stop_phase,native_head;
    u32 standard_refs[3];descriptor_t control_terminal;} stream_task_t;
#define STREAM_CONTEXT(pid) (*(stream_task_t *)task_data(pid,TASK_DATA_STREAMS))
u32 streams_task_bytes(void){return sizeof(stream_task_t);}

static u32 job_counter;
typedef struct {descriptor_t descriptor;u32 token;int previous,next,standard;} native_reference_t;
static u32 native_counter;
static u8 serial_input[4096];
static u32 serial_head,serial_tail,serial_session;
static int serial_endpoint=-1,event_head=-1,event_tail=-1;
static void events_remove(endpoint_t *e)
{
    if(!e->event_queued)return;
    if(e->event_previous>=0)ENDPOINT(e->event_previous).event_next=e->event_next;else event_head=e->event_next;
    if(e->event_next>=0)ENDPOINT(e->event_next).event_previous=e->event_previous;else event_tail=e->event_previous;
    e->event_queued=0;e->event_previous=e->event_next=-1;
}
static void events_append(endpoint_t *e)
{
    e->event_previous=event_tail;e->event_next=-1;e->event_queued=1;
    if(event_tail>=0)ENDPOINT(event_tail).event_next=e->index;else event_head=e->index;
    event_tail=e->index;
}
static void events_changed(endpoint_t *e)
{
    e->event_epoch++;
    if(!e->event_queued){e->event_cursor=e->members;e->event_walk_epoch=e->event_epoch;events_append(e);}
    task_kernel_wake();
}
static u32 size(const char *s){u32 n=0;while(s[n])n++;return n;}
static void clear(void *p,u32 n){for(u32 i=0;i<n;i++)((u8 *)p)[i]=0;}
void streams_init(void)
{
    /* 三个全局池没有预设数量；每任务描述符仍按已发布的16项合同。
     * 池节点/正文直接借物理页，不能把固定内核小堆变成替代任务上限。 */
    if(objpool_init(&endpoint_pool,sizeof(endpoint_t),0) || objpool_init(&pipe_pool,sizeof(pipe_t),0)
        || objpool_init(&job_pool,sizeof(job_t),0) || objpool_init(&native_pool,sizeof(native_reference_t),0))panic("STREAM MEMORY",0);
    for(int pid=task_next(-1);pid>=0;pid=task_next(pid)){
        STREAM_CONTEXT(pid).jobs_head=-1;STREAM_CONTEXT(pid).owned_job=-1;
        STREAM_CONTEXT(pid).control_terminal=(descriptor_t){-1,0};
        STREAM_CONTEXT(pid).terminal_head=STREAM_CONTEXT(pid).native_head=-1;
        for(int fd=0;fd<FD_MAX;fd++)STREAM_CONTEXT(pid).fds[fd].endpoint=-1;
    }
}
static endpoint_t *endpoint(int pid,int fd)
{
    if(pid<1 || !task_owned(pid) || fd<0 || fd>=FD_MAX)return 0;
    descriptor_t *d=&STREAM_CONTEXT(pid).fds[fd];
    if(d->weak && (fd>2 || !STREAM_CONTEXT(pid).standard_refs[fd]))return 0;
    int index=d->endpoint;
    endpoint_t *e=objpool_get(&endpoint_pool,index);return e && e->refs?e:0;
}
static int vacant(int pid,int first)
{if(!task_owned(pid))return -1;for(int fd=first;fd<FD_MAX;fd++)if(STREAM_CONTEXT(pid).fds[fd].endpoint<0)return fd;return -4;}
static int allocate_endpoint(void)
{
    int index=objpool_alloc(&endpoint_pool);if(index<0)return -4;
    endpoint_t *e=&ENDPOINT(index);e->index=index;e->event_previous=e->event_next=e->owner_previous=e->owner_next=-1;
    return index;
}
static void terminal_unlink(endpoint_t *e)
{
    if(e->type!=END_TTY || !e->generation)return;
    if(task_owned(e->owner) && task_generation(e->owner)==e->generation){
        if(e->owner_previous>=0)ENDPOINT(e->owner_previous).owner_next=e->owner_next;
        else STREAM_CONTEXT(e->owner).terminal_head=e->owner_next;
        if(e->owner_next>=0)ENDPOINT(e->owner_next).owner_previous=e->owner_previous;
    }
    e->generation=0;e->owner_previous=e->owner_next=-1;
}
#include "streams_pty_core.inc"
static void reference(descriptor_t *d,int add)
{
    if(d->endpoint<0)return;
    endpoint_t *e=objpool_get(&endpoint_pool,d->endpoint);if(!e || (!add && !e->refs))return;
    if(d->weak){
        /* 弱proxy只承担旧slot的控制订阅和默认继承路由，不计reader/
         * writer。不放入成员链，避免成为隐形EOF保活引用。 */
        if(!add && d->controls){e->controls-=d->controls;d->controls=0;}
        return;
    }
    if(add){d->previous=0;d->next=e->members;if(d->next)d->next->previous=d;e->members=d;}
    else{
        if(e->event_cursor==d)e->event_cursor=d->next;
        if(d->previous)d->previous->next=d->next;else e->members=d->next;
        if(d->next)d->next->previous=d->previous;
    }
    if(!add && d->controls)e->controls-=d->controls;
    if(add)e->refs++;else e->refs--;
    if(e->type==END_PIPE){
        pipe_t *p=&PIPE(e->pipe);
        if(d->direction&STREAM_READ){if(add)p->readers++;else p->readers--;}
        if(d->direction&STREAM_WRITE){if(add)p->writers++;else p->writers--;}
    }
    pty_reference(e,d,add);
    if(!e->refs && e->type==END_REPLACE && e->token)fs_stream_abort(e->owner,e->token);
    if(!e->refs && e->type==END_MEMORY && e->pages){pframe_free_run(e->memory,e->pages);e->memory=e->bytes=e->pages=0;}
    if(!e->refs){
        events_remove(e);terminal_unlink(e);pty_endpoint_release(e);if(serial_endpoint==e->index)serial_endpoint=-1;
        if(e->type==END_PIPE)objpool_release(&pipe_pool,e->pipe);objpool_release(&endpoint_pool,d->endpoint);
    }else events_changed(e);
}
static void attach(int pid,int fd,int index,u32 direction)
{
    descriptor_t *d=&STREAM_CONTEXT(pid).fds[fd];
    if(d->endpoint>=0)reference(d,0);
    d->endpoint=index;d->direction=direction;d->controls=0;d->weak=0;d->owner=pid;d->generation=task_generation(pid);reference(d,1);
    task_notify(pid,TASK_EVENT_STREAM);
}
void streams_spawn(int pid,int parent)
{
    for(int fd=0;fd<FD_MAX;fd++)STREAM_CONTEXT(pid).fds[fd]=(descriptor_t){-1,0};
    STREAM_CONTEXT(pid).arguments[0]=0;
    STREAM_CONTEXT(pid).control_terminal=(descriptor_t){-1,0};
    STREAM_CONTEXT(pid).jobs_head=STREAM_CONTEXT(pid).owned_job=-1;
    STREAM_CONTEXT(pid).terminal_head=STREAM_CONTEXT(pid).native_head=-1;
    for(int i=0;i<3;i++)STREAM_CONTEXT(pid).standard_refs[i]=0;
    if(parent>0 && parent<NTASK && auth_uid(pid)==auth_uid(parent))
        for(int fd=0;fd<3;fd++)if(endpoint(parent,fd)){
            descriptor_t d=STREAM_CONTEXT(parent).fds[fd];attach(pid,fd,d.endpoint,d.direction);
        }
    if(parent>0 && task_owned(parent) && auth_uid(pid)==auth_uid(parent)
        && session_task(pid)==session_task(parent) && STREAM_CONTEXT(parent).control_terminal.endpoint>=0)
        pty_task_attach(pid,STREAM_CONTEXT(parent).control_terminal.endpoint);
}
static job_t *owned_job(int parent,u32 token)
{
    if(token>0x7FFFFFFFu || !task_owned(parent))return 0;
    job_t *j=objpool_get(&job_pool,(int)token);
    return j && j->ticket==token && j->parent==parent && j->parent_generation==task_generation(parent)?j:0;
}
static void release_job(job_t *j)
{
    /* 父子都存真实票据与代数。退出仅走自己的关联链，避免N个子
     * 进程退出各扫N个作业；先断链再停止子进程可安全递归。 */
    if(task_owned(j->parent) && task_generation(j->parent)==j->parent_generation){
        if(j->parent_prev>=0)((job_t *)objpool_get(&job_pool,j->parent_prev))->parent_next=j->parent_next;
        else STREAM_CONTEXT(j->parent).jobs_head=j->parent_next;
        if(j->parent_next>=0)((job_t *)objpool_get(&job_pool,j->parent_next))->parent_prev=j->parent_prev;
    }
    if(j->child>0 && task_owned(j->child) && task_generation(j->child)==j->child_generation)
        STREAM_CONTEXT(j->child).owned_job=-1;
    objpool_release(&job_pool,(int)j->ticket);
}
void streams_stop(int pid,int code)
{
    while(STREAM_CONTEXT(pid).native_head>=0){
        native_reference_t *r=objpool_get(&native_pool,STREAM_CONTEXT(pid).native_head);
        if(!r)break;streams_native_close(pid,r->token);
    }
    pty_task_close(pid);
    while(STREAM_CONTEXT(pid).terminal_head>=0){
        endpoint_t *e=&ENDPOINT(STREAM_CONTEXT(pid).terminal_head);terminal_unlink(e);events_changed(e);
    }
    for(int fd=0;fd<FD_MAX;fd++)streams_close(pid,fd,0);
    fs_stream_stop(pid);
    job_t *mine=objpool_get(&job_pool,STREAM_CONTEXT(pid).owned_job);
    if(mine && mine->child==pid && mine->child_generation==task_generation(pid)){mine->child=0;mine->result=code;
        task_notify_generation(mine->parent,mine->parent_generation,TASK_EVENT_JOB);}
    STREAM_CONTEXT(pid).owned_job=-1;
    while(STREAM_CONTEXT(pid).jobs_head>=0){
        job_t *j=objpool_get(&job_pool,STREAM_CONTEXT(pid).jobs_head);
        int child=j->child;u32 generation=j->child_generation;release_job(j);
        if(child>0 && generation==task_generation(child) && TASK(child).state && TASK(child).state!=2){
            process_exit(child,130);task_stop(child);}
    }
    STREAM_CONTEXT(pid).arguments[0]=0;
}
int streams_stop_step(int pid,int code)
{
    stream_task_t *c=&STREAM_CONTEXT(pid);
    if(!c->stop_phase){
        if(c->native_head>=0){native_reference_t *r=objpool_get(&native_pool,c->native_head);
            if(r)streams_native_close(pid,r->token);else c->native_head=-1;return 0;}
        if(c->control_terminal.endpoint>=0){pty_task_close(pid);return 0;}
        if(c->terminal_head>=0){endpoint_t *e=&ENDPOINT(c->terminal_head);terminal_unlink(e);events_changed(e);return 0;}
        for(int fd=0;fd<FD_MAX;fd++)streams_close(pid,fd,0);
        c->stop_phase=1;return 0;
    }
    if(c->stop_phase==1){
        if(!fs_stream_stop_step(pid))return 0;
        job_t *mine=objpool_get(&job_pool,c->owned_job);
        if(mine && mine->child==pid && mine->child_generation==task_generation(pid)){
            mine->child=0;mine->result=code;task_notify_generation(mine->parent,mine->parent_generation,TASK_EVENT_JOB);
        }
        c->owned_job=-1;c->stop_phase=2;
    }
    if(c->jobs_head>=0){
        job_t *j=objpool_get(&job_pool,c->jobs_head);int child=j->child;u32 generation=j->child_generation;
        release_job(j);
        if(child>0 && generation==task_generation(child) && TASK(child).state && TASK(child).state!=2){process_exit(child,130);task_stop(child);}
        return 0;
    }
    c->arguments[0]=0;return 1;
}
int streams_open(int pid,const char *path,u32 mode,u32 capacity)
{
    if((mode!=STREAM_READ && mode!=STREAM_WRITE && mode!=STREAM_NULL && mode!=STREAM_TERMINAL) || !path)return -1;
    if(mode==STREAM_TERMINAL){int fd=vacant(pid,3);if(fd<0)return fd;
        int control=STREAM_CONTEXT(pid).control_terminal.endpoint;endpoint_t *ce=objpool_get(&endpoint_pool,control);
        if(ce && ce->type==END_PTY_SLAVE && pty_access(pid,pty_of(ce))){attach(pid,fd,control,STREAM_READ|STREAM_WRITE);return fd;}
        for(int source=0;source<3;source++){endpoint_t *e=endpoint(pid,source);if(e && (e->type==END_TTY || e->type==END_SERIAL || (e->type==END_PTY_SLAVE && pty_access(pid,pty_of(e))))){
                descriptor_t d=STREAM_CONTEXT(pid).fds[source];attach(pid,fd,d.endpoint,STREAM_READ|STREAM_WRITE);return fd;}}return -1;}
    int fd=vacant(pid,3);if(fd<0)return fd;int index=allocate_endpoint();if(index<0)return index;
    endpoint_t *e=&ENDPOINT(index);e->owner=pid;
    if(mode==STREAM_NULL){e->type=END_NULL;attach(pid,fd,index,STREAM_READ|STREAM_WRITE);return fd;}
    int result=-1;if(userspace_resolve(pid,path,e->path,sizeof(e->path))<0)goto failed;
    if(mode==STREAM_READ){
        u32 info[8];if(!fs_access(pid,e->path,FS_ACCESS_READ)){result=-5;goto failed;}
        if(fs_metadata(e->path,info) || info[1]!=1)goto failed;
        e->type=END_FILE;e->generation=info[7];
    }else{
        int token=fs_stream_begin(pid,e->path,capacity);if(token<0){result=token;goto failed;}
        e->type=END_REPLACE;e->token=(u32)token;
    }
    attach(pid,fd,index,mode);return fd;
failed:
    objpool_release(&endpoint_pool,index);return result;
}
int streams_close(int pid,int fd,int commit)
{
    endpoint_t *e=endpoint(pid,fd);if(!e)return -1;
    if(commit && e->type==END_REPLACE && e->token){
        if(e->owner!=pid)return -5;
        int result=fs_stream_commit(pid,e->token);if(result<0)return result;e->token=0;
    }
    descriptor_t *d=&STREAM_CONTEXT(pid).fds[fd];reference(d,0);*d=(descriptor_t){-1,0};return 0;
}
int streams_dup(int pid,int from,int to)
{
    if(!endpoint(pid,from) || to<0 || to>=FD_MAX)return -1;
    if(from==to)return to;
    descriptor_t d=STREAM_CONTEXT(pid).fds[from];attach(pid,to,d.endpoint,d.direction);return to;
}
int streams_pipe(int pid,int out[2])
{
    int a=vacant(pid,3);if(a<0)return -4;
    int b=-1;for(int i=a+1;i<FD_MAX;i++)if(STREAM_CONTEXT(pid).fds[i].endpoint<0){b=i;break;}
    if(b<0)return -4;int index=allocate_endpoint();if(index<0)return index;
    int slot=objpool_alloc(&pipe_pool);if(slot<0){objpool_release(&endpoint_pool,index);return -4;}
    ENDPOINT(index).type=END_PIPE;ENDPOINT(index).pipe=slot;
    attach(pid,a,index,STREAM_READ);attach(pid,b,index,STREAM_WRITE);out[0]=a;out[1]=b;return 0;
}
int streams_memory(int pid,const void *data,u32 bytes)
{
    if(bytes>32768)return -1;int fd=vacant(pid,3);if(fd<0)return fd;
    int index=allocate_endpoint();if(index<0)return index;
    u32 pages=(bytes+4095)/4096,base=pages?pframe_alloc_run(pages):0;
    if(pages && !base){objpool_release(&endpoint_pool,index);return -4;}
    endpoint_t *e=&ENDPOINT(index);e->type=END_MEMORY;e->owner=pid;e->memory=base;e->pages=pages;e->bytes=bytes;
    /* 复制的是调用者已经验证的三环缓冲；此后不可写，最后引用才回收。
     * here-document不占磁盘、不假借SYSTEM文件，也不会提前写满管道。 */
    for(u32 i=0;i<bytes;i++)((u8 *)base)[i]=((const u8 *)data)[i];attach(pid,fd,index,STREAM_READ);return fd;
}
static void tty_poll(endpoint_t *e)
{
    u32 original=e->key_head+e->event+e->eof;
    for(u32 budget=16;budget && e->key_head-e->key_tail<256;budget--){
        int key=wm_user_getkey(e->owner);if(key<0)break;
        if(key==3 && e->controls){e->event++;continue;}
        if(key==4 && e->controls){e->eof++;continue;}
        e->keys[e->key_head++&255]=(u8)key;
    }
    if(original!=e->key_head+e->event+e->eof)events_changed(e);
}
int streams_event(int pid,int fd,u32 action)
{
    endpoint_t *e=endpoint(pid,fd);if(!e || (e->type!=END_TTY && e->type!=END_SERIAL && e->type!=END_PTY_SLAVE) || action>2)return -1;
    if(e->type==END_PTY_SLAVE && !pty_access(pid,pty_of(e)))return -5;
    descriptor_t *d=&STREAM_CONTEXT(pid).fds[fd];
    if(action==1){if(d->controls==65535)return -4;d->controls++;e->controls++;}
    else if(action==2){if(!d->controls)return -1;d->controls--;e->controls--;}
    if(e->type==END_TTY)tty_poll(e);return (int)(e->event&0x7FFFFFFFu);
}
static int endpoint_read(int pid,endpoint_t *e,u32 direction,void *data,u32 length)
{
    if(!e || !(direction&STREAM_READ) || length>65536)return -1;
    if(!length)return 0;u8 *out=(u8 *)data;
    if(pty_of(e))return pty_read(pid,e,data,length);
    if(e->type==END_NULL)return 0;
    if(e->type==END_MEMORY){if(e->position>=e->bytes)return 0;u32 count=e->bytes-e->position;if(count>length)count=length;
        for(u32 i=0;i<count;i++)out[i]=((const u8 *)e->memory)[e->position+i];e->position+=count;return (int)count;}
    if(e->type==END_FILE){
        u32 info[8];if(!fs_access(pid,e->path,FS_ACCESS_READ))return -5;
        if(fs_metadata(e->path,info) || info[7]!=e->generation)return -8;
        int n=fs_read_at(e->path,data,length,e->position);if(n>0)e->position+=(u32)n;return n;
    }
    if(e->type==END_PIPE){
        pipe_t *p=&PIPE(e->pipe);u32 count=p->head-p->tail;if(count>length)count=length;
        for(u32 i=0;i<count;i++)out[i]=p->bytes[(p->tail+i)&(PIPE_SIZE-1)];p->tail+=count;
        if(count)events_changed(e);
        return count?(int)count:p->writers?STREAM_AGAIN:0;
    }
    if(e->type==END_SERIAL){
        if(e->generation!=serial_session)return -5;
        u32 count=serial_head-serial_tail;if(count>length)count=length;
        for(u32 i=0;i<count;i++)out[i]=serial_input[(serial_tail+i)&4095];serial_tail+=count;
        if(count)return (int)count;if(e->eof){e->eof--;return 0;}return STREAM_AGAIN;
    }
    if(e->type==END_TTY){
        if(e->generation!=task_generation(e->owner) || !wm_terminal_owned(e->owner,e->window))return -8;
        tty_poll(e);u32 count=e->key_head-e->key_tail;if(count>length)count=length;
        for(u32 i=0;i<count;i++)out[i]=e->keys[e->key_tail++&255];
        if(count)return (int)count;if(e->eof){e->eof--;return 0;}return STREAM_AGAIN;
    }
    return -1;
}
int streams_read(int pid,int fd,void *data,u32 length)
{endpoint_t *e=endpoint(pid,fd);return endpoint_read(pid,e,e?STREAM_CONTEXT(pid).fds[fd].direction:0,data,length);}
static int endpoint_write(int pid,endpoint_t *e,u32 direction,const void *data,u32 length)
{
    if(!e || !(direction&STREAM_WRITE) || length>65536)return -1;
    const u8 *in=(const u8 *)data;if(!length)return 0;
    if(pty_of(e))return pty_write(pid,e,data,length);
    if(e->type==END_NULL)return (int)length;
    if(e->type==END_REPLACE){
        /* 父Shell保留输出事务，子任务只能顺序写正文；commit仍只认
         * 原owner。每次按子任务真实身份再次检查，不能跨用户继承写权。 */
        if(auth_uid(pid)!=auth_uid(e->owner) || !e->token || !fs_access(pid,e->path,FS_ACCESS_WRITE)
            || !fs_user_mutable(e->path,0))return -5;
        return fs_stream_write(e->owner,e->token,data,length);
    }
    if(e->type==END_PIPE){
        pipe_t *p=&PIPE(e->pipe);if(!p->readers)return -7;
        u32 count=PIPE_SIZE-(p->head-p->tail);if(count>length)count=length;
        for(u32 i=0;i<count;i++)p->bytes[(p->head+i)&(PIPE_SIZE-1)]=in[i];p->head+=count;
        if(count)events_changed(e);
        return count?(int)count:STREAM_AGAIN;
    }
    if(e->type==END_SERIAL){
        if(e->generation!=serial_session)return -5;
        return management_console_write(data,length);
    }
    if(e->type==END_TTY){
        if(e->generation!=task_generation(e->owner) || !wm_terminal_owned(e->owner,e->window))return -8;
        /* 文字终端需NUL，分块只消耗确定已提交的部分；二进制不能隐式
         * 截断到首个零字节，明确返回错误，文件/管道路径始终是字节流。 */
        char text[262];u32 take=length>256?256:length,count=0,used=e->utf8_used,needed=e->utf8_needed;u8 scalar[4];
        for(u32 i=0;i<used;i++)scalar[i]=e->utf8[i];
        /* stdout/stderr及多个子任务共享终端，但不共享半个字符的归属。
         * 其它任务接手时显示一次替代符，不能拿另一UID/任务的续字节
         * 拼成标量；PID槽复用还须核对代数。候选状态在提交成功后发布。 */
        if(used && (e->utf8_writer!=(u32)pid || e->utf8_generation!=task_generation(pid))){text[count++]='?';used=0;}
        /* M9流写允许UTF-8跨系统调用（echo/printf逐字节也正确）。旧
         * PUTS校验完全不改；最多暂存一个标量，完成后再提交完整UTF-8。
         * 每次处理至多256输入字节，控制字符/坏序列明确失败。 */
        for(u32 i=0;i<take;i++){u8 c=in[i];if(!c){e->utf8_used=0;return -2;}
            if(used){if((c&0xC0)!=0x80 || (used==1 && ((scalar[0]==0xE0 && c<0xA0) || (scalar[0]==0xED && c>=0xA0)
                    || (scalar[0]==0xF0 && c<0x90) || (scalar[0]==0xF4 && c>=0x90)))){e->utf8_used=0;return -2;}scalar[used++]=c;
                if(used==needed){for(u32 j=0;j<used;j++)text[count++]=(char)scalar[j];used=0;}}
            else if(c<128)text[count++]=(char)c;
            else {needed=c>=0xC2 && c<=0xDF?2:c>=0xE0 && c<=0xEF?3:c>=0xF0 && c<=0xF4?4:0;if(!needed){e->utf8_used=0;return -2;}scalar[0]=c;used=1;}
        }
        text[count]=0;int result=count?wm_terminal_puts(e->owner,e->window,text):0;if(result<0)return result;
        e->utf8_used=used;e->utf8_needed=needed;e->utf8_writer=(u32)pid;e->utf8_generation=task_generation(pid);
        for(u32 i=0;i<used;i++)e->utf8[i]=scalar[i];return (int)take;
    }
    return -1;
}
int streams_write(int pid,int fd,const void *data,u32 length)
{endpoint_t *e=endpoint(pid,fd);return endpoint_write(pid,e,e?STREAM_CONTEXT(pid).fds[fd].direction:0,data,length);}
static int endpoint_seek(int pid,endpoint_t *e,u32 offset)
{
    if(!e)return -1;
    if(e->type==END_MEMORY){if(offset>e->bytes)return -1;e->position=offset;return (int)offset;}if(e->type!=END_FILE)return -1;
    u32 info[8];if(!fs_access(pid,e->path,FS_ACCESS_READ))return -5;
    if(fs_metadata(e->path,info) || info[7]!=e->generation || offset>info[2])return -8;
    e->position=offset;return (int)offset;
}
int streams_seek(int pid,int fd,u32 offset)
{return endpoint_seek(pid,endpoint(pid,fd),offset);}
int streams_tty(int pid,int window)
{
    if(!wm_terminal_owned(pid,window))return -5;
    int index=allocate_endpoint();if(index<0)return index;
    ENDPOINT(index).type=END_TTY;ENDPOINT(index).owner=pid;ENDPOINT(index).window=window;
    ENDPOINT(index).generation=task_generation(pid);
    ENDPOINT(index).owner_next=STREAM_CONTEXT(pid).terminal_head;
    if(ENDPOINT(index).owner_next>=0)ENDPOINT(ENDPOINT(index).owner_next).owner_previous=index;
    STREAM_CONTEXT(pid).terminal_head=index;
    attach(pid,0,index,STREAM_READ);attach(pid,1,index,STREAM_WRITE);attach(pid,2,index,STREAM_WRITE);return 0;
}
int streams_terminal_info(int pid,int fd,u32 out[8])
{
    endpoint_t *e=endpoint(pid,fd);if(!e)return -1;clear(out,32);out[0]=1;out[1]=e->type;
    if(pty_of(e))return pty_old_info(pid,e,out);
    if(e->type==END_SERIAL){if(e->generation!=serial_session)return -5;out[2]=80;out[3]=24;out[4]=3;}
    else if(e->type==END_TTY){int columns,rows;if(e->generation!=task_generation(e->owner) || wm_terminal_size(e->owner,e->window,&columns,&rows)<0)return -8;
        out[2]=(u32)columns;out[3]=(u32)rows;out[4]=3;out[5]=(u32)e->window;}
    return 0;
}
int streams_terminal_clear(int pid,int fd)
{
    endpoint_t *e=endpoint(pid,fd);if(!e || !(STREAM_CONTEXT(pid).fds[fd].direction&STREAM_WRITE))return -1;e->utf8_used=0;
    if(pty_of(e))return pty_clear(pid,e);
    if(e->type==END_SERIAL){if(e->generation!=serial_session)return -5;int result=management_console_write_atomic("\033[2J\033[H",7);return result==7?0:result;}
    if(e->type!=END_TTY || e->generation!=task_generation(e->owner))return -1;return wm_terminal_control(e->owner,e->window,1);
}
int streams_serial_attach(int pid,u32 session)
{
    if(!session || !auth_can_mod(pid))return -5;
    int index=allocate_endpoint();if(index<0)return index;
    streams_serial_reset();serial_session=session;ENDPOINT(index).type=END_SERIAL;ENDPOINT(index).generation=session;
    serial_endpoint=index;
    attach(pid,0,index,STREAM_READ);attach(pid,1,index,STREAM_WRITE);attach(pid,2,index,STREAM_WRITE);return 0;
}
void streams_serial_reset(void)
{
    endpoint_t *e=objpool_get(&endpoint_pool,serial_endpoint);if(e)events_changed(e);
    serial_head=serial_tail=serial_session=0;serial_endpoint=-1;
}
void streams_serial_writable(void)
{endpoint_t *e=objpool_get(&endpoint_pool,serial_endpoint);if(e)events_changed(e);}
void streams_login_terminal(int parent,int child)
{
    /* 认证成功后可以共享终端连接，但不继承另一身份打开的私密文件或
     * 写入事务。SERIAL终端是连接能力；SYSTEM权限仍由独立realm核对。 */
    for(int fd=0;fd<3;fd++){
        endpoint_t *e=endpoint(parent,fd);if(!e)continue;
        if(e->type!=END_SERIAL && e->type!=END_TTY && e->type!=END_PIPE && e->type!=END_NULL && e->type!=END_PTY_SLAVE)continue;
        if(e->type==END_PTY_SLAVE && !pty_access(child,pty_of(e)))continue;
        descriptor_t d=STREAM_CONTEXT(parent).fds[fd];attach(child,fd,d.endpoint,d.direction);
    }
    if(session_task(parent)==session_task(child) && STREAM_CONTEXT(parent).control_terminal.endpoint>=0)
        pty_task_attach(child,STREAM_CONTEXT(parent).control_terminal.endpoint);
}
int streams_serial_input(const void *data,u32 length)
{
    if(!serial_session || length>4096-(serial_head-serial_tail))return STREAM_AGAIN;
    endpoint_t *terminal=objpool_get(&endpoint_pool,serial_endpoint);
    for(u32 i=0;i<length;i++){
        u8 byte=((const u8 *)data)[i];
        if(terminal && terminal->controls && byte==3){terminal->event++;continue;}
        if(terminal && terminal->controls && byte==4){terminal->eof++;continue;}
        serial_input[serial_head++&4095]=byte;
    }if(terminal && length)events_changed(terminal);return (int)length;
}
void streams_terminal_input(int owner,int handle)
{
    if(!task_owned(owner))return;
    for(int index=STREAM_CONTEXT(owner).terminal_head;index>=0;index=ENDPOINT(index).owner_next)
        if(ENDPOINT(index).window==handle)events_changed(&ENDPOINT(index));
}
void streams_terminal_closed(int owner,int handle)
{
    if(!task_owned(owner))return;
    int index=STREAM_CONTEXT(owner).terminal_head;
    while(index>=0){endpoint_t *e=&ENDPOINT(index);index=e->owner_next;
        if(e->window==handle){terminal_unlink(e);events_changed(e);}}
}
void streams_poll(void)
{
    /* 一个流可以被很多子进程继承。每批最多64订阅节点、每端点最多8个，
     * 未完端点轮到链尾；不在逐字节写入/IRQ中扫描全任务或全订阅者。
     * 新状态在本波途中到达会再走一波，避免已唤醒者重睡后漏掉EOF。
     * descriptor撤销会修正游标，任务旁表不能在断开引用前回收。 */
    u32 budget=64;
    while(event_head>=0 && budget){
        endpoint_t *e=&ENDPOINT(event_head);
        if(e->type==END_TTY && e->generation && task_generation(e->owner)==e->generation
            && wm_terminal_owned(e->owner,e->window))tty_poll(e);
        u32 portion=8;
        while(e->event_cursor && portion-- && budget){
            descriptor_t *d=e->event_cursor;e->event_cursor=d->next;budget--;
            task_notify_generation(d->owner,d->generation,TASK_EVENT_STREAM);
        }
        if(!e->event_cursor && e->event_walk_epoch!=e->event_epoch){
            e->event_cursor=e->members;e->event_walk_epoch=e->event_epoch;
        }
        events_remove(e);if(e->event_cursor)events_append(e);
    }
    if(event_head>=0)task_kernel_wake();
}
int streams_ready(int pid,int fd,u32 out[8])
{
    endpoint_t *e=endpoint(pid,fd);if(!e)return -1;clear(out,32);out[0]=1;out[1]=e->type;
    u32 readable=0,writable=0;int error=0;u32 eof=0;
    if(e->type==END_PIPE){pipe_t *p=&PIPE(e->pipe);readable=p->head-p->tail;writable=PIPE_SIZE-readable;
        eof=!p->writers;if(!p->readers)error=-7;}
    else if(pty_of(e))pty_ready(pid,e,&readable,&writable,&eof,&error);
    else if(e->type==END_TTY){
        if(!e->generation || e->generation!=task_generation(e->owner) || !wm_terminal_owned(e->owner,e->window))error=-8;
        else{tty_poll(e);readable=e->key_head-e->key_tail;writable=256;eof=e->eof!=0;}
    }else if(e->type==END_SERIAL){if(!serial_session || e->generation!=serial_session)error=-5;
        else{readable=serial_head-serial_tail;writable=management_console_capacity();eof=e->eof!=0;}}
    else if(e->type==END_MEMORY){readable=e->bytes-e->position;eof=!readable;}
    else if(e->type==END_NULL){eof=1;writable=65536;}
    else if(e->type==END_FILE){u32 info[8];if(!fs_access(pid,e->path,FS_ACCESS_READ))error=-5;
        else if(fs_metadata(e->path,info) || info[7]!=e->generation)error=-8;
        else{readable=info[2]>e->position?info[2]-e->position:0;eof=!readable;}}
    else if(e->type==END_REPLACE)writable=1;
    u32 direction=STREAM_CONTEXT(pid).fds[fd].direction;
    out[2]=(direction&STREAM_READ)!=0 && (readable || eof || error);
    out[3]=(direction&STREAM_WRITE)!=0 && (writable || error);
    out[4]=(u32)error;out[5]=eof;out[6]=readable;out[7]=e->event;return 0;
}
void streams_set_arguments(int pid,const char *text)
{
    u32 n=0;while(text[n] && n<1023){STREAM_CONTEXT(pid).arguments[n]=text[n];n++;}STREAM_CONTEXT(pid).arguments[n]=0;
    /* 旧GETARGS有显式容量、既有task_t仍128B；保留历史视图。 */
    for(u32 i=0;i<128;i++){TASK(pid).args[i]=i<n && i<127?text[i]:0;}
}
int streams_arguments(int pid,char *out,u32 capacity)
{
    u32 n=size(STREAM_CONTEXT(pid).arguments);if(capacity<=n)return -1;
    for(u32 i=0;i<=n;i++)out[i]=STREAM_CONTEXT(pid).arguments[i];return (int)n;
}
int streams_pipe_descriptors(int parent,const i32 fds[3])
{
    for(int i=0;i<3;i++){endpoint_t *e=endpoint(parent,fds[i]);
        if(!e || e->type!=END_PIPE || !(STREAM_CONTEXT(parent).fds[fds[i]].direction&(i?STREAM_WRITE:STREAM_READ)))return 0;
    }return 1;
}
static int streams_execute(int parent,const char *command,const descriptor_t targets[3],u32 flags)
{
    if(flags&~1u)return -1; /* bit0=脱离终端，默认建立可等待作业 */
    if(!task_owned(parent) || job_counter==0x7FFFFFFFu)return -6;
    char file[64],resolved[64];u32 n=0;while(*command==' ')command++;
    while(command[n] && command[n]!=' '){if(n==63)return -1;file[n]=command[n];n++;}file[n]=0;
    const char *args=command+n;while(*args==' ')args++;if(size(args)>1023)return -1;
    /* PATH按任务环境查找，绝对/相对路径尊重cwd。最终执行权仍由
     * task_exec_scx以真实身份检查；文件所有者不会成为执行身份。 */
    if(!n)return -1;
    int slash=0;for(u32 i=0;i<n;i++)if(file[i]=='/')slash=1;
    if(slash){if(userspace_resolve(parent,file,resolved,sizeof(resolved))<0)return -1;}
    else{
        char paths[256];if(userspace_query(parent,4,"PATH",paths,sizeof(paths))<0)return -1;
        u32 at=0;int found=0;u32 info[2];
        while(paths[at]){
            u32 k=0;while(paths[at] && paths[at]!=':'){if(k==63)return -1;resolved[k++]=paths[at++];}
            if(paths[at]==':')at++;if(k && resolved[k-1]!='/')resolved[k++]='/';
            if(k+n+4>63)return -1;for(u32 j=0;j<n;j++)resolved[k++]=file[j];
            resolved[k]=0;
            if(!fs_stat(resolved,info) && info[0]==1 && fs_access(parent,resolved,FS_ACCESS_READ)){found=1;break;}
            resolved[k++]='.';resolved[k++]='s';resolved[k++]='c';resolved[k++]='x';resolved[k]=0;
            if(!fs_stat(resolved,info) && info[0]==1 && fs_access(parent,resolved,FS_ACCESS_READ)){found=1;break;}
        }
        if(!found)return -2;
    }
    /* 票据就是稀疏索引，不再线性查找64项。先留作业对象再spawn，
     * 内存不足不留下无法wait/取消的孩子；失败释放对象，票据不重复。 */
    int token=(int)(job_counter+1);if(objpool_claim(&job_pool,token))return -6;job_counter++;
    job_t *job=objpool_get(&job_pool,token);
    int child=task_exec_scx(resolved);if(child<1){objpool_release(&job_pool,token);return child;}
    streams_set_arguments(child,args);
    for(int i=0;i<3;i++){
        streams_close(child,i,0);
        if(!(flags&1) && targets[i].endpoint>=0)attach(child,i,targets[i].endpoint,targets[i].direction);
    }
    *job=(job_t){(u32)token,task_generation(parent),task_generation(child),parent,child,0,child,-1,STREAM_CONTEXT(parent).jobs_head};
    if(job->parent_next>=0)((job_t *)objpool_get(&job_pool,job->parent_next))->parent_prev=token;
    STREAM_CONTEXT(parent).jobs_head=token;STREAM_CONTEXT(child).owned_job=token;return token;
}
int streams_exec(int parent,const char *command,const i32 fds[3],u32 flags)
{
    if(flags&~1u || !task_owned(parent))return -1;
    descriptor_t targets[3];clear(targets,sizeof(targets));
    for(int i=0;i<3;i++){targets[i].endpoint=-1;
        if(fds[i]>=0){if(!endpoint(parent,fds[i]))return -1;targets[i]=STREAM_CONTEXT(parent).fds[fds[i]];}}
    return streams_execute(parent,command,targets,flags);
}
#include "streams_launch.inc"

int streams_wait(int parent,u32 token,u32 out[8])
{
    job_t *j=owned_job(parent,token);if(j){
        clear(out,32);out[0]=1;out[1]=j->child?(TASK(j->child).state==3?2:1):0;
        out[2]=(u32)j->result;out[3]=(u32)j->child;out[4]=j->child_generation;
        if(!j->child)release_job(j);return (int)out[1];
    }
    return -1;
}
int streams_job_snapshot(int parent,u32 token,u32 out[8])
{
    /* 只看状态，不领取退出码。jobs/$!不可让后面的wait失去票据；
     * 原始PID只供显示/匹配，结束仍需核对代数，不能用于陈旧kill。 */
    job_t *j=owned_job(parent,token);if(j){
        clear(out,32);out[0]=2;out[1]=j->child?(TASK(j->child).state==3?2:1):0;out[2]=(u32)j->result;
        out[3]=(u32)j->child;out[4]=j->child_generation;out[5]=(u32)j->original_child;return (int)out[1];}return -1;
}
int streams_exec_buffer(int parent,const void *script,u32 bytes,const i32 fds[3],const char *args)
{
    const char *prefix="/BIN/SH.SCX --buffer3 ";char command[1024];u32 used=size(prefix),n=args?size(args):0;
    if(n+used>=sizeof(command))return -1;for(u32 i=0;i<used;i++)command[i]=prefix[i];for(u32 i=0;i<=n;i++)command[used+i]=args?args[i]:0;
    int fd=streams_memory(parent,script,bytes);if(fd<0)return fd;int job=streams_exec(parent,command,fds,0);
    if(job>0){u32 info[8];if(streams_wait(parent,(u32)job,info)>0 && info[3]){
            descriptor_t d=STREAM_CONTEXT(parent).fds[fd];attach((int)info[3],3,d.endpoint,STREAM_READ);
        }else job=-1;}
    streams_close(parent,fd,0);return job;
}
int streams_kill(int caller,int target)
{
    if(target<1 || target>=NTASK || !TASK(target).state || TASK(target).state==2)return -1;
    if(auth_uid(target)==AUTH_SYSTEM && !auth_can_mod(caller))return -5;
    if(auth_uid(caller)!=auth_uid(target) && !auth_can_manage(caller))return -5;
    process_exit(target,130);task_stop(target);return 0;
}
int streams_processes(int caller,u32 *out,u32 words)
{
    if(words<8+LEGACY_TASK_ROWS2*16)return -1;clear(out,(8+LEGACY_TASK_ROWS2*16)*4);out[0]=1;out[1]=LEGACY_TASK_ROWS2;out[2]=16;
    for(int i=0;i<LEGACY_TASK_ROWS2;i++){
        u32 *row=out+8+i*16;row[0]=(u32)i;row[1]=(u32)TASK(i).state;row[2]=task_generation(i);
        row[3]=(u32)auth_uid(i);row[4]=(u32)auth_gid(i);
        if(i==caller || auth_can_manage(caller) || auth_uid(i)==auth_uid(caller))
            for(u32 j=0;j<12;j++)((char *)(row+8))[j]=TASK(i).name[j];
    }
    return 0;
}

/* native标准流引用与旧slot分离；take明确转移引用，不在fd0/1/2留
 * 一个隐形writer令fclose(stdout)后对端永远等不到EOF。未调用take的
 * 旧程序仍使用原来的16fd、重定向及owner/generation合同。 */
static native_reference_t *native_reference(int pid,u32 token)
{
    if(!task_owned(pid) || !token || token>0x7fffffffu)return 0;
    native_reference_t *r=objpool_get(&native_pool,(int)token);
    return r && r->token==token && r->descriptor.owner==pid
        && r->descriptor.generation==task_generation(pid)?r:0;
}
static native_reference_t *native_allocate(int pid)
{
    if(native_counter==0x7fffffffu)return 0;
    u32 token=native_counter+1;if(objpool_claim(&native_pool,(int)token))return 0;
    native_reference_t *r=objpool_get(&native_pool,(int)token);r->token=++native_counter;
    r->descriptor.endpoint=-1;r->descriptor.owner=pid;r->descriptor.generation=task_generation(pid);r->standard=-1;
    r->previous=-1;r->next=STREAM_CONTEXT(pid).native_head;
    if(r->next>=0)((native_reference_t *)objpool_get(&native_pool,r->next))->previous=(int)token;
    STREAM_CONTEXT(pid).native_head=(int)token;return r;
}
int streams_native_take(int pid,int fd,u32 *token)
{
    endpoint_t *e=fd>=0 && fd<=2?endpoint(pid,fd):0;if(!e)return -1;
    if(e->refs==0xffffffffu)return -4;
    native_reference_t *r=native_allocate(pid);if(!r)return -4;
    descriptor_t *d=&STREAM_CONTEXT(pid).fds[fd];r->descriptor=*d;r->descriptor.weak=0;r->descriptor.controls=0;
    r->standard=fd;STREAM_CONTEXT(pid).standard_refs[fd]++;
    /* 先新引用再撤旧引用，不能在唯一ref的过渡时销毁endpoint。
     * 控制订阅一并转移；dup只增加字节端点，不复制Ctrl-C订阅。 */
    if(d->weak){reference(&r->descriptor,1);}
    else{
        u32 controls=d->controls;d->controls=0;
        reference(&r->descriptor,1);reference(d,0);
        *d=(descriptor_t){r->descriptor.endpoint,r->descriptor.direction,controls,pid,task_generation(pid),0,0,1};
    }
    *token=r->token;return 0;
}
int streams_native_duplicate(int pid,u32 token,u32 *copy_token)
{
    native_reference_t *original=native_reference(pid,token);if(!original)return -8;
    endpoint_t *e=objpool_get(&endpoint_pool,original->descriptor.endpoint);if(!e || !e->refs)return -8;
    if(e->refs==0xffffffffu)return -4;
    native_reference_t *r=native_allocate(pid);if(!r)return -4;
    r->descriptor.endpoint=original->descriptor.endpoint;r->descriptor.direction=original->descriptor.direction;
    r->descriptor.controls=0;r->standard=original->standard;
    if(r->standard>=0)STREAM_CONTEXT(pid).standard_refs[r->standard]++;
    reference(&r->descriptor,1);*copy_token=r->token;return 0;
}
int streams_native_close(int pid,u32 token)
{
    native_reference_t *r=native_reference(pid,token);if(!r)return -8;
    if(r->standard>=0 && STREAM_CONTEXT(pid).standard_refs[r->standard]){
        if(!--STREAM_CONTEXT(pid).standard_refs[r->standard]){
            descriptor_t *proxy=&STREAM_CONTEXT(pid).fds[r->standard];
            if(proxy->weak && proxy->endpoint==r->descriptor.endpoint){reference(proxy,0);*proxy=(descriptor_t){-1,0};}
        }
    }
    if(r->previous>=0)((native_reference_t *)objpool_get(&native_pool,r->previous))->next=r->next;
    else STREAM_CONTEXT(pid).native_head=r->next;
    if(r->next>=0)((native_reference_t *)objpool_get(&native_pool,r->next))->previous=r->previous;
    reference(&r->descriptor,0);objpool_release(&native_pool,(int)token);return 0;
}
static endpoint_t *native_endpoint(native_reference_t *r)
{
    endpoint_t *e=r?objpool_get(&endpoint_pool,r->descriptor.endpoint):0;
    return e && e->refs?e:0;
}
int streams_native_read(int pid,u32 token,void *data,u32 bytes)
{
    native_reference_t *r=native_reference(pid,token);endpoint_t *e=native_endpoint(r);if(!e)return -8;
    if(bytes>65536u || !(r->descriptor.direction&STREAM_READ))return -1;
    /* 新seek允许二进制stdin的EOF外位置；旧read分支原字节逻辑
     * 仍保留，新视图在EOF处返回0，不让无符号剩余长度下溢。 */
    if(e->type==END_MEMORY && e->position>=e->bytes)return 0;
    if(e->type==END_FILE){u32 info[8];if(!fs_access(pid,e->path,FS_ACCESS_READ))return -5;
        if(fs_metadata(e->path,info) || info[7]!=e->generation)return -8;if(e->position>=info[2])return 0;}
    return endpoint_read(pid,e,r->descriptor.direction,data,bytes);
}
int streams_native_write(int pid,u32 token,const void *data,u32 bytes)
{
    native_reference_t *r=native_reference(pid,token);endpoint_t *e=native_endpoint(r);if(!e)return -8;
    return endpoint_write(pid,e,r->descriptor.direction,data,bytes);
}
int streams_native_query(int pid,u32 token,u32 out[8])
{
    native_reference_t *r=native_reference(pid,token);endpoint_t *e=native_endpoint(r);if(!e)return -8;
    clear(out,32);out[0]=1;out[1]=e->type;out[2]=e->position;out[4]=e->generation;out[5]=r->descriptor.direction;
    if(e->type==END_FILE){u32 info[8];if(!fs_access(pid,e->path,FS_ACCESS_READ))return -5;
        if(fs_metadata(e->path,info) || info[7]!=e->generation)return -8;out[3]=info[2];out[6]=3;}
    else if(e->type==END_MEMORY){out[3]=e->bytes;out[6]=3;}
    else if(e->type==END_REPLACE){
        if(auth_uid(pid)!=auth_uid(e->owner))return -5;
        u32 info[4];int error=fs_stream_snapshot(e->owner,e->token,info);if(error)return error;
        out[2]=out[3]=info[0];out[4]=info[2];out[6]=2|8;out[7]=info[3]?(u32)-1:0;
    }
    else if(pty_of(e)){if(!pty_access(pid,pty_of(e)))return -5;}
    else if(e->type==END_TTY){if(e->generation!=task_generation(e->owner) || !wm_terminal_owned(e->owner,e->window))return -8;}
    else if(e->type==END_SERIAL){if(e->generation!=serial_session)return -5;}
    out[6]|=4;
    return 0;
}
int streams_native_seek(int pid,u32 token,u32 offset)
{
    native_reference_t *r=native_reference(pid,token);endpoint_t *e=native_endpoint(r);if(!e)return -8;
    u32 info[8];int error=streams_native_query(pid,token,info);if(error)return error;
    if(!(info[6]&1u))return -12;e->position=offset;return 0;
}
int streams_native_flush(int pid,u32 token)
{
    native_reference_t *r=native_reference(pid,token);endpoint_t *e=native_endpoint(r);if(!e)return -8;
    u32 snapshot[8];int checked=streams_native_query(pid,token,snapshot);if(checked)return checked;
    if(e->type==END_REPLACE){
        /* Shell父事务的目录发布仍由父commit拥有；新stdio只保证
         * 缓冲已送端点并刷新ATA，不替child取得父事务管理权。 */
        if(auth_uid(pid)!=auth_uid(e->owner))return -5;
        u32 info[4];int error=fs_stream_snapshot(e->owner,e->token,info);if(error)return error;
        if(info[3])return -1;return ata_flush()?-1:0;
    }
    if(e->type==END_FILE)return ata_flush()?-1:0;
    return 0;
}

#include "streams_capture.inc"

#include "streams_pty_native.inc"
