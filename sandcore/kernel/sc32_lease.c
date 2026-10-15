#include "sc32_lease.h"
#include "sc32_core.h"
#include "fs_lease.h"
#include "paging.h"
#include "task.h"
#include "userspace.h"

typedef char lease_info_160[(sizeof(ScLeaseInfo)==160)?1:-1];
typedef char lease_acquire_64[(sizeof(Sc32LeaseAcquireRequest)==64)?1:-1];
typedef char lease_open_64[(sizeof(Sc32LeaseOpenRequest)==64)?1:-1];
typedef char lease_publish_96[(sizeof(Sc32LeasePublishRequest)==96)?1:-1];
typedef char lease_remove_48[(sizeof(Sc32LeaseRemoveRequest)==48)?1:-1];
typedef char lease_mode_48[(sizeof(Sc32LeaseModeRequest)==48)?1:-1];
typedef char lease_control_32[(sizeof(Sc32LeaseControlRequest)==32)?1:-1];
typedef char lease_snapshot_40[(sizeof(Sc32LeaseSnapshotRequest)==40)?1:-1];
typedef char lease_page_32[(sizeof(ScLeasePage)==32)?1:-1];
typedef char lease_entry_128[(sizeof(ScLeaseEntry)==128)?1:-1];

static void lease_call_zero(void *out,u32 bytes)
{for(u32 i=0;i<bytes;i++)((u8 *)out)[i]=0;}
static int lease_call_publish(int pid,u32 address,const void *data,u32 bytes)
{return paging_user_copy(TASK(pid).pd,address,(void *)data,bytes,1)?SC32_E_INVALID:0;}
static int lease_call_overlap(u32 a,u32 bytes,u32 b,u32 other)
{return bytes && other && a<=0xffffffffu-bytes && b<=0xffffffffu-other && a<b+other && b<a+bytes;}
static int lease_call_path(int pid,u32 address,char out[64],u32 *input_bytes)
{
    char input[128];if(!address)return SC32_E_INVALID;
    u32 i=0;
    for(;i<sizeof(input);i++){
        if(address>0xffffffffu-i || !paging_user_range(TASK(pid).pd,address+i,1)
           || paging_user_copy(TASK(pid).pd,address+i,input+i,1,0))return SC32_E_INVALID;
        if(!input[i])break;
    }
    if(i==sizeof(input) || !paging_user_range(TASK(pid).pd,address,i+1))return SC32_E_INVALID;
    /* 完整字符串RO复制后只解析这一次；后续operation保存rootless值，
     * 后台/Advance没有用户VA，也不借新的CWD重新解释目标名字。 */
    lease_call_zero(out,64);*input_bytes=i+1;
    return userspace_resolve(pid,input,out,64)<0?SC32_E_RANGE:0;
}
int sc32_lease_call(int pid,u32 operation,u32 request,u32 output)
{
    u32 bytes;
    switch(operation){
    case SC32_L_ACQUIRE:bytes=64;break;
    case SC32_L_OPEN:case SC32_L_CREATE:bytes=64;break;
    case SC32_L_PUBLISH:case SC32_L_RENAME:bytes=96;break;
    case SC32_L_REMOVE:case SC32_L_SET_MODE:bytes=48;break;
    case SC32_L_QUERY:case SC32_L_RELEASE:case SC32_L_CANCEL:case SC32_L_ADVANCE:bytes=32;break;
    case SC32_L_SNAPSHOT:bytes=40;break;
    default:return SC32_E_NOT_SUPPORTED;
    }
    u32 q[24];int status=sc32_core_input(pid,request,q,bytes);if(status)return status;
    u32 known=operation==SC32_L_SNAPSHOT?32u:160u;
    status=sc32_core_output(pid,output,known);if(status)return status;
    u32 declared=0;if(paging_user_copy(TASK(pid).pd,output,&declared,4,0))return SC32_E_INVALID;
    if(lease_call_overlap(request,bytes,output,declared))return SC32_E_INVALID;
    if(operation==SC32_L_SNAPSHOT){
        Sc32LeaseSnapshotRequest *s=(Sc32LeaseSnapshotRequest *)q;
        if(s->capacity>SC32_LEASE_BATCH_MAX)return SC32_E_RANGE;
        u32 row_bytes=s->capacity*(u32)sizeof(ScLeaseEntry);
        if(row_bytes && (!paging_user_write_range(TASK(pid).pd,s->rows,row_bytes)
           || lease_call_overlap(s->rows,row_bytes,output,declared)
           || lease_call_overlap(s->rows,row_bytes,request,bytes)))return SC32_E_INVALID;
        ScLeasePage page;status=fs_lease_snapshot(pid,s,&page);if(status)return status;
        for(u32 i=0;i<page.count;i++){
            ScLeaseEntry entry;status=fs_lease_snapshot_row(pid,s->lease,s->generation,s->cursor+i,&entry);
            if(status)return status;
            status=lease_call_publish(pid,s->rows+i*(u32)sizeof(entry),&entry,sizeof(entry));if(status)return status;
        }
        return lease_call_publish(pid,output,&page,sizeof(page));
    }
    char name[64];u32 path_address=0,path_bytes=0;
    if(operation==SC32_L_OPEN || operation==SC32_L_CREATE)path_address=q[2];
    else if(operation==SC32_L_PUBLISH || operation==SC32_L_RENAME)path_address=q[8];
    if(path_address){
        status=lease_call_path(pid,path_address,name,&path_bytes);if(status)return status;
        if(lease_call_overlap(path_address,path_bytes,output,declared))return SC32_E_INVALID;
    }else if(operation==SC32_L_OPEN || operation==SC32_L_CREATE || operation==SC32_L_PUBLISH || operation==SC32_L_RENAME)return SC32_E_INVALID;
    ScLeaseInfo info;lease_call_zero(&info,sizeof(info));info.raw=(ScI32)0x80000000u;
    switch(operation){
    case SC32_L_ACQUIRE:status=fs_lease_acquire(pid,(Sc32LeaseAcquireRequest *)q,&info);break;
    case SC32_L_OPEN:case SC32_L_CREATE:
        status=fs_lease_open(pid,(Sc32LeaseOpenRequest *)q,name,operation==SC32_L_CREATE,&info);break;
    case SC32_L_PUBLISH:case SC32_L_RENAME:
        status=fs_lease_begin(pid,(Sc32LeasePublishRequest *)q,name,operation==SC32_L_RENAME,&info);break;
    case SC32_L_REMOVE:status=fs_lease_remove(pid,(Sc32LeaseRemoveRequest *)q,&info);break;
    case SC32_L_SET_MODE:status=fs_lease_set_mode(pid,(Sc32LeaseModeRequest *)q,&info);break;
    default:status=fs_lease_control(pid,operation,(Sc32LeaseControlRequest *)q,&info);break;
    }
    /* 只有已构成真实内核结果的路径写out；参数/权限/映射等preflight
     * 拒绝保留caller整个输出。PENDING结果也不留out指针供poll。 */
    if(info.size==sizeof(info) && info.version==1 && info.raw!=(ScI32)0x80000000u){
        int copied=lease_call_publish(pid,output,&info,sizeof(info));if(copied)return copied;
    }
    return status;
}
int sc32_lease_stop_step(int pid){return fs_lease_stop_step(pid);}
void sc32_lease_poll(void){fs_lease_poll();}
