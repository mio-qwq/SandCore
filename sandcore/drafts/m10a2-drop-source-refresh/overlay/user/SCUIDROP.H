#ifndef SANDCORE_SCUIDROP_H
#define SANDCORE_SCUIDROP_H
#include "SCUI.H"

/* flags0仍是原立即提交合同；只有显式选择才协商v2异步业务结果。 */
enum {
    SC_UI_DROP_ABI_MAJOR=1,SC_UI_DROP_ABI_MINOR=4,SC_UI_DROP_LIBRARY_VERSION=5,
    SC_UI_DRAG_DEFERRED=1,
    SC_UI_DRAG_HANDOFF=7,
    SC_UI_DROP_HANDSHAKE=1,SC_UI_DROP_READY=2,SC_UI_DROP_REPLYING=3,
    SC_UI_DROP_SENT=4,SC_UI_DROP_FAILED=5,
    SC_UI_DROP_COMMITTED=1,SC_UI_DROP_SOURCE_CHANGED=2
};
typedef struct {ScU32 size,version,id,generation;} ScUiDropReceipt;
typedef struct {
    ScU32 size,version,action;ScI32 status;
    ScU32 flags,items_total,items_durable,source_changed,retained,failed,skipped,uncertain;
    ScU32 reserved[2];
} ScUiDropResult;
typedef struct {
    ScU32 size,version,state;ScI32 status;
    /* 只借本人已验证TRANSFER的不可变副本，直到Release/本context退出。 */
    ScUiDragData transfer;
    ScUiDropResult result;
} ScUiDropReceiptInfo;
/* v2源HANDOFF/带结果终态的ScUiEvent.command是该次Begin输出token，
 * 防止晚到结果清掉下一次物理拖动；不是菜单command，ScUiDragData字节不变。
 * Defer只接受当前真实TRANSFER回调中的event；成功只取得receipt，不代表READY。
 * Query包括FAILED终态仍返回有效快照。Reply重复相同结果幂等，发送完成才返回0，
 * 期间返回SC_UI_PENDING；Release未完成对象等同取消，绝不宣称回滚已持久文件。 */
ScStatus ScUiDropDefer(ScUiContext *,const ScUiEvent *,ScUiDropReceipt *out);
ScStatus ScUiDropQuery(ScUiContext *,const ScUiDropReceipt *,ScUiDropReceiptInfo *out);
ScStatus ScUiDropReply(ScUiContext *,const ScUiDropReceipt *,const ScUiDropResult *);
ScStatus ScUiDropRelease(ScUiContext *,ScUiDropReceipt *);
#endif
