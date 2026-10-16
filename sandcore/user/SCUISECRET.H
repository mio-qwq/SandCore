#ifndef SANDCORE_SCUISECRET_H
#define SANDCORE_SCUISECRET_H
#include "SCUI.H"
enum {SC_UI_SECRET=27,SC_UI_SECRET_ARMED=1,SC_UI_SECRET_CONSUMING=2,SC_UI_SECRET_INVALIDATED=4};
typedef struct {ScU32 size,version,parent,max_bytes,flags,reserved[3];} ScUiSecretDesc;
typedef struct {ScU32 size,version,node,bytes,characters,maximum,revision,flags;ScU32 reserved[2];} ScUiSecretInfo;
/* bytes不含终止NUL；借用只在回调内有效，不准保存/日志/文件/命令行。
 * Consume即使回调失败也擦活动区与提交候选，不提供String/CopyText。 */
typedef ScStatus (*ScUiSecretProc)(ScUiContext *,ScUiNode,const char *data,ScU32 bytes,void *context);
typedef struct {ScU32 size,version,button,flags;ScUiSecretProc procedure;void *context;ScDropProc context_drop;ScU32 reserved[3];} ScUiSecretSubmit;
ScStatus ScUiSecretCreate(ScUiContext *,const ScUiSecretDesc *,ScUiNode *out);
ScStatus ScUiSecretQuery(ScUiContext *,ScUiNode,ScUiSecretInfo *out);
ScStatus ScUiSecretClear(ScUiContext *,ScUiNode);
ScStatus ScUiSecretConsume(ScUiContext *,ScUiNode,ScUiSecretProc,void *context);
/* button0/procedure0解除；同一按钮只能绑定一个Secret。绑定使失焦
 * 到该按钮产生最多两秒的专页提交事务，真实Command消费；其它失焦清空。 */
ScStatus ScUiSecretBindSubmit(ScUiContext *,ScUiNode,const ScUiSecretSubmit *);
#endif
