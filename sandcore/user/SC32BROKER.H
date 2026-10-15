#ifndef SANDCORE_SC32BROKER_H
#define SANDCORE_SC32BROKER_H
#include "SC32API.H"

/* 小整数以enum提供，避免公开SDK持续占满384项CPP宏表。 */
enum {
    SC32_BROKER_ID_HARDWARE_SYSTEM=1u,
    SC32_BROKER_REALM_NORMAL=0u,
    SC32_BROKER_REALM_SERVICE=1u,
    SC32_BROKER_REALM_SERIAL=2u,
    SC32_BROKER_REALM_KERNEL=3u,
    SC32_BROKER_EVENT=4u,
    SC32_BROKER_IO_MAX=65536u,
    SC32_BROKER_PAGE_MAX=64u,
    SC32_BROKER_ENUM_BEGIN=0u,
    SC32_CLIPBOARD_TEXT=1u,
    SC32_CLIPBOARD_FILES=2u,
    SC32_CLIPBOARD_CUSTOM_FIRST=0x10000u,
    SC32_CLIPBOARD_COPY=1u,
    SC32_CLIPBOARD_CUT=2u,
    SC32_CLIPBOARD_PIN_OBSERVATION=1u,
    SC32_CLIPBOARD_FINISHED=1u,
    SC32_CLIPBOARD_EMPTY=2u,
    SC32_BROKER_IO_END=1u,
    SC32_IPC_NAMESPACE_SESSION=0u,
    SC32_IPC_NAMESPACE_SYSTEM=1u,
    SC32_IPC_KIND_ENDPOINT=1u,
    SC32_IPC_KIND_CHANNEL=2u,
    SC32_IPC_ACL_SESSION=1u,
    SC32_IPC_ACL_UID=2u,
    SC32_IPC_ACL_GID=4u,
    SC32_IPC_ACL_REALM=8u,
    SC32_IPC_ACL_PUBLIC_NORMAL=16u,
    SC32_IPC_STATE_CREATED=1u,
    SC32_IPC_STATE_LISTENING=2u,
    SC32_IPC_STATE_CONNECTING=3u,
    SC32_IPC_STATE_CONNECTED=4u,
    SC32_IPC_STATE_CLOSED=5u,
    SC32_IPC_FRAME_DATA=1u,
    SC32_IPC_FRAME_EOF=2u,
    SC32_BROKER_READY_READ=1u,
    SC32_BROKER_READY_WRITE=2u,
    SC32_BROKER_READY_ACCEPT=4u,
    SC32_BROKER_READY_CLOSED=8u,
    SC32_SHARED_READ=1u,
    SC32_SHARED_WRITE=2u,
    SC32_SHARED_GRANT_CROSS_SESSION=1u,
    SC32_SHARED_READY=1u,
    SC32_SHARED_PREPARING=2u,
    SC32_SHARED_FAILED=3u
};

/* 公开cdecl对象合同。编号只标识本人能力，不能据它解内核指针或
 * 读别人的私有页；地址只有Map成功后才是当前进程的真实用户VA。 */
typedef ScU32 ScClipboardCandidate;
typedef ScU32 ScClipboardSnapshot;
typedef ScU32 ScIpcEndpoint;
typedef ScU32 ScIpcChannel;
typedef ScU32 ScSharedMemory;
typedef ScU32 ScSharedGrant;
typedef ScU32 ScSharedMapping;

typedef struct {
    ScU32 size,version,pid,generation;
    ScI32 uid,gid;
    ScU32 realm,session;
    ScI32 subject_uid,subject_gid;
    ScU32 flags,reserved;
} ScBrokerIdentity;
#define SC32_BROKER_ENUM_END 0xFFFFFFFFu

typedef struct {ScU32 size,version,budget_bytes,flags,reserved[4];} ScClipboardDesc;
typedef struct {ScU32 size,version,format,bytes;} ScClipboardFormatInfo;
typedef struct {
    ScU32 size,version,handle,epoch,formats,bytes,budget_bytes,flags;
    ScBrokerIdentity publisher;
    ScU32 reserved[4];
} ScClipboardInfo;
typedef struct {ScU32 size,version,snapshot,count,cursor,epoch,flags,reserved;} ScClipboardPage;
typedef struct {ScU32 size,version,handle,transferred,total_bytes,offset,flags,reserved;} ScBrokerIoResult;
/* FILES是意图。正文跟16B头及若干20B记录，每记录后跟唯一末尾NUL
 * 的绝对路径，再补零到4B；没有自动授予文件读取/删除或SYSTEM权限。
 * object_id仅在PIN_OBSERVATION时是活open-pin的观察，不谎称birth ID。 */
typedef struct {ScU32 size,version,count,operation;} ScClipboardFiles;
typedef struct {ScU32 record_bytes,path_bytes,object_id,revision,flags;} ScClipboardFileRow;
ScStatus ScClipboardBegin(const ScClipboardDesc *desc,ScClipboardCandidate *out);
ScStatus ScClipboardOffer(ScClipboardCandidate candidate,ScU32 format,const void *data,ScU32 bytes);
ScStatus ScClipboardCommit(ScClipboardCandidate candidate,ScU32 expected_epoch,ScClipboardInfo *out);
ScStatus ScClipboardAbort(ScClipboardCandidate candidate);
ScStatus ScClipboardOpen(ScClipboardInfo *out);
ScStatus ScClipboardFormats(ScClipboardSnapshot snapshot,ScU32 after,ScClipboardFormatInfo *rows,ScU32 capacity,ScClipboardPage *out);
ScStatus ScClipboardRead(ScClipboardSnapshot snapshot,ScU32 format,ScU32 offset,void *data,ScU32 bytes,ScBrokerIoResult *out);
ScStatus ScClipboardClose(ScClipboardSnapshot snapshot);
ScStatus ScClipboardClear(ScU32 expected_epoch,ScClipboardInfo *out);
ScStatus ScClipboardQuery(ScClipboardInfo *out);
ScStatus ScClipboardWatch(ScU32 enable,ScU32 *epoch);

typedef struct {
    ScU32 size,version,namespace_id,session;
    const char *name;
    ScU32 queue_budget_bytes,max_frame_bytes,pending_budget_bytes,acl_flags;
    ScI32 allowed_uid,allowed_gid;
    ScU32 allowed_realm,allowed_session,flags,reserved[2];
} ScIpcEndpointDesc;
typedef struct {
    ScU32 size,version;
    const char *name;
    ScU32 namespace_id,session,flags,reserved[2];
} ScIpcConnectDesc;
typedef struct {
    ScU32 size,version,handle,kind,state,epoch,queued_bytes,queued_frames;
    ScU32 queue_budget_bytes,max_frame_bytes,peer_closed,pending_connections,reserved[4];
} ScIpcInfo;
typedef struct {
    ScU32 size,version,type,bytes,flags,sequence_lo,sequence_hi,reserved;
    ScBrokerIdentity sender;
} ScIpcPacket;
ScStatus ScIpcCreate(const ScIpcEndpointDesc *desc,ScIpcEndpoint *out);
ScStatus ScIpcListen(ScIpcEndpoint endpoint);
ScStatus ScIpcConnect(const ScIpcConnectDesc *desc,ScIpcChannel *out);
ScStatus ScIpcAccept(ScIpcEndpoint endpoint,ScIpcChannel *out,ScBrokerIdentity *peer);
ScStatus ScIpcSend(ScIpcChannel channel,ScU32 type,const void *data,ScU32 bytes);
ScStatus ScIpcPeek(ScIpcChannel channel,ScIpcPacket *out);
ScStatus ScIpcReceive(ScIpcChannel channel,void *data,ScU32 capacity,ScIpcPacket *out);
ScStatus ScIpcPeer(ScIpcChannel channel,ScBrokerIdentity *out);
ScStatus ScIpcQuery(ScU32 handle,ScIpcInfo *out);
ScStatus ScIpcClose(ScU32 handle);

typedef struct {ScU32 size,version,handle,flags,epoch,read_bytes,write_bytes,reserved;} ScBrokerReadyInfo;
ScStatus ScBrokerReady(ScU32 handle,ScBrokerReadyInfo *out);
/* handle0/ready_flags0可只等待event_mask；INPUT包括实际GUI队列检查。
 * owner内核在同一IF=0步骤检查ready并安装等待，没有R3跨调用睡眠窗。
 * events只报唤醒事实，返回后仍重新Receive/Accept/Query，不预留消息。 */
typedef struct {ScU32 size,version,handle,ready_flags,minimum_write_bytes,event_mask,timeout,reserved;} ScBrokerWaitDesc;
ScStatus ScBrokerWait(const ScBrokerWaitDesc *desc,ScU32 *events);

typedef struct {ScU32 size,version,bytes,access,flags,reserved[3];} ScSharedDesc;
typedef struct {
    ScU32 size,version,handle,object_id,bytes,offset,access,state;
    ScU32 allocation_bytes,ready_pages,references,mappings,issuer_pid,issuer_generation,flags,reserved;
} ScSharedInfo;
typedef struct {
    ScU32 size,version,grant,target_pid,target_generation,access,offset,bytes;
} ScSharedGrantInfo;
typedef struct {
    ScU32 size,version,handle,object_id,address,bytes,offset,access,state,epoch,reserved[2];
} ScSharedMappingInfo;
/* Grant/Map的offset与bytes以4096B为粒度，权限只允许READ或READ|WRITE。
 * 页尾padding是本对象的清零容量，绝不映射到相邻对象。close reference
 * 不撤仍活的mapping；显式Unmap/进程退出先撤PTE，最后引用才还PF。 */
ScStatus ScMemoryCreateShared(const ScSharedDesc *desc,ScSharedMemory *out);
ScStatus ScMemoryGrantShared(ScSharedMemory memory,const ScProcessRef *target,ScU32 offset,ScU32 bytes,ScU32 access,ScU32 flags,ScSharedGrantInfo *out);
ScStatus ScMemoryOpenShared(ScSharedGrant grant,ScSharedMemory *out);
ScStatus ScMemoryRevokeShared(ScSharedGrant grant);
ScStatus ScMemoryMapShared(ScSharedMemory memory,ScU32 offset,ScU32 bytes,ScU32 access,ScSharedMappingInfo *out);
ScStatus ScMemoryUnmapShared(ScSharedMapping mapping);
ScStatus ScMemoryQueryShared(ScSharedMemory memory,ScSharedInfo *out);
ScStatus ScMemoryCloseShared(ScSharedMemory memory);

#endif
