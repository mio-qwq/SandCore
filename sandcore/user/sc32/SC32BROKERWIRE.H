#ifndef SANDCORE_SC32BROKERWIRE_H
#define SANDCORE_SC32BROKERWIRE_H
#include "SC32WIRE.H"
#include "../SC32BROKER.H"

/* selector与公共Error.operation是两个域；原SCAPI忽略的寄存器原样保留。 */
enum {
    SC32_B_CLIP_BEGIN=0x300u,
    SC32_B_CLIP_OFFER=0x301u,
    SC32_B_CLIP_COMMIT=0x302u,
    SC32_B_CLIP_ABORT=0x303u,
    SC32_B_CLIP_OPEN=0x304u,
    SC32_B_CLIP_FORMATS=0x305u,
    SC32_B_CLIP_READ=0x306u,
    SC32_B_CLIP_CLOSE=0x307u,
    SC32_B_CLIP_CLEAR=0x308u,
    SC32_B_CLIP_QUERY=0x309u,
    SC32_B_CLIP_WATCH=0x30au,
    SC32_B_CLIP_OFFER_BEGIN=0x30bu,
    SC32_B_CLIP_OFFER_WRITE=0x30cu,
    SC32_B_CLIP_OFFER_FINISH=0x30du,
    SC32_B_CLIP_OFFER_ABORT=0x30eu,
    SC32_B_IPC_CREATE=0x400u,
    SC32_B_IPC_LISTEN=0x401u,
    SC32_B_IPC_CONNECT=0x402u,
    SC32_B_IPC_ACCEPT=0x403u,
    SC32_B_IPC_SEND=0x404u,
    SC32_B_IPC_PEEK=0x405u,
    SC32_B_IPC_RECEIVE=0x406u,
    SC32_B_IPC_PEER=0x407u,
    SC32_B_IPC_QUERY=0x408u,
    SC32_B_IPC_CLOSE=0x409u,
    SC32_B_READY=0x40au,
    SC32_B_WAIT=0x40bu,
    SC32_B_SHARED_CREATE=0x500u,
    SC32_B_SHARED_STEP=0x501u,
    SC32_B_SHARED_GRANT=0x502u,
    SC32_B_SHARED_OPEN=0x503u,
    SC32_B_SHARED_REVOKE=0x504u,
    SC32_B_SHARED_MAP=0x505u,
    SC32_B_SHARED_MAP_STEP=0x506u,
    SC32_B_SHARED_UNMAP=0x507u,
    SC32_B_SHARED_QUERY=0x508u,
    SC32_B_SHARED_CLOSE=0x509u,
    SC32_B_CLIP_OPERATION=0xb00u,
    SC32_B_IPC_OPERATION=0xc00u,
    SC32_B_SHARED_OPERATION=0xd00u,
    SC32_B_STEP_PAGES=16u,
    SC32_B_SHARED_WINDOW_BASE=0x60000000u,
    SC32_B_SHARED_WINDOW_TOP=0x64000000u,
    SC32_B_SHARED_PTE=0x800u
};


typedef struct {ScU32 size,version,handle,value;} Sc32BrokerHandle;
typedef struct {ScU32 size,version,handle,value;} Sc32BrokerToken;
typedef struct {ScU32 size,version,handle,format,data,bytes;} Sc32ClipboardOffer;
typedef struct {ScU32 size,version,handle,format,total_bytes,reserved;} Sc32ClipboardOfferBegin;
typedef struct {ScU32 size,version,handle,data,bytes,offset;} Sc32ClipboardOfferWrite;
typedef struct {ScU32 size,version,handle,after,rows,capacity,reserved[2];} Sc32ClipboardFormats;
typedef struct {ScU32 size,version,handle,format,offset,data,bytes,reserved;} Sc32ClipboardRead;
typedef struct {
    ScU32 size,version,namespace_id,session,name,queue_budget_bytes,max_frame_bytes,pending_budget_bytes,acl_flags;
    ScI32 allowed_uid,allowed_gid;
    ScU32 allowed_realm,allowed_session,flags,reserved[2];
} Sc32IpcCreate;
typedef struct {ScU32 size,version,name,namespace_id,session,flags,reserved[2];} Sc32IpcConnect;
typedef struct {ScU32 size,version,handle,state;ScBrokerIdentity peer;} Sc32IpcAccepted;
typedef struct {ScU32 size,version,handle,type,data,bytes;} Sc32IpcSend;
typedef struct {ScU32 size,version,handle,data,bytes,reserved;} Sc32IpcReceive;
typedef struct {
    ScU32 size,version,handle,target_pid,target_generation,offset,bytes,access,flags,reserved[3];
} Sc32SharedGrant;
typedef struct {ScU32 size,version,handle,offset,bytes,access,reserved[2];} Sc32SharedMap;

/* 固定输入精确size/version1；所有out先验整个caller声明RW范围。
 * clip/IPC每帧至多64KiB，零span允许NULL；copy消息Receive成功后才消费。
 * 执行失败不写out，例外为Shared短步的内部PENDING/FAILED记录：
 * 先把候选opaque token交私有wrapper，wrapper每步让出并全范围重验，
 * public out只在READY发布；失败候选Close/Unmap或EXIT统一撤销。
 * WAIT out必须NULL，只存可信陷入帧，EAX返回事件位或稳定负错误域。 */
#endif
