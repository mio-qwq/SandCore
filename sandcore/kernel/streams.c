#include "streams.h"
#include "task.h"
#include "auth.h"
#include "userspace.h"
#include "fs.h"
#include "process.h"
#include "wm.h"
#include "management.h"
#include "memory.h"
#include "interrupts.h"
#define FD_MAX 16
#define END_MAX 128
#define PIPE_MAX 16
#define PIPE_SIZE 4096u
#define JOB_MAX 64
enum { END_FILE=1,END_REPLACE,END_PIPE,END_SERIAL,END_TTY,END_NULL,END_MEMORY };
typedef struct {
    u32 refs,type,position,generation,token,controls,event,eof,key_head,key_tail;
    u8 keys[256];
    int owner,pipe,window;char path[64];
    u32 memory,bytes,pages;
    u8 utf8[4];u32 utf8_used,utf8_needed,utf8_writer,utf8_generation;
} endpoint_t;
typedef struct { int endpoint;u32 direction,controls; } descriptor_t;
typedef struct { u8 bytes[PIPE_SIZE];u32 head,tail,readers,writers; } pipe_t;
typedef struct { u32 ticket,parent_generation,child_generation;int parent,child,result,original_child; } job_t;
typedef struct {
    descriptor_t descriptors[NTASK][FD_MAX];endpoint_t endpoints[END_MAX];
    pipe_t pipes[PIPE_MAX];job_t jobs[JOB_MAX];char arguments[NTASK][1024];
} stream_runtime_t;
static stream_runtime_t *runtime;
#define descriptors (runtime->descriptors)
#define endpoints (runtime->endpoints)
#define pipes (runtime->pipes)
#define jobs (runtime->jobs)
#define arguments (runtime->arguments)
static u32 job_counter;
static u8 serial_input[4096];
static u32 serial_head,serial_tail,serial_session;
static u32 size(const char *s){u32 n=0;while(s[n])n++;return n;}
static void clear(void *p,u32 n){for(u32 i=0;i<n;i++)((u8 *)p)[i]=0;}
void streams_init(void)
{
    /* 管道正文/长参数不塞进低端BSS；一次分配，任务间共享端点只改
     * 引用计数，热路径不反复申请整块内存。 */
    u32 pages=(sizeof(*runtime)+4095)/4096;runtime=(stream_runtime_t *)pframe_alloc_run(pages);
    if(!runtime)panic("STREAM MEMORY",0);clear(runtime,pages*4096);
    for(int pid=0;pid<NTASK;pid++)for(int fd=0;fd<FD_MAX;fd++)descriptors[pid][fd].endpoint=-1;
}
static endpoint_t *endpoint(int pid,int fd)
{
    if(pid<1 || pid>=NTASK || fd<0 || fd>=FD_MAX)return 0;
    int index=descriptors[pid][fd].endpoint;
    return index>=0 && index<END_MAX && endpoints[index].refs?&endpoints[index]:0;
}
static int vacant(int pid,int first)
{for(int fd=first;fd<FD_MAX;fd++)if(descriptors[pid][fd].endpoint<0)return fd;return -4;}
static int allocate_endpoint(void)
{for(int i=0;i<END_MAX;i++)if(!endpoints[i].refs){clear(&endpoints[i],sizeof(endpoint_t));return i;}return -4;}
static void reference(descriptor_t d,int add)
{
    if(d.endpoint<0)return;
    endpoint_t *e=&endpoints[d.endpoint];
    if(!add && d.controls)e->controls-=d.controls;
    if(add)e->refs++;else e->refs--;
    if(e->type==END_PIPE){
        pipe_t *p=&pipes[e->pipe];
        if(d.direction&STREAM_READ){if(add)p->readers++;else p->readers--;}
        if(d.direction&STREAM_WRITE){if(add)p->writers++;else p->writers--;}
    }
    if(!e->refs && e->type==END_REPLACE && e->token)fs_stream_abort(e->owner,e->token);
    if(!e->refs && e->type==END_MEMORY && e->pages){pframe_free_run(e->memory,e->pages);e->memory=e->bytes=e->pages=0;}
}
static void attach(int pid,int fd,int index,u32 direction)
{
    descriptor_t *d=&descriptors[pid][fd];
    if(d->endpoint>=0)reference(*d,0);
    d->endpoint=index;d->direction=direction;d->controls=0;reference(*d,1);
}
void streams_spawn(int pid,int parent)
{
    for(int fd=0;fd<FD_MAX;fd++)descriptors[pid][fd]=(descriptor_t){-1,0};
    arguments[pid][0]=0;
    if(parent>0 && parent<NTASK && auth_uid(pid)==auth_uid(parent))
        for(int fd=0;fd<3;fd++)if(endpoint(parent,fd)){
            descriptors[pid][fd]=descriptors[parent][fd];descriptors[pid][fd].controls=0;reference(descriptors[pid][fd],1);
        }
}
void streams_stop(int pid,int code)
{
    for(int fd=0;fd<FD_MAX;fd++)streams_close(pid,fd,0);
    fs_stream_stop(pid);
    for(int i=0;i<JOB_MAX;i++){
        job_t *j=&jobs[i];
        if(j->ticket && j->child==pid && j->child_generation==task_generation(pid)){j->child=0;j->result=code;}
        if(j->ticket && j->parent==pid && j->parent_generation==task_generation(pid)){
            int child=j->child;u32 generation=j->child_generation;j->ticket=0;
            if(child>0 && generation==task_generation(child) && tasks[child].state && tasks[child].state!=2){
                process_exit(child,130);task_stop(child);}
        }
    }
    arguments[pid][0]=0;
}
int streams_open(int pid,const char *path,u32 mode,u32 capacity)
{
    if((mode!=STREAM_READ && mode!=STREAM_WRITE && mode!=STREAM_NULL && mode!=STREAM_TERMINAL) || !path)return -1;
    if(mode==STREAM_TERMINAL){int fd=vacant(pid,3);if(fd<0)return fd;
        for(int source=0;source<3;source++){endpoint_t *e=endpoint(pid,source);if(e && (e->type==END_TTY || e->type==END_SERIAL)){
                descriptor_t d=descriptors[pid][source];attach(pid,fd,d.endpoint,STREAM_READ|STREAM_WRITE);return fd;}}return -1;}
    int fd=vacant(pid,3),index=allocate_endpoint();if(fd<0 || index<0)return -4;
    endpoint_t *e=&endpoints[index];e->owner=pid;
    if(mode==STREAM_NULL){e->type=END_NULL;attach(pid,fd,index,STREAM_READ|STREAM_WRITE);return fd;}
    if(userspace_resolve(pid,path,e->path,sizeof(e->path))<0)return -1;
    if(mode==STREAM_READ){
        u32 info[8];if(!fs_access(pid,e->path,FS_ACCESS_READ))return -5;
        if(fs_metadata(e->path,info) || info[1]!=1)return -1;
        e->type=END_FILE;e->generation=info[7];
    }else{
        int token=fs_stream_begin(pid,e->path,capacity);if(token<0)return token;
        e->type=END_REPLACE;e->token=(u32)token;
    }
    attach(pid,fd,index,mode);return fd;
}
int streams_close(int pid,int fd,int commit)
{
    endpoint_t *e=endpoint(pid,fd);if(!e)return -1;
    if(commit && e->type==END_REPLACE && e->token){
        if(e->owner!=pid)return -5;
        int result=fs_stream_commit(pid,e->token);if(result<0)return result;e->token=0;
    }
    descriptor_t d=descriptors[pid][fd];descriptors[pid][fd]=(descriptor_t){-1,0};reference(d,0);return 0;
}
int streams_dup(int pid,int from,int to)
{
    if(!endpoint(pid,from) || to<0 || to>=FD_MAX)return -1;
    if(from==to)return to;
    descriptor_t d=descriptors[pid][from];attach(pid,to,d.endpoint,d.direction);return to;
}
int streams_pipe(int pid,int out[2])
{
    int a=vacant(pid,3);if(a<0)return -4;
    int b=-1;for(int i=a+1;i<FD_MAX;i++)if(descriptors[pid][i].endpoint<0){b=i;break;}
    int index=allocate_endpoint(),slot=-1;
    for(int i=0;i<PIPE_MAX;i++)if(!pipes[i].readers && !pipes[i].writers){slot=i;break;}
    if(b<0 || index<0 || slot<0)return -4;
    clear(&pipes[slot],sizeof(pipe_t));endpoints[index].type=END_PIPE;endpoints[index].pipe=slot;
    attach(pid,a,index,STREAM_READ);attach(pid,b,index,STREAM_WRITE);out[0]=a;out[1]=b;return 0;
}
int streams_memory(int pid,const void *data,u32 bytes)
{
    if(bytes>32768)return -1;int fd=vacant(pid,3),index=allocate_endpoint();if(fd<0 || index<0)return -4;
    u32 pages=(bytes+4095)/4096,base=pages?pframe_alloc_run(pages):0;if(pages && !base)return -4;
    endpoint_t *e=&endpoints[index];e->type=END_MEMORY;e->owner=pid;e->memory=base;e->pages=pages;e->bytes=bytes;
    /* 复制的是调用者已经验证的三环缓冲；此后不可写，最后引用才回收。
     * here-document不占磁盘、不假借SYSTEM文件，也不会提前写满管道。 */
    for(u32 i=0;i<bytes;i++)((u8 *)base)[i]=((const u8 *)data)[i];attach(pid,fd,index,STREAM_READ);return fd;
}
static void tty_poll(endpoint_t *e)
{
    for(u32 budget=16;budget && e->key_head-e->key_tail<256;budget--){
        int key=wm_user_getkey(e->owner);if(key<0)break;
        if(key==3 && e->controls){e->event++;continue;}
        if(key==4 && e->controls){e->eof++;continue;}
        e->keys[e->key_head++&255]=(u8)key;
    }
}
int streams_event(int pid,int fd,u32 action)
{
    endpoint_t *e=endpoint(pid,fd);if(!e || (e->type!=END_TTY && e->type!=END_SERIAL) || action>2)return -1;
    descriptor_t *d=&descriptors[pid][fd];
    if(action==1){if(d->controls==65535)return -4;d->controls++;e->controls++;}
    else if(action==2){if(!d->controls)return -1;d->controls--;e->controls--;}
    if(e->type==END_TTY)tty_poll(e);return (int)(e->event&0x7FFFFFFFu);
}
int streams_read(int pid,int fd,void *data,u32 length)
{
    endpoint_t *e=endpoint(pid,fd);if(!e || !(descriptors[pid][fd].direction&STREAM_READ) || length>65536)return -1;
    if(!length)return 0;u8 *out=(u8 *)data;
    if(e->type==END_NULL)return 0;
    if(e->type==END_MEMORY){u32 count=e->bytes-e->position;if(count>length)count=length;
        for(u32 i=0;i<count;i++)out[i]=((const u8 *)e->memory)[e->position+i];e->position+=count;return (int)count;}
    if(e->type==END_FILE){
        u32 info[8];if(!fs_access(pid,e->path,FS_ACCESS_READ))return -5;
        if(fs_metadata(e->path,info) || info[7]!=e->generation)return -8;
        int n=fs_read_at(e->path,data,length,e->position);if(n>0)e->position+=(u32)n;return n;
    }
    if(e->type==END_PIPE){
        pipe_t *p=&pipes[e->pipe];u32 count=p->head-p->tail;if(count>length)count=length;
        for(u32 i=0;i<count;i++)out[i]=p->bytes[(p->tail+i)&(PIPE_SIZE-1)];p->tail+=count;
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
int streams_write(int pid,int fd,const void *data,u32 length)
{
    endpoint_t *e=endpoint(pid,fd);if(!e || !(descriptors[pid][fd].direction&STREAM_WRITE) || length>65536)return -1;
    const u8 *in=(const u8 *)data;if(!length)return 0;
    if(e->type==END_NULL)return (int)length;
    if(e->type==END_REPLACE){
        /* 父Shell保留输出事务，子任务只能顺序写正文；commit仍只认
         * 原owner。每次按子任务真实身份再次检查，不能跨用户继承写权。 */
        if(auth_uid(pid)!=auth_uid(e->owner) || !e->token || !fs_access(pid,e->path,FS_ACCESS_WRITE)
            || !fs_user_mutable(e->path,0))return -5;
        return fs_stream_write(e->owner,e->token,data,length);
    }
    if(e->type==END_PIPE){
        pipe_t *p=&pipes[e->pipe];if(!p->readers)return -7;
        u32 count=PIPE_SIZE-(p->head-p->tail);if(count>length)count=length;
        for(u32 i=0;i<count;i++)p->bytes[(p->head+i)&(PIPE_SIZE-1)]=in[i];p->head+=count;
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
int streams_seek(int pid,int fd,u32 offset)
{
    endpoint_t *e=endpoint(pid,fd);if(!e)return -1;
    if(e->type==END_MEMORY){if(offset>e->bytes)return -1;e->position=offset;return (int)offset;}if(e->type!=END_FILE)return -1;
    u32 info[8];if(!fs_access(pid,e->path,FS_ACCESS_READ))return -5;
    if(fs_metadata(e->path,info) || info[7]!=e->generation || offset>info[2])return -8;
    e->position=offset;return (int)offset;
}
int streams_tty(int pid,int window)
{
    if(!wm_terminal_owned(pid,window))return -5;
    int index=allocate_endpoint();if(index<0)return index;
    endpoints[index].type=END_TTY;endpoints[index].owner=pid;endpoints[index].window=window;
    endpoints[index].generation=task_generation(pid);
    attach(pid,0,index,STREAM_READ);attach(pid,1,index,STREAM_WRITE);attach(pid,2,index,STREAM_WRITE);return 0;
}
int streams_terminal_info(int pid,int fd,u32 out[8])
{
    endpoint_t *e=endpoint(pid,fd);if(!e)return -1;clear(out,32);out[0]=1;out[1]=e->type;
    if(e->type==END_SERIAL){if(e->generation!=serial_session)return -5;out[2]=80;out[3]=24;out[4]=3;}
    else if(e->type==END_TTY){int columns,rows;if(e->generation!=task_generation(e->owner) || wm_terminal_size(e->owner,e->window,&columns,&rows)<0)return -8;
        out[2]=(u32)columns;out[3]=(u32)rows;out[4]=3;out[5]=(u32)e->window;}
    return 0;
}
int streams_terminal_clear(int pid,int fd)
{
    endpoint_t *e=endpoint(pid,fd);if(!e || !(descriptors[pid][fd].direction&STREAM_WRITE))return -1;e->utf8_used=0;
    if(e->type==END_SERIAL){if(e->generation!=serial_session)return -5;int result=management_console_write_atomic("\033[2J\033[H",7);return result==7?0:result;}
    if(e->type!=END_TTY || e->generation!=task_generation(e->owner))return -1;return wm_terminal_control(e->owner,e->window,1);
}
int streams_serial_attach(int pid,u32 session)
{
    if(!session || !auth_can_mod(pid))return -5;
    int index=allocate_endpoint();if(index<0)return index;
    streams_serial_reset();serial_session=session;endpoints[index].type=END_SERIAL;endpoints[index].generation=session;
    attach(pid,0,index,STREAM_READ);attach(pid,1,index,STREAM_WRITE);attach(pid,2,index,STREAM_WRITE);return 0;
}
void streams_serial_reset(void){serial_head=serial_tail=serial_session=0;}
void streams_login_terminal(int parent,int child)
{
    /* 认证成功后可以共享终端连接，但不继承另一身份打开的私密文件或
     * 写入事务。SERIAL终端是连接能力；SYSTEM权限仍由独立realm核对。 */
    for(int fd=0;fd<3;fd++){
        endpoint_t *e=endpoint(parent,fd);if(!e)continue;
        if(e->type!=END_SERIAL && e->type!=END_TTY && e->type!=END_PIPE && e->type!=END_NULL)continue;
        descriptor_t d=descriptors[parent][fd];attach(child,fd,d.endpoint,d.direction);
    }
}
int streams_serial_input(const void *data,u32 length)
{
    if(!serial_session || length>4096-(serial_head-serial_tail))return STREAM_AGAIN;
    endpoint_t *terminal=0;for(int i=0;i<END_MAX;i++)if(endpoints[i].refs && endpoints[i].type==END_SERIAL && endpoints[i].generation==serial_session){terminal=&endpoints[i];break;}
    for(u32 i=0;i<length;i++){
        u8 byte=((const u8 *)data)[i];
        if(terminal && terminal->controls && byte==3){terminal->event++;continue;}
        if(terminal && terminal->controls && byte==4){terminal->eof++;continue;}
        serial_input[serial_head++&4095]=byte;
    }return (int)length;
}
void streams_set_arguments(int pid,const char *text)
{
    u32 n=0;while(text[n] && n<1023){arguments[pid][n]=text[n];n++;}arguments[pid][n]=0;
    /* 旧GETARGS有显式容量、既有task_t仍128B；保留历史视图。 */
    for(u32 i=0;i<128;i++){tasks[pid].args[i]=i<n && i<127?text[i]:0;}
}
int streams_arguments(int pid,char *out,u32 capacity)
{
    u32 n=size(arguments[pid]);if(capacity<=n)return -1;
    for(u32 i=0;i<=n;i++)out[i]=arguments[pid][i];return (int)n;
}
int streams_exec(int parent,const char *command,const i32 fds[3],u32 flags)
{
    if(flags&~1u)return -1; /* bit0=脱离终端，默认建立可等待作业 */
    job_t *job=0;for(int i=0;i<JOB_MAX;i++)if(!jobs[i].ticket){job=&jobs[i];break;}
    if(!job || job_counter==0x7FFFFFFFu)return -6;
    for(int i=0;i<3;i++)if(fds[i]>=0 && !endpoint(parent,fds[i]))return -1;
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
    int child=task_exec_scx(resolved);if(child<1)return child;
    streams_set_arguments(child,args);
    for(int i=0;i<3;i++){
        streams_close(child,i,0);
        if(!(flags&1) && fds[i]>=0){descriptor_t d=descriptors[parent][fds[i]];attach(child,i,d.endpoint,d.direction);}
    }
    *job=(job_t){++job_counter,task_generation(parent),task_generation(child),parent,child,0,child};return (int)job->ticket;
}
int streams_wait(int parent,u32 token,u32 out[8])
{
    for(int i=0;i<JOB_MAX;i++){
        job_t *j=&jobs[i];if(j->ticket!=token || j->parent!=parent || j->parent_generation!=task_generation(parent))continue;
        clear(out,32);out[0]=1;out[1]=j->child?(tasks[j->child].state==3?2:1):0;
        out[2]=(u32)j->result;out[3]=(u32)j->child;out[4]=j->child_generation;
        if(!j->child)j->ticket=0;return (int)out[1];
    }
    return -1;
}
int streams_job_snapshot(int parent,u32 token,u32 out[8])
{
    /* 只看状态，不领取退出码。jobs/$!不可让后面的wait失去票据；
     * 原始PID只供显示/匹配，结束仍需核对代数，不能用于陈旧kill。 */
    for(int i=0;i<JOB_MAX;i++){job_t *j=&jobs[i];if(j->ticket!=token || j->parent!=parent || j->parent_generation!=task_generation(parent))continue;
        clear(out,32);out[0]=2;out[1]=j->child?(tasks[j->child].state==3?2:1):0;out[2]=(u32)j->result;
        out[3]=(u32)j->child;out[4]=j->child_generation;out[5]=(u32)j->original_child;return (int)out[1];}return -1;
}
int streams_exec_buffer(int parent,const void *script,u32 bytes,const i32 fds[3],const char *args)
{
    const char *prefix="/BIN/SH.SCX --buffer3 ";char command[1024];u32 used=size(prefix),n=args?size(args):0;
    if(n+used>=sizeof(command))return -1;for(u32 i=0;i<used;i++)command[i]=prefix[i];for(u32 i=0;i<=n;i++)command[used+i]=args?args[i]:0;
    int fd=streams_memory(parent,script,bytes);if(fd<0)return fd;int job=streams_exec(parent,command,fds,0);
    if(job>0){u32 info[8];if(streams_wait(parent,(u32)job,info)>0 && info[3]){
            descriptor_t d=descriptors[parent][fd];attach((int)info[3],3,d.endpoint,STREAM_READ);
        }else job=-1;}
    streams_close(parent,fd,0);return job;
}
int streams_kill(int caller,int target)
{
    if(target<1 || target>=NTASK || !tasks[target].state || tasks[target].state==2)return -1;
    if(auth_uid(target)==AUTH_SYSTEM && !auth_can_mod(caller))return -5;
    if(auth_uid(caller)!=auth_uid(target) && !auth_can_manage(caller))return -5;
    process_exit(target,130);task_stop(target);return 0;
}
int streams_processes(int caller,u32 *out,u32 words)
{
    if(words<8+NTASK*16)return -1;clear(out,(8+NTASK*16)*4);out[0]=1;out[1]=NTASK;out[2]=16;
    for(int i=0;i<NTASK;i++){
        u32 *row=out+8+i*16;row[0]=(u32)i;row[1]=(u32)tasks[i].state;row[2]=task_generation(i);
        row[3]=(u32)auth_uid(i);row[4]=(u32)auth_gid(i);
        if(i==caller || auth_can_manage(caller) || auth_uid(i)==auth_uid(caller))
            for(u32 j=0;j<12;j++)((char *)(row+8))[j]=tasks[i].name[j];
    }
    return 0;
}
