#include "../sc32/SC32PRIVATE.H"
#include "SC32LEASEWIRE.H"

/* 组合到真正SC32API.C；寄存器桥仅复用它的私有sc32_call。普通
 * SCSHELL/worker只调用上述cdecl公开函数，不含SCAPI或inline asm。 */
#define SC32_OP_LEASE_BASE 0x0f00u
static ScStatus sc32_lease_gate(ScU32 operation,const void *request,ScLeaseInfo *out)
{
    ScLeaseInfo reply;sc32_zero(&reply,sizeof(reply));reply.size=sizeof(reply);reply.version=1;
    reply.raw=(ScI32)0x80000000u;
    ScI32 status=sc32_call(SC32_WIRE_GATE,(ScI32)operation,(ScI32)request,(ScI32)&reply,0,0,0);
    int valid=reply.size==sizeof(reply) && reply.version==1 && reply.raw!=(ScI32)0x80000000u;
    if(valid)sc32_copy(out,&reply,sizeof(reply));
    if(status==SC32_OK || status==SC32_PENDING){
        if(valid)return status;
        return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_KERNEL,status,SC32_OP_LEASE_BASE+operation-SC32_L_ACQUIRE);
    }
    ScI32 code=status<=SC32_E_INVALID && status>=SC32_E_FAILED?status:SC32_E_FAILED;
    return sc32_fail(code,SC32_DOMAIN_KERNEL,valid?reply.raw:status,SC32_OP_LEASE_BASE+operation-SC32_L_ACQUIRE);
}
static ScStatus sc32_lease_records(const void *input,ScU32 bytes,ScLeaseInfo *out,ScU32 operation)
{
    ScStatus checked=sc32_record_in(input,bytes);
    if(checked<0)return sc32_invalid(checked,SC32_OP_LEASE_BASE+operation-SC32_L_ACQUIRE);
    checked=sc32_record_out(out,sizeof(*out));
    if(checked<0)return sc32_invalid(checked,SC32_OP_LEASE_BASE+operation-SC32_L_ACQUIRE);
    if(sc32_overlap(input,bytes,out,out->size))return sc32_invalid(SC32_E_INVALID,SC32_OP_LEASE_BASE+operation-SC32_L_ACQUIRE);
    return 0;
}
ScStatus ScFileLeaseAcquire(const ScLeaseAcquireDesc *desc,ScLeaseInfo *out)
{
    ScStatus checked=sc32_lease_records(desc,sizeof(*desc),out,SC32_L_ACQUIRE);if(checked<0)return checked;
    Sc32LeaseAcquireRequest q;sc32_copy(&q,desc,sizeof(q));return sc32_lease_gate(SC32_L_ACQUIRE,&q,out);
}
static ScStatus sc32_lease_open(const ScLeaseOpenDesc *desc,ScLeaseInfo *out,ScU32 operation)
{
    ScStatus checked=sc32_lease_records(desc,sizeof(*desc),out,operation);if(checked<0)return checked;
    ScU32 length=0;checked=sc32_string(desc->path,127,&length);
    if(checked<0)return sc32_invalid(checked,SC32_OP_LEASE_BASE+operation-SC32_L_ACQUIRE);
    if(sc32_overlap(desc->path,length+1,out,out->size))return sc32_invalid(SC32_E_INVALID,SC32_OP_LEASE_BASE+operation-SC32_L_ACQUIRE);
    Sc32LeaseOpenRequest q;sc32_copy(&q,desc,sizeof(q));return sc32_lease_gate(operation,&q,out);
}
ScStatus ScFileLeaseOpenPinned(const ScLeaseOpenDesc *desc,ScLeaseInfo *out)
{return sc32_lease_open(desc,out,SC32_L_OPEN);}
ScStatus ScFileLeaseCreatePinned(const ScLeaseOpenDesc *desc,ScLeaseInfo *out)
{return sc32_lease_open(desc,out,SC32_L_CREATE);}
static ScStatus sc32_lease_control(const ScLeaseControl *desc,ScLeaseInfo *out,ScU32 operation)
{
    ScStatus checked=sc32_lease_records(desc,sizeof(*desc),out,operation);if(checked<0)return checked;
    Sc32LeaseControlRequest q;sc32_copy(&q,desc,sizeof(q));return sc32_lease_gate(operation,&q,out);
}
ScStatus ScFileLeaseQuery(const ScLeaseControl *desc,ScLeaseInfo *out)
{return sc32_lease_control(desc,out,SC32_L_QUERY);}
ScStatus ScFileLeaseRelease(const ScLeaseControl *desc,ScLeaseInfo *out)
{return sc32_lease_control(desc,out,SC32_L_RELEASE);}
ScStatus ScFileLeaseCancel(const ScLeaseControl *desc,ScLeaseInfo *out)
{return sc32_lease_control(desc,out,SC32_L_CANCEL);}
ScStatus ScFileLeaseAdvance(const ScLeaseControl *desc,ScLeaseInfo *out)
{return sc32_lease_control(desc,out,SC32_L_ADVANCE);}
static ScStatus sc32_lease_publish(const ScLeasePublishDesc *desc,ScLeaseInfo *out,ScU32 operation)
{
    ScStatus checked=sc32_lease_records(desc,sizeof(*desc),out,operation);if(checked<0)return checked;
    ScU32 length=0;checked=sc32_string(desc->target_path,127,&length);
    if(checked<0)return sc32_invalid(checked,SC32_OP_LEASE_BASE+operation-SC32_L_ACQUIRE);
    if(sc32_overlap(desc->target_path,length+1,out,out->size))return sc32_invalid(SC32_E_INVALID,SC32_OP_LEASE_BASE+operation-SC32_L_ACQUIRE);
    Sc32LeasePublishRequest q;sc32_copy(&q,desc,sizeof(q));return sc32_lease_gate(operation,&q,out);
}
ScStatus ScFileLeasePublish(const ScLeasePublishDesc *desc,ScLeaseInfo *out)
{return sc32_lease_publish(desc,out,SC32_L_PUBLISH);}
ScStatus ScFileLeaseRename(const ScLeasePublishDesc *desc,ScLeaseInfo *out)
{return sc32_lease_publish(desc,out,SC32_L_RENAME);}
ScStatus ScFileLeaseRemove(const ScLeaseRemoveDesc *desc,ScLeaseInfo *out)
{
    ScStatus checked=sc32_lease_records(desc,sizeof(*desc),out,SC32_L_REMOVE);if(checked<0)return checked;
    Sc32LeaseRemoveRequest q;sc32_copy(&q,desc,sizeof(q));return sc32_lease_gate(SC32_L_REMOVE,&q,out);
}
ScStatus ScFileLeaseSetMode(const ScLeaseModeDesc *desc,ScLeaseInfo *out)
{
    ScStatus checked=sc32_lease_records(desc,sizeof(*desc),out,SC32_L_SET_MODE);if(checked<0)return checked;
    Sc32LeaseModeRequest q;sc32_copy(&q,desc,sizeof(q));return sc32_lease_gate(SC32_L_SET_MODE,&q,out);
}
ScStatus ScFileLeaseSnapshot(const ScLeaseControl *desc,ScU32 cursor,ScLeaseEntry *rows,ScU32 capacity,ScLeasePage *out)
{
    ScU32 operation=SC32_OP_LEASE_BASE+SC32_L_SNAPSHOT-SC32_L_ACQUIRE;
    ScStatus checked=sc32_record_in(desc,sizeof(*desc));if(checked<0)return sc32_invalid(checked,operation);
    if(desc->operation_sequence || desc->flags || desc->reserved[0] || desc->reserved[1] || capacity>SC32_LEASE_BATCH_MAX)
        return sc32_invalid(SC32_E_INVALID,operation);
    checked=sc32_record_out(out,sizeof(*out));if(checked<0)return sc32_invalid(checked,operation);
    ScU32 bytes=capacity*(ScU32)sizeof(*rows);checked=sc32_out(rows,bytes);if(checked<0)return sc32_invalid(checked,operation);
    if(sc32_overlap(rows,bytes,out,out->size) || sc32_overlap(rows,bytes,desc,sizeof(*desc))
       || sc32_overlap(desc,sizeof(*desc),out,out->size))return sc32_invalid(SC32_E_INVALID,operation);
    Sc32LeaseSnapshotRequest q;sc32_zero(&q,sizeof(q));q.size=sizeof(q);q.version=1;
    q.lease=desc->lease;q.generation=desc->generation;q.rows=(ScU32)rows;q.capacity=capacity;q.cursor=cursor;
    ScLeasePage page;sc32_zero(&page,sizeof(page));page.size=sizeof(page);page.version=1;
    ScI32 status=sc32_call(SC32_WIRE_GATE,SC32_L_SNAPSHOT,(ScI32)&q,(ScI32)&page,0,0,0);
    if(status)return sc32_fail(status<=SC32_E_INVALID && status>=SC32_E_FAILED?status:SC32_E_FAILED,SC32_DOMAIN_KERNEL,status,operation);
    if(page.size!=sizeof(page) || page.version!=1 || page.count>capacity)return sc32_fail(SC32_E_FAILED,SC32_DOMAIN_KERNEL,status,operation);
    sc32_copy(out,&page,sizeof(page));return 0;
}
