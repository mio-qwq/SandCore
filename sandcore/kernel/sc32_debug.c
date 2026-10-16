#include "sc32_debug.h"
#include "sc32_core.h"
#include "task.h"
#include "task_store.h"
#include "process.h"
#include "paging.h"
#include "memory.h"
#include "objpool.h"
#include "streams.h"
#include "auth.h"
#include "session.h"
#include "fs.h"
#include "crypto.h"
#include "../user/debugapi/SC32DEBUGWIRE.H"
#include "SCLFMT.H"

typedef struct {
    u32 generation,binding,breaks,modules,pending,break_epoch,module_epoch;
    u32 stepping,auto_continue,reason,loading,mapping_epoch,break_count,module_count;
} d2_state_t;
typedef struct {
    u32 owner,owner_generation,next,id,generation,address,flags,hits;
    u8 original;
} d2_break_t;
typedef struct {
    u32 owner,owner_generation,next,metadata,metadata_pages;
    u32 file,file_pages,source_object,read_at,phase,verify_segment,verify_at,reloc_index;
    u32 input_metadata,input_metadata_bytes,proof_mapping_epoch,proof_reloc_index,proof_reloc_value;
    sha256_t hash;
    ScDebugModule info;
} d2_module_t;
static objpool_t d2_states,d2_breaks,d2_modules;
static int d2_initialized;
static u32 d2_serial;
static void d2_zero(void *p,u32 bytes){u8 *b=p;
    for(u32 i=0;i<bytes;i++)b[i]=0;
}
static void d2_text(char *d,u32 capacity,const char *s)
{u32 n=0;
    while(n+1<capacity && s[n]){d[n]=s[n];
        n++;
    }d[n]=0;
}
static int d2_equal(const char *a,const char *b)
{while(*a && *a==*b){a++;
        b++;
    }return *a==*b;
}
static u32 d2_word(const u8 *p,u32 offset){return scl_read32(p+offset);
}
static int d2_init(void)
{
    if(d2_initialized)return 0;
    if(objpool_init(&d2_states,sizeof(d2_state_t),0)
    || objpool_init(&d2_breaks,sizeof(d2_break_t),0)
    || objpool_init(&d2_modules,sizeof(d2_module_t),0))return SC32_E_MEMORY;
    d2_initialized=1;
    return 0;
}
static u32 d2_next_serial(void)
{if(d2_serial==0xffffffffu)return 0;
    return ++d2_serial;
}
static d2_state_t *d2_state(int pid,int create)
{
    if(d2_init() || !task_owned(pid))return 0;
    d2_state_t *s=objpool_get(&d2_states,pid);
    if(!s && create){if(objpool_claim(&d2_states,pid)<0)return 0;
        s=objpool_get(&d2_states,pid);
        d2_zero(s,sizeof(*s));
        s->generation=task_generation(pid);
        s->break_epoch=s->module_epoch=s->mapping_epoch=1;
    }
    return s && s->generation==task_generation(pid)?s:0;
}
static d2_break_t *d2_break(u32 id)
{return id?objpool_get(&d2_breaks,(int)(id-1)):0;
}
static d2_module_t *d2_module(u32 id)
{return id?objpool_get(&d2_modules,(int)(id-1)):0;
}
static int d2_overlap(u32 a,u32 an,u32 b,u32 bn)
{return a<b+bn && b<a+an;
}
static int d2_publish(int pid,u32 out,const void *data,u32 bytes)
{return paging_user_copy(TASK(pid).pd,out,(void *)data,bytes,1)?SC32_E_INVALID:0;
}
static int d2_ref(int owner,const ScDebugRef *ref,d2_state_t **out)
{
    if(ref->size!=sizeof(*ref) || ref->version!=1 || ref->reserved || !ref->pid
    || ref->pid>0x7fffffffu || !ref->generation || !ref->binding)return SC32_E_INVALID;
    int target=(int)ref->pid;
    if(!task_owned(target) || task_generation(target)!=ref->generation)return SC32_E_STALE;
    /* 真实owner检查由process私有旁表完成；UID/SID再限制新接口，
     * 不能用普通root管理权把不属于自己的进程变成调试目标。 */
    if(!process_debug_authorize(owner,target,ref->generation))return SC32_E_ACCESS;
    if(auth_uid(owner)!=auth_uid(target) || session_task(owner)!=session_task(target))return SC32_E_ACCESS;
    d2_state_t *s=d2_state(target,0);
    if(!s || s->binding!=ref->binding)return SC32_E_STALE;
     *out=s;
    return 0;
}
static int d2_snapshot(int owner,const ScDebugRef *ref,ScDebugSnapshot *out)
{
    d2_state_t *s;
    int error=d2_ref(owner,ref,&s);
    if(error)return error;
    int target=(int)ref->pid;
    d2_zero(out,sizeof(*out));
    out->size=sizeof(*out);
    out->version=1;
    out->target=*ref;
    out->state=(u32)TASK(target).state;
    out->module_epoch=s->module_epoch;
    out->breakpoint_epoch=s->break_epoch;
    out->registers.size=sizeof(out->registers);
    out->registers.version=1;
    if(out->state==0 || out->state==2){out->state=SC32_DEBUG_EXITED;
        out->exit_code=task_record(target)->exit_code;
        return 0;
    }
    if(out->state==3){u32 frame[22];
        if(process_debug(owner,target,DBG_INFO,0,frame,sizeof(frame)))return SC32_E_STALE;
        ScDebugRegisters *r=&out->registers;
        r->eax=frame[11];
        r->ebx=frame[8];
        r->ecx=frame[10];
        r->edx=frame[9];
        r->esi=frame[5];
        r->edi=frame[4];
        r->ebp=frame[6];
        r->esp=frame[17];
        r->eip=frame[14];
        r->eflags=frame[16];
        r->gs=frame[0];
        r->fs=frame[1];
        r->es=frame[2];
        r->ds=frame[3];
        r->cs=frame[15];
        r->ss=frame[18];
        r->vector=frame[12];
        r->error=frame[13];
        r->cr2=frame[20];
        out->stop_generation=frame[21];
        out->flags=SC32_DEBUG_REGISTERS_VALID;
        out->reason=s->reason?s->reason:frame[12]==3?SC32_DEBUG_STOP_BREAKPOINT:
        frame[12]==1?SC32_DEBUG_STOP_STEP:SC32_DEBUG_STOP_EXCEPTION;
    }
    return 0;
}
static int d2_stopped(int owner,const ScDebugRef *ref,u32 stop,d2_state_t **state)
{
    ScDebugSnapshot snap;
    int error=d2_snapshot(owner,ref,&snap);
    if(error)return error;
    if(snap.state!=SC32_DEBUG_PAUSED)return SC32_E_BUSY;
    if(!stop || snap.stop_generation!=stop)return SC32_E_STALE;
    return d2_ref(owner,ref,state);
}
int sc32_debug_legacy_conflict(int pid,u32 address)
{
    d2_state_t *s=d2_state(pid,0);
    if(!s)return 0;
    for(u32 id=s->breaks;id;){d2_break_t *b=d2_break(id);
        if(!b)break;
        if(b->address==address && !(b->flags&SC32_DEBUG_BP_INVALID))return 1;
        id=b->next;
    }
    return 0;
}
int sc32_debug_exception(int pid,u32 vector,u32 *frame)
{
    d2_state_t *s=d2_state(pid,0);
    if(!s || !s->binding)return 0;
    if(vector==1 && !s->pending && process_debug_legacy_pending(pid)){
        s->stepping=0;
        s->reason=SC32_DEBUG_STOP_STEP;
        return 0;
    }
    if(vector==1 && (s->pending || s->stepping)){
        if(s->pending){d2_break_t *b=d2_break(s->pending);
            if(b){b->flags&=~SC32_DEBUG_BP_PENDING;
                if(paging_debug_patch(TASK(pid).pd,b->address,0xcc))b->flags|=SC32_DEBUG_BP_INVALID;
            }
            s->pending=0;
            s->break_epoch++;
        }
        s->stepping=0;
        if(s->auto_continue){s->auto_continue=0;
            return 1;
        }
        s->reason=SC32_DEBUG_STOP_STEP;
        return 2;
    }
    if(vector==3){for(u32 id=s->breaks;id;){d2_break_t *b=d2_break(id);
            if(!b)break;
            if(b->address==frame[14]-1 && !(b->flags&SC32_DEBUG_BP_INVALID)){
                if(paging_debug_patch(TASK(pid).pd,b->address,b->original))return 0;
                frame[14]--;
                b->hits++;
                b->flags|=SC32_DEBUG_BP_PENDING;
                s->pending=id;
                s->break_epoch++;
                s->reason=SC32_DEBUG_STOP_BREAKPOINT;
                return 2;
            }
            id=b->next;
    }}
    s->reason=vector==3?SC32_DEBUG_STOP_BREAKPOINT:SC32_DEBUG_STOP_EXCEPTION;
    return 0;
}
static void d2_module_drop(d2_module_t *m)
{
    if(m->source_object)fs_native_close(m->source_object,0);
    if(m->file)pframe_free_run(m->file,m->file_pages);
    if(m->metadata)pframe_free_run(m->metadata,m->metadata_pages);
    objpool_release(&d2_modules,(int)m->info.id-1);
}
void sc32_debug_unmap(int pid,u32 base,u32 bytes)
{
    d2_state_t *s=d2_state(pid,0);
    if(!s || !bytes)return;
    s->mapping_epoch++;
    if(s->loading){d2_module_t *loading=d2_module(s->loading);
        if(loading && loading->info.base==base){s->loading=0;
            d2_module_drop(loading);
    }}
    for(u32 id=s->breaks;id;){d2_break_t *b=d2_break(id);
        if(!b)break;
        if(b->address>=base && b->address-base<bytes){b->flags=SC32_DEBUG_BP_INVALID;
            if(s->pending==id)s->pending=s->auto_continue=s->stepping=0;
            s->break_epoch++;
        }id=b->next;
    }
    u32 *link=&s->modules;
    while(*link){d2_module_t *m=d2_module(*link);
        if(!m)break;
        if(m->info.base>=base && m->info.base-base<bytes){*link=m->next;
            d2_module_drop(m);
            s->module_count--;
            s->module_epoch++;
        }
        else link=&m->next;
    }
}
void sc32_debug_mapping_changed(int pid,u32 base,u32 bytes)
{
    d2_state_t *s=d2_state(pid,0);
    if(!s || !bytes)return;
    s->mapping_epoch++;
    for(u32 id=s->modules;id;){d2_module_t *m=d2_module(id);
        if(!m)break;
        if(base<m->info.base+m->info.image_bytes && m->info.base<base+bytes){s->module_epoch++;
            return;
        }id=m->next;
    }
}
void sc32_debug_stop(int pid)
{
    if(!d2_initialized)return;
    d2_state_t *s=objpool_get(&d2_states,pid);
    if(!s)return;
    /* 目标已经退出，代码页稍后统一释放，不向将被复用的页恢复旧字节。 */
    while(s->breaks){d2_break_t *b=d2_break(s->breaks);
        if(!b)break;
        u32 id=s->breaks;
        s->breaks=b->next;
        objpool_release(&d2_breaks,(int)id-1);
    }
    while(s->modules){d2_module_t *m=d2_module(s->modules);
        if(!m)break;
        s->modules=m->next;
        d2_module_drop(m);
    }
    if(s->loading){d2_module_t *m=d2_module(s->loading);
        if(m)d2_module_drop(m);
    }
    objpool_release(&d2_states,pid);
}
int sc32_debug_stop_step(int pid)
{
    if(!d2_initialized)return 1;
    d2_state_t *s=objpool_get(&d2_states,pid);
    if(!s)return 1;
    if(s->breaks){u32 id=s->breaks;
        d2_break_t *b=d2_break(id);
        s->breaks=b?b->next:0;
        objpool_release(&d2_breaks,(int)id-1);
        return 0;
    }
    if(s->modules){d2_module_t *m=d2_module(s->modules);
        if(m){s->modules=m->next;
            d2_module_drop(m);
        }else s->modules=0;
        return 0;
    }
    if(s->loading){d2_module_t *m=d2_module(s->loading);
        s->loading=0;
        if(m)d2_module_drop(m);
        return 0;
    }
    objpool_release(&d2_states,pid);
    return 1;
}
void sc32_debug_reset(int pid){sc32_debug_stop(pid);
}

static int d2_launch(int pid,u32 request,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScDebugLaunchInfo));
    if(error)return error;
    ScDebugLaunchDesc desc;
    error=sc32_core_input(pid,request,&desc,sizeof(desc));
    if(error)return error;
    if(desc.flags || desc.reserved)return SC32_E_INVALID;
    char command[1088];
    u32 length=0;
    for(;length<sizeof(command);length++){if(paging_user_copy(TASK(pid).pd,(u32)desc.command+length,command+length,1,0))return SC32_E_INVALID;
        if(!command[length])break;
    }if(!length || length==sizeof(command))return SC32_E_RANGE;
    for(u32 i=0;i<3;i++)if(desc.streams[i]<-1)return SC32_E_INVALID;
    int job=streams_exec(pid,command,desc.streams,0);
    if(job<=0)return job==-2?SC32_E_NOT_FOUND:job==-3?SC32_E_INVALID:job==-4?SC32_E_MEMORY:job==-5?SC32_E_ACCESS:SC32_E_FAILED;
    u32 jobinfo[8];
    int child=0;
    if(streams_job_snapshot(pid,(u32)job,jobinfo)>0)child=(int)jobinfo[3];
    d2_state_t *s=child?d2_state(child,1):0;
    u32 binding=d2_next_serial();
    int rejected=!s?SC32_E_MEMORY:!binding?SC32_E_RANGE:
    auth_uid(pid)!=auth_uid(child) || session_task(pid)!=session_task(child)?SC32_E_ACCESS:0;
    if(!rejected && process_debug_bind(child,pid)<0)rejected=SC32_E_FAILED;
    if(rejected){if(child)task_stop(child);
        (void)streams_wait(pid,(u32)job,jobinfo);
        return rejected;
    }
    s->binding=binding;
    s->reason=SC32_DEBUG_STOP_ENTRY;
    ScDebugLaunchInfo result;
    d2_zero(&result,sizeof(result));
    result.size=sizeof(result);
    result.version=1;
    result.target.size=sizeof(result.target);
    result.target.version=1;
    result.target.pid=(u32)child;
    result.target.generation=task_generation(child);
    result.target.binding=binding;
    result.job=(u32)job;
    return d2_publish(pid,output,&result,sizeof(result));
}
static int d2_control(int pid,Sc32DebugRequest *r,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScDebugSnapshot));
    if(error)return error;
    d2_state_t *s;
    error=d2_ref(pid,&r->target,&s);
    if(error)return error;
    int target=(int)r->target.pid;
    if(r->action!=SC32_DEBUG_PAUSE && r->action!=SC32_DEBUG_TERMINATE){
        error=d2_stopped(pid,&r->target,r->stop,&s);
        if(error)return error;
    }
    if(r->action==SC32_DEBUG_TERMINATE){ScDebugSnapshot result;
        error=d2_snapshot(pid,&r->target,&result);
        if(error)return error;
        result.state=SC32_DEBUG_EXITED;
        result.flags=0;
        d2_zero(&result.registers,sizeof(result.registers));
        result.registers.size=sizeof(result.registers);
        result.registers.version=1;
        task_stop(target);
        return d2_publish(pid,output,&result,sizeof(result));
    }
    if(r->action==SC32_DEBUG_PAUSE){if(process_debug(pid,target,DBG_PAUSE,0,0,0))return SC32_E_BUSY;
        s->reason=SC32_DEBUG_STOP_PAUSE;
        s->stepping=s->auto_continue=0;
    }
    else if(r->action==SC32_DEBUG_STEP || r->action==SC32_DEBUG_CONTINUE){
        /* TF覆盖的是真实已保存CPU帧。pending的新断点也需要先单步
         * 原指令；继续时#DB只重插后运行，STEP则把#DB交pause_fault。 */
        u32 *f=(u32 *)TASK(target).saved_esp;
        if(r->action==SC32_DEBUG_STEP || s->pending)f[16]|=0x100u;
        s->stepping=r->action==SC32_DEBUG_STEP;
        s->auto_continue=r->action==SC32_DEBUG_CONTINUE;
        s->reason=0;
        if(process_debug(pid,target,r->action==SC32_DEBUG_STEP?DBG_STEP:DBG_CONTINUE,0,0,0))return SC32_E_BUSY;
    }else return SC32_E_INVALID;
    ScDebugSnapshot result;
    error=d2_snapshot(pid,&r->target,&result);
    return error?error:d2_publish(pid,output,&result,sizeof(result));
}
static int d2_read(int pid,Sc32DebugRequest *r,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScDebugReadInfo));
    if(error)return error;
    d2_state_t *s;
    error=d2_stopped(pid,&r->target,r->stop,&s);
    if(error)return error;
    if(!r->bytes || r->bytes>SC32_DEBUG_READ_MAX || r->address>0xffffffffu-(r->bytes-1)
    || r->flags&~SC32_DEBUG_READ_ORIGINAL_CODE)return SC32_E_RANGE;
    u32 pd=TASK(pid).pd,header[2];
    if(paging_user_copy(pd,output,header,8,0) || !paging_user_write_range(pd,r->data,r->bytes)
    || !paging_user_write_range(pd,r->readable,r->bytes)
    || d2_overlap(r->data,r->bytes,r->readable,r->bytes)
    || d2_overlap(r->data,r->bytes,output,header[0])
    || d2_overlap(r->readable,r->bytes,output,header[0]))return SC32_E_INVALID;
    u8 bytes[256],mask[256];
    u32 readable=0,target_pd=TASK((int)r->target.pid).pd;
    for(u32 at=0;at<r->bytes;){u32 count=r->bytes-at,page=4096u-((r->address+at)&4095u);
        if(count>sizeof(bytes))count=sizeof(bytes);
        if(count>page)count=page;
        int valid=!paging_user_copy(target_pd,r->address+at,bytes,count,0);
        for(u32 i=0;i<count;i++){mask[i]=(u8)valid;
            if(!valid)bytes[i]=0;
            else readable++;
        }
        if(valid && r->flags&SC32_DEBUG_READ_ORIGINAL_CODE){
            for(u32 id=s->breaks;id;){d2_break_t *b=d2_break(id);
                if(!b)break;
                if(!(b->flags&SC32_DEBUG_BP_INVALID) && b->address>=r->address+at && b->address-(r->address+at)<count){
                    bytes[b->address-(r->address+at)]=b->original;
                }
                id=b->next;
            }
            process_debug_original((int)r->target.pid,r->address+at,bytes,count);
        }
        if(paging_user_copy(pd,r->data+at,bytes,count,1) || paging_user_copy(pd,r->readable+at,mask,count,1))return SC32_E_INVALID;
        at+=count;
    }
    ScDebugReadInfo info={sizeof(info),1,r->stop,r->address,r->bytes,readable,r->flags,0};
    return d2_publish(pid,output,&info,sizeof(info));
}
static void d2_break_value(const d2_break_t *b,u32 stop,ScDebugBreakpoint *v)
{d2_zero(v,sizeof(*v));
    v->size=sizeof(*v);
    v->version=1;
    v->id=b->id;
    v->generation=b->generation;
    v->address=b->address;
    v->flags=b->flags;
    v->hits=b->hits;
    v->stop_generation=stop;
}
static int d2_break_add(int pid,Sc32DebugRequest *r,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScDebugBreakpoint));
    if(error)return error;
    d2_state_t *s;
    error=d2_stopped(pid,&r->target,r->stop,&s);
    if(error)return error;
    int target=(int)r->target.pid;
    for(u32 id=s->breaks;id;){d2_break_t *b=d2_break(id);
        if(!b)break;
        if(b->address==r->address){ScDebugBreakpoint value;
            d2_break_value(b,r->stop,&value);
            return d2_publish(pid,output,&value,sizeof(value));
        }id=b->next;
    }
    if(process_debug_legacy_break(target,r->address))return SC32_E_BUSY;
    u8 original;
    if(paging_user_copy(TASK(target).pd,r->address,&original,1,0))return SC32_E_RANGE;
    if(original==0xcc)return SC32_E_EXISTS;
    int index=objpool_alloc(&d2_breaks);
    if(index<0)return SC32_E_MEMORY;
    u32 generation=d2_next_serial();
    if(!generation){objpool_release(&d2_breaks,index);
        return SC32_E_RANGE;
    }
    u32 *frame=(u32 *)TASK(target).saved_esp;
    int at_current=frame[14]==r->address;
    if(paging_debug_patch(TASK(target).pd,r->address,at_current?original:0xcc)){objpool_release(&d2_breaks,index);
        return SC32_E_RANGE;
    }
    d2_break_t *b=objpool_get(&d2_breaks,index);
    d2_zero(b,sizeof(*b));
    b->owner=r->target.pid;
    b->owner_generation=r->target.generation;
    b->id=(u32)index+1;
    b->generation=generation;
    b->address=r->address;
    b->original=original;
    b->flags=SC32_DEBUG_BP_ENABLED;
    /* 在当前暂停IP新建断点时，原指令还没有执行；先记作待重插，
     * 下一次STEP/CONT以TF执行它，再重插INT3，不制造同IP空停一次。 */
    if(at_current){b->flags|=SC32_DEBUG_BP_PENDING;
        s->pending=b->id;
    }
    b->next=s->breaks;
    s->breaks=b->id;
    s->break_count++;
    s->break_epoch++;
    ScDebugBreakpoint value;
    d2_break_value(b,r->stop,&value);
    return d2_publish(pid,output,&value,sizeof(value));
}
static int d2_break_remove(int pid,Sc32DebugRequest *r)
{
    d2_state_t *s;
    int error=d2_stopped(pid,&r->target,r->stop,&s);
    if(error)return error;
    u32 *link=&s->breaks;
    while(*link){d2_break_t *b=d2_break(*link);
        if(!b)break;
        if(b->id==r->id && b->generation==r->generation){
            if(!(b->flags&(SC32_DEBUG_BP_PENDING|SC32_DEBUG_BP_INVALID))
            && paging_debug_patch(TASK((int)r->target.pid).pd,b->address,b->original))return SC32_E_IO;
            if(s->pending==b->id)s->pending=s->auto_continue=0;
             *link=b->next;
            objpool_release(&d2_breaks,(int)b->id-1);
            s->break_count--;
            s->break_epoch++;
            return 0;
        }
        link=&b->next;
    }
    return SC32_E_STALE;
}

#include "sc32_debug_modules.inc"

static int d2_page(int pid,u32 operation,Sc32DebugRequest *r,u32 output)
{
    int error=sc32_core_output(pid,output,sizeof(ScDebugPage));
    if(error)return error;
    d2_state_t *s;
    error=d2_stopped(pid,&r->target,r->stop,&s);
    if(error)return error;
    u32 stride=operation==SC32_D2_BREAKS?sizeof(ScDebugBreakpoint):operation==SC32_D2_MODULES?
    sizeof(ScDebugModule):operation==SC32_D2_SEGMENTS?sizeof(ScDebugSegment):sizeof(ScDebugSymbol);
    u32 header[2];
    if(!r->capacity || r->capacity>SC32_DEBUG_PAGE_MAX || r->after==0xffffffffu
    || !paging_user_write_range(TASK(pid).pd,r->rows,r->capacity*stride)
    || paging_user_copy(TASK(pid).pd,output,header,8,0)
    || d2_overlap(r->rows,r->capacity*stride,output,header[0]))return SC32_E_INVALID;
    u32 epoch=operation==SC32_D2_BREAKS?s->break_epoch:s->module_epoch;
    if(r->epoch && r->epoch!=epoch)return SC32_E_STALE;
    ScDebugPage page={sizeof(page),1,r->stop,epoch,r->after,0,0,0};
    if(operation==SC32_D2_MODULES || operation==SC32_D2_BREAKS){
        u32 id=operation==SC32_D2_MODULES?s->modules:s->breaks;
        page.total=operation==SC32_D2_MODULES?s->module_count:s->break_count;
        /* cursor直接跟随本目标的稳定拥有链。每页只走实际返回行，
         * 不为计算total反复扫完整链，也不从头寻找第N页。 */
        if(r->after){
            if(operation==SC32_D2_MODULES){d2_module_t *m=d2_module(r->after);
                if(!m || m->owner!=r->target.pid || m->owner_generation!=r->target.generation
                || !(m->info.flags&SC32_DEBUG_VERIFIED_SOURCE))return SC32_E_STALE;
                id=m->next;
            }else{d2_break_t *b=d2_break(r->after);
                if(!b || b->owner!=r->target.pid || b->owner_generation!=r->target.generation)return SC32_E_STALE;
                id=b->next;
            }
        }
        while(id && page.count<r->capacity){u32 next;
            if(operation==SC32_D2_MODULES){d2_module_t *m=d2_module(id);
                if(!m)return SC32_E_STALE;
                next=m->next;
            }
            else{d2_break_t *b=d2_break(id);
                if(!b)return SC32_E_STALE;
                next=b->next;
            }
            if(operation==SC32_D2_MODULES){d2_module_t *m=d2_module(id);
                if(d2_publish(pid,r->rows+page.count*stride,&m->info,stride))return SC32_E_INVALID;
            }
            else{ScDebugBreakpoint value;
                d2_break_value(d2_break(id),r->stop,&value);
                if(d2_publish(pid,r->rows+page.count*stride,&value,stride))return SC32_E_INVALID;
            }
            page.count++;
            page.cursor=id;
            id=next;
        }
        if(!id)page.flags=SC32_DEBUG_PAGE_FINISHED;
    }else{
        d2_module_t *m=d2_module(r->module);
        if(!m || !(m->info.flags&SC32_DEBUG_VERIFIED_SOURCE) || m->owner!=r->target.pid || m->owner_generation!=r->target.generation
        || m->info.generation!=r->module_generation)return SC32_E_STALE;
        page.total=operation==SC32_D2_SEGMENTS?m->info.segment_count:m->info.symbol_count;
        if(r->after>page.total)return SC32_E_STALE;
        for(u32 index=r->after;index<page.total && page.count<r->capacity;index++){
            if(operation==SC32_D2_SEGMENTS){ScDebugSegment seg;
                d2_segment(m,index,&seg);
                if(d2_publish(pid,r->rows+page.count*stride,&seg,stride))return SC32_E_INVALID;
            }
            else{ScDebugSymbol symbol;
                d2_symbol(m,index,&symbol);
                if(d2_publish(pid,r->rows+page.count*stride,&symbol,stride))return SC32_E_INVALID;
            }
            page.count++;
            page.cursor=index+1;
        }
        if(page.cursor==page.total)page.flags=SC32_DEBUG_PAGE_FINISHED;
    }
    return d2_publish(pid,output,&page,sizeof(page));
}
int sc32_debug_call(int pid,u32 operation,u32 request,u32 output)
{
    /* core_input/output在每次陷入重新检查真实caller、IF=0和完整范围。
     * 登记入口同样只操作本人页表；不存在任意PID/内核地址选项。 */
    if(d2_init())return SC32_E_MEMORY;
    if(operation==SC32_D2_LAUNCH)return d2_launch(pid,request,output);
    if(operation>=SC32_D2_REGISTER && operation<=SC32_D2_READY)return d2_registration(pid,operation,request,output);
    Sc32DebugRequest r;
    int error=sc32_core_input(pid,request,&r,sizeof(r));
    if(error)return error;
    if(operation==SC32_D2_QUERY){error=sc32_core_output(pid,output,sizeof(ScDebugSnapshot));
        if(error)return error;
        ScDebugSnapshot value;
        error=d2_snapshot(pid,&r.target,&value);
        return error?error:d2_publish(pid,output,&value,sizeof(value));
    }
    if(operation==SC32_D2_CONTROL)return d2_control(pid,&r,output);
    if(operation==SC32_D2_READ)return d2_read(pid,&r,output);
    if(operation==SC32_D2_ADD)return d2_break_add(pid,&r,output);
    if(operation==SC32_D2_REMOVE){if(output)return SC32_E_INVALID;
        return d2_break_remove(pid,&r);
    }
    if(operation>=SC32_D2_BREAKS && operation<=SC32_D2_SYMBOLS)return d2_page(pid,operation,&r,output);
    return SC32_E_NOT_SUPPORTED;
}
