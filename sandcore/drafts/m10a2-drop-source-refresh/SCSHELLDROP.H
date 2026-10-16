#ifndef SANDCORE_SCSHELLDROP_H
#define SANDCORE_SCSHELLDROP_H
#include "SCSHELL.H"
#include "SCUIDROP.H"
enum {SC_SHELL_DROP_ABI_MAJOR=1,SC_SHELL_DROP_ABI_MINOR=2,SC_SHELL_DROP_LIBRARY_VERSION=3,
      SC_SHELL_DRAG_DEFERRED=1};
/* 旧Begin不变。Ex只有显式flag才启用业务终态；调用者仍须真实物理拖动。 */
ScStatus ScShellDragBeginEx(ScUiContext *,ScUiNode,const ScShellFileItem *,ScU32 count,ScU32 actions,ScU32 flags,ScU32 *token);
/* 成功把receipt所有权转给绑定job并清caller id。原FILES正文仅取自receipt，
 * 不接收应用另造的offer或“成功”位；失败保caller receipt供Reply/Release。 */
ScStatus ScShellDropFilesDeferred(ScUiContext *,ScUiDropReceipt *,ScU32 action,const char *destination,const char *recovery,ScShellFileJob **out);
/* 只接受该入口绑定的job。每步最多审16项，END表示终态已送出并释放receipt；
 * PENDING不是完成。任意普通job返回INVALID，调用者不能拼SOURCE_CHANGED。 */
ScStatus ScShellDropReply(ScShellFileJob *);
#endif
