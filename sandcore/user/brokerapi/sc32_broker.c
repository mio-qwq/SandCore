#include "../SC32BROKER.H"
#include "../sc32/SC32PRIVATE.H"
#include "../sc32/SC32BROKERWIRE.H"
/* 待Root组合到真正SC32API.SCL，当前aggregate/json没有改。片段只
* 共用已有私有bridge，不含SCAPI/asm，不依赖SCCLIB形成引导环。
* 公共调用先验全部out，再进gate；Shared内部短步不会把候选VA或
* 用户buffer藏到kernel异步任务，公开out仅在完整READY成功时发布。 */
typedef char broker_public_peer_48[(sizeof(ScBrokerIdentity)==48)?1:-1];
typedef char broker_public_clip_96[(sizeof(ScClipboardInfo)==96)?1:-1];
typedef char broker_public_ipc_desc_64[(sizeof(ScIpcEndpointDesc)==64)?1:-1];
typedef char broker_public_ipc_packet_80[(sizeof(ScIpcPacket)==80)?1:-1];
typedef char broker_public_mapping_48[(sizeof(ScSharedMappingInfo)==48)?1:-1];

static ScU32 broker_operation(ScU32 selector)
{
    if(selector>=0x500u)return SC32_B_SHARED_OPERATION+selector-0x500u;
    if(selector>=0x400u)return SC32_B_IPC_OPERATION+selector-0x400u;
    return SC32_B_CLIP_OPERATION+selector-0x300u;
}

static ScStatus broker_invalid(ScStatus error,ScU32 selector)
{return sc32_invalid(error,broker_operation(selector));
}

static ScI32 broker_gate(ScU32 selector,const void *input,void *out)
{return sc32_call(SC32_WIRE_GATE,(ScI32)selector,(ScI32)input,(ScI32)out,0,0,0);
}

static ScStatus broker_result(ScI32 raw,ScU32 selector)
{return sc32_native(raw,broker_operation(selector));
}

static void broker_record(void *out,ScU32 bytes)
{sc32_zero(out,bytes);
    ((ScU32 *)out)[0]=bytes;
    ((ScU32 *)out)[1]=1;
}

static ScI32 broker_handle(ScU32 selector,ScU32 handle,ScU32 value,void *out)
{Sc32BrokerHandle input={16,1,handle,value};
    return broker_gate(selector,&input,out);
}

static ScStatus broker_close(ScU32 selector,ScU32 handle)
{
    ScI32 raw=broker_handle(selector,handle,0,0);
    while(raw==SC32_PENDING){ScProcessYield();
        raw=broker_handle(selector,handle,0,0);
    }
    return broker_result(raw,selector);
}

ScStatus ScClipboardBegin(const ScClipboardDesc *desc,ScClipboardCandidate *out)
{
    ScStatus error=sc32_record_in(desc,sizeof(*desc));
    if(!error)error=sc32_out(out,4);
    if(error)return broker_invalid(error,SC32_B_CLIP_BEGIN);
    Sc32BrokerToken reply;
    broker_record(&reply,sizeof(reply));
    ScI32 raw=broker_gate(SC32_B_CLIP_BEGIN,desc,&reply);
    if(raw)return broker_result(raw,SC32_B_CLIP_BEGIN);
    *out=reply.handle;
    return 0;
}

ScStatus ScClipboardOffer(ScClipboardCandidate candidate,ScU32 format,const void *data,ScU32 bytes)
{
    if(bytes>0x04000000u)return broker_invalid(SC32_E_RANGE,SC32_B_CLIP_OFFER);
    ScStatus error=sc32_probe(data,bytes,SC32_WIRE_READ);
    if(error)return broker_invalid(error,SC32_B_CLIP_OFFER);
    if(bytes>SC32_BROKER_IO_MAX){
        Sc32ClipboardOfferBegin begin={24,1,candidate,format,bytes,0};
        Sc32BrokerToken reply;
        broker_record(&reply,sizeof(reply));
        ScI32 raw=broker_gate(SC32_B_CLIP_OFFER_BEGIN,&begin,&reply);
        if(raw)return broker_result(raw,SC32_B_CLIP_OFFER);
        for(ScU32 offset=0;offset<bytes;){
            ScU32 count=bytes-offset;
            if(count>SC32_BROKER_IO_MAX)count=SC32_BROKER_IO_MAX;
            Sc32ClipboardOfferWrite portion={24,1,reply.handle,(ScU32)data+offset,count,offset};
            raw=broker_gate(SC32_B_CLIP_OFFER_WRITE,&portion,0);
            if(raw){(void)broker_handle(SC32_B_CLIP_OFFER_ABORT,reply.handle,0,0);
                return broker_result(raw,SC32_B_CLIP_OFFER);
            }
            offset+=count;
            if(offset<bytes)ScProcessYield();
        }
        raw=broker_handle(SC32_B_CLIP_OFFER_FINISH,reply.handle,0,0);
        if(raw)(void)broker_handle(SC32_B_CLIP_OFFER_ABORT,reply.handle,0,0);
        return broker_result(raw,SC32_B_CLIP_OFFER);
    }
    Sc32ClipboardOffer input={24,1,candidate,format,(ScU32)data,bytes};
    return broker_result(broker_gate(SC32_B_CLIP_OFFER,&input,0),SC32_B_CLIP_OFFER);
}

ScStatus ScClipboardCommit(ScClipboardCandidate candidate,ScU32 expected_epoch,ScClipboardInfo *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_CLIP_COMMIT);
    return broker_result(broker_handle(SC32_B_CLIP_COMMIT,candidate,expected_epoch,out),SC32_B_CLIP_COMMIT);
}

ScStatus ScClipboardAbort(ScClipboardCandidate candidate){return broker_close(SC32_B_CLIP_ABORT,candidate);
}

ScStatus ScClipboardOpen(ScClipboardInfo *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_CLIP_OPEN);
    return broker_result(broker_gate(SC32_B_CLIP_OPEN,0,out),SC32_B_CLIP_OPEN);
}

ScStatus ScClipboardFormats(ScClipboardSnapshot snapshot,ScU32 after,ScClipboardFormatInfo *rows,ScU32 capacity,ScClipboardPage *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_CLIP_FORMATS);
    if(capacity>SC32_BROKER_PAGE_MAX)return broker_invalid(SC32_E_RANGE,SC32_B_CLIP_FORMATS);
    error=sc32_out(rows,capacity*sizeof(*rows));
    if(error)return broker_invalid(error,SC32_B_CLIP_FORMATS);
    if(sc32_overlap(out,out->size,rows,capacity*sizeof(*rows)))return broker_invalid(SC32_E_INVALID,SC32_B_CLIP_FORMATS);
    Sc32ClipboardFormats input={32,1,snapshot,after,(ScU32)rows,capacity,{0,0}};
    return broker_result(broker_gate(SC32_B_CLIP_FORMATS,&input,out),SC32_B_CLIP_FORMATS);
}

ScStatus ScClipboardRead(ScClipboardSnapshot snapshot,ScU32 format,ScU32 offset,void *data,ScU32 bytes,ScBrokerIoResult *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_CLIP_READ);
    if(bytes>SC32_BROKER_IO_MAX)return broker_invalid(SC32_E_RANGE,SC32_B_CLIP_READ);
    error=sc32_out(data,bytes);
    if(error)return broker_invalid(error,SC32_B_CLIP_READ);
    if(sc32_overlap(out,out->size,data,bytes))return broker_invalid(SC32_E_INVALID,SC32_B_CLIP_READ);
    Sc32ClipboardRead input={32,1,snapshot,format,offset,(ScU32)data,bytes,0};
    return broker_result(broker_gate(SC32_B_CLIP_READ,&input,out),SC32_B_CLIP_READ);
}

ScStatus ScClipboardClose(ScClipboardSnapshot snapshot){return broker_close(SC32_B_CLIP_CLOSE,snapshot);
}

ScStatus ScClipboardClear(ScU32 expected_epoch,ScClipboardInfo *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_CLIP_CLEAR);
    return broker_result(broker_handle(SC32_B_CLIP_CLEAR,0,expected_epoch,out),SC32_B_CLIP_CLEAR);
}

ScStatus ScClipboardQuery(ScClipboardInfo *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_CLIP_QUERY);
    return broker_result(broker_gate(SC32_B_CLIP_QUERY,0,out),SC32_B_CLIP_QUERY);
}

ScStatus ScClipboardWatch(ScU32 enable,ScU32 *epoch)
{
    ScStatus error=sc32_out(epoch,4);
    if(error)return broker_invalid(error,SC32_B_CLIP_WATCH);
    Sc32BrokerToken reply;
    broker_record(&reply,sizeof(reply));
    ScI32 raw=broker_handle(SC32_B_CLIP_WATCH,enable,0,&reply);
    if(raw)return broker_result(raw,SC32_B_CLIP_WATCH);
    *epoch=reply.value;
    return 0;
}

ScStatus ScIpcCreate(const ScIpcEndpointDesc *desc,ScIpcEndpoint *out)
{
    ScStatus error=sc32_record_in(desc,sizeof(*desc));
    if(!error)error=sc32_out(out,4);
    if(!error)error=sc32_string(desc->name,63,0);
    if(error)return broker_invalid(error,SC32_B_IPC_CREATE);
    Sc32IpcCreate input;
    sc32_copy(&input,desc,sizeof(input));
    input.name=(ScU32)desc->name;
    Sc32BrokerToken reply;
    broker_record(&reply,sizeof(reply));
    ScI32 raw=broker_gate(SC32_B_IPC_CREATE,&input,&reply);
    if(raw)return broker_result(raw,SC32_B_IPC_CREATE);
    *out=reply.handle;
    return 0;
}

ScStatus ScIpcListen(ScIpcEndpoint endpoint){return broker_result(broker_handle(SC32_B_IPC_LISTEN,endpoint,0,0),SC32_B_IPC_LISTEN);
}

ScStatus ScIpcConnect(const ScIpcConnectDesc *desc,ScIpcChannel *out)
{
    ScStatus error=sc32_record_in(desc,sizeof(*desc));
    if(!error)error=sc32_out(out,4);
    if(!error)error=sc32_string(desc->name,63,0);
    if(error)return broker_invalid(error,SC32_B_IPC_CONNECT);
    Sc32IpcConnect input;
    sc32_copy(&input,desc,sizeof(input));
    input.name=(ScU32)desc->name;
    Sc32BrokerToken reply;
    broker_record(&reply,sizeof(reply));
    ScI32 raw=broker_gate(SC32_B_IPC_CONNECT,&input,&reply);
    if(raw)return broker_result(raw,SC32_B_IPC_CONNECT);
    *out=reply.handle;
    return 0;
}

ScStatus ScIpcAccept(ScIpcEndpoint endpoint,ScIpcChannel *out,ScBrokerIdentity *peer)
{
    ScStatus error=sc32_out(out,4);
    if(!error)error=sc32_record_out(peer,sizeof(*peer));
    if(error)return broker_invalid(error,SC32_B_IPC_ACCEPT);
    if(sc32_overlap(out,4,peer,peer->size))return broker_invalid(SC32_E_INVALID,SC32_B_IPC_ACCEPT);
    Sc32IpcAccepted reply;
    broker_record(&reply,sizeof(reply));
    ScI32 raw=broker_handle(SC32_B_IPC_ACCEPT,endpoint,0,&reply);
    if(raw)return broker_result(raw,SC32_B_IPC_ACCEPT);
    *out=reply.handle;
    sc32_copy(peer,&reply.peer,sizeof(*peer));
    return 0;
}

ScStatus ScIpcSend(ScIpcChannel channel,ScU32 type,const void *data,ScU32 bytes)
{
    if(bytes>SC32_BROKER_IO_MAX)return broker_invalid(SC32_E_RANGE,SC32_B_IPC_SEND);
    ScStatus error=sc32_probe(data,bytes,SC32_WIRE_READ);
    if(error)return broker_invalid(error,SC32_B_IPC_SEND);
    Sc32IpcSend input={24,1,channel,type,(ScU32)data,bytes};
    return broker_result(broker_gate(SC32_B_IPC_SEND,&input,0),SC32_B_IPC_SEND);
}

ScStatus ScIpcPeek(ScIpcChannel channel,ScIpcPacket *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_IPC_PEEK);
    return broker_result(broker_handle(SC32_B_IPC_PEEK,channel,0,out),SC32_B_IPC_PEEK);
}

ScStatus ScIpcReceive(ScIpcChannel channel,void *data,ScU32 capacity,ScIpcPacket *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_IPC_RECEIVE);
    if(capacity>SC32_BROKER_IO_MAX)return broker_invalid(SC32_E_RANGE,SC32_B_IPC_RECEIVE);
    error=sc32_out(data,capacity);
    if(error)return broker_invalid(error,SC32_B_IPC_RECEIVE);
    if(sc32_overlap(out,out->size,data,capacity))return broker_invalid(SC32_E_INVALID,SC32_B_IPC_RECEIVE);
    Sc32IpcReceive input={24,1,channel,(ScU32)data,capacity,0};
    return broker_result(broker_gate(SC32_B_IPC_RECEIVE,&input,out),SC32_B_IPC_RECEIVE);
}

ScStatus ScIpcPeer(ScIpcChannel channel,ScBrokerIdentity *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_IPC_PEER);
    return broker_result(broker_handle(SC32_B_IPC_PEER,channel,0,out),SC32_B_IPC_PEER);
}

ScStatus ScIpcQuery(ScU32 handle,ScIpcInfo *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_IPC_QUERY);
    return broker_result(broker_handle(SC32_B_IPC_QUERY,handle,0,out),SC32_B_IPC_QUERY);
}

ScStatus ScIpcClose(ScU32 handle){return broker_close(SC32_B_IPC_CLOSE,handle);
}

ScStatus ScBrokerReady(ScU32 handle,ScBrokerReadyInfo *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_READY);
    return broker_result(broker_handle(SC32_B_READY,handle,0,out),SC32_B_READY);
}

ScStatus ScBrokerWait(const ScBrokerWaitDesc *desc,ScU32 *events)
{
    ScStatus error=sc32_record_in(desc,sizeof(*desc));
    if(!error)error=sc32_out(events,4);
    if(error)return broker_invalid(error,SC32_B_WAIT);
    ScI32 raw=broker_gate(SC32_B_WAIT,desc,0);
    if(raw<0)return broker_result(raw,SC32_B_WAIT);
    error=sc32_out(events,4);
    if(error)return broker_invalid(error,SC32_B_WAIT);
    *events=(ScU32)raw;
    return ((ScU32)raw&0x40000000u)?SC32_TIMEOUT:SC32_OK;
}

static ScI32 broker_shared_finish(ScU32 step,ScU32 close,ScU32 handle,void *reply,void *public_out,ScStatus *validation)
{
    ScI32 raw=SC32_PENDING;
    *validation=0;
    while(raw==SC32_PENDING){ScProcessYield();
        ScStatus checked=step==SC32_B_SHARED_STEP?sc32_out(public_out,4):sc32_record_out(public_out,sizeof(ScSharedMappingInfo));
        if(checked){*validation=checked;
            raw=checked;
            break;
        }
        raw=broker_handle(step,handle,0,reply);
    }
    if(raw<0){ScI32 cleanup=broker_handle(close,handle,0,0);
        while(cleanup==SC32_PENDING){ScProcessYield();
            cleanup=broker_handle(close,handle,0,0);
    }}
    return raw;
}

ScStatus ScMemoryCreateShared(const ScSharedDesc *desc,ScSharedMemory *out)
{
    ScStatus error=sc32_record_in(desc,sizeof(*desc));
    if(!error)error=sc32_out(out,4);
    if(error)return broker_invalid(error,SC32_B_SHARED_CREATE);
    ScSharedInfo reply;
    broker_record(&reply,sizeof(reply));
    ScI32 raw=broker_gate(SC32_B_SHARED_CREATE,desc,&reply);
    ScStatus validation=0;
    if(raw==SC32_PENDING)raw=broker_shared_finish(SC32_B_SHARED_STEP,SC32_B_SHARED_CLOSE,reply.handle,&reply,out,&validation);
    if(validation)return broker_invalid(validation,SC32_B_SHARED_CREATE);
    if(raw)return broker_result(raw,SC32_B_SHARED_CREATE);
    *out=reply.handle;
    return 0;
}

ScStatus ScMemoryGrantShared(ScSharedMemory memory,const ScProcessRef *target,ScU32 offset,ScU32 bytes,ScU32 access,ScU32 flags,ScSharedGrantInfo *out)
{
    ScStatus error=sc32_record_in(target,sizeof(*target));
    if(!error)error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_SHARED_GRANT);
    Sc32SharedGrant input={48,1,memory,target->pid,target->generation,offset,bytes,access,flags,{0,0,0}};
    return broker_result(broker_gate(SC32_B_SHARED_GRANT,&input,out),SC32_B_SHARED_GRANT);
}

ScStatus ScMemoryOpenShared(ScSharedGrant grant,ScSharedMemory *out)
{
    ScStatus error=sc32_out(out,4);
    if(error)return broker_invalid(error,SC32_B_SHARED_OPEN);
    ScSharedInfo reply;
    broker_record(&reply,sizeof(reply));
    ScI32 raw=broker_handle(SC32_B_SHARED_OPEN,grant,0,&reply);
    if(raw)return broker_result(raw,SC32_B_SHARED_OPEN);
    *out=reply.handle;
    return 0;
}

ScStatus ScMemoryRevokeShared(ScSharedGrant grant){return broker_close(SC32_B_SHARED_REVOKE,grant);
}

ScStatus ScMemoryMapShared(ScSharedMemory memory,ScU32 offset,ScU32 bytes,ScU32 access,ScSharedMappingInfo *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_SHARED_MAP);
    Sc32SharedMap input={32,1,memory,offset,bytes,access,{0,0}};
    ScSharedMappingInfo reply;
    broker_record(&reply,sizeof(reply));
    ScI32 raw=broker_gate(SC32_B_SHARED_MAP,&input,&reply);
    ScStatus validation=0;
    if(raw==SC32_PENDING)raw=broker_shared_finish(SC32_B_SHARED_MAP_STEP,SC32_B_SHARED_UNMAP,reply.handle,&reply,out,&validation);
    if(validation)return broker_invalid(validation,SC32_B_SHARED_MAP);
    if(raw)return broker_result(raw,SC32_B_SHARED_MAP);
    sc32_copy(out,&reply,sizeof(*out));
    return 0;
}

ScStatus ScMemoryUnmapShared(ScSharedMapping mapping){return broker_close(SC32_B_SHARED_UNMAP,mapping);
}

ScStatus ScMemoryQueryShared(ScSharedMemory memory,ScSharedInfo *out)
{
    ScStatus error=sc32_record_out(out,sizeof(*out));
    if(error)return broker_invalid(error,SC32_B_SHARED_QUERY);
    return broker_result(broker_handle(SC32_B_SHARED_QUERY,memory,0,out),SC32_B_SHARED_QUERY);
}

ScStatus ScMemoryCloseShared(ScSharedMemory memory){return broker_close(SC32_B_SHARED_CLOSE,memory);
}
