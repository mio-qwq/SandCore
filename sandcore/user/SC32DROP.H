#ifndef SANDCORE_SC32DROP_H
#define SANDCORE_SC32DROP_H
#include "SC32BROKER.H"
#include "SC32GUI.H"

/* OS只核实真实窗口/身份/输入资格；文件提交与删源属于应用事务。
 * 小整数用enum，避免正式SDK继续挤占CPP宏工作区。 */
enum {
    SC32_CAP_DROP_TARGETS=4194304u,
    SC32_DROP_COPY=1u,SC32_DROP_MOVE=2u,SC32_DROP_FORMAT_LIMIT=64u,
    SC32_DROP_CAPTURED=1u,SC32_DROP_HANDED_OFF=2u,
    SC32_DRAG_OFFER=1u,SC32_DRAG_ACCEPT=2u,SC32_DRAG_TRANSFER=3u,
    SC32_DRAG_COMPLETE=4u,SC32_DRAG_CANCEL=5u,SC32_DRAG_REJECT=6u
};
typedef struct {ScU32 size,version,target_id,target_generation;} ScDropTargetRef;
typedef struct {ScU32 size,version,ticket_lo,ticket_hi;} ScDropToken;
typedef struct {
    ScU32 size,version,window,generation,endpoint,actions;
    const ScU32 *formats;
    ScU32 format_count,flags,reserved[3];
} ScDropTargetDesc;
typedef struct {
    ScU32 size,version,source_window,source_generation;
    ScI32 screen_x,screen_y;
    ScU32 actions,flags;
} ScDropPoint;
typedef struct {
    ScU32 size,version,target_id,target_generation,window,generation,endpoint,actions;
    ScI32 client_x,client_y;
    ScU32 geometry_epoch,visibility_epoch;
    ScBrokerIdentity identity;
    ScU32 ticket_lo,ticket_hi,expires_lo,expires_hi,source_window,source_generation,flags,reserved;
    /* 真实源进程身份用于与Packet.sender/Peer核票，不采纳IPC正文自报PID。 */
    ScU32 source_pid,source_pid_generation,reserved2[2];
} ScDropInfo;
typedef struct {
    ScU32 size,version,ticket_lo,ticket_hi,source_window,source_generation,target_id,target_generation;
} ScDropValidateRequest;
ScStatus ScDropRegister(const ScDropTargetDesc *desc,ScDropTargetRef *out);
ScStatus ScDropUnregister(ScWindow window,ScU32 generation);
ScStatus ScDropAt(const ScDropPoint *point,ScDropInfo *out);
ScStatus ScDropValidate(const ScDropValidateRequest *ticket,ScDropInfo *out);
ScStatus ScDropConnect(const ScDropValidateRequest *ticket,ScIpcChannel *out);
ScStatus ScDropComplete(const ScDropToken *ticket,ScU32 action);
ScStatus ScDropCancel(const ScDropToken *ticket);

/* IPC正文不携对方指针。Offer表的offset相对整份Offer body，表/各正文
 * 都先检查范围及不重叠；帧真实sender只认ScIpcPacket.sender。 */
typedef struct {ScU32 size,version,count,total_bytes;} ScDragOffer;
typedef struct {ScU32 format,offset,bytes,flags;} ScDragFormat;
typedef struct {
    ScU32 size,version,type,nonce_lo,nonce_hi,source_window,source_generation,target_id,target_generation;
    ScU32 ticket_lo,ticket_hi,format,allowed_actions,chosen_action,body_bytes;
    ScI32 status;
} ScDragEnvelope;

#endif
