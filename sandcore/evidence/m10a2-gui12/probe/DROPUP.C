#include <SCUI.H>
#include <SC32DROP2.H>
#include <stdio.h>
#include <string.h>

/* 只测试新UP授权的真实输入与拒绝边界，不伪造系统消息或写客体内存。
 * 两个普通窗口和一个本人endpoint；没有文件复制、权限变更或后台任务。 */
static ScUiContext *ui;
static ScUiNode source_root,target_root,source_button;
static ScWindowInfo source_window,target_window;
static ScIdentityInfo identity;
static ScProcessRef self;
static ScIpcEndpoint endpoint;
static ScU32 registered,armed,completed,failures;

static ScUiText text(const char *value)
{ScUiText result={sizeof(result),1,value,(ScU32)strlen(value)};return result;}

static void check(const char *name,ScStatus actual,ScStatus expected,ScU32 intact)
{
    ScU32 pass=actual==expected && intact;
    if(!pass)failures++;
    printf("DROPUP CHECK %s actual=%d expected=%d intact=%u pass=%u\n",name,actual,expected,intact,pass);
}

static void reject(const char *name,const ScDropReleasePoint *point,ScStatus expected)
{
    ScDropInfo before,after;memset(&before,0xa5,sizeof(before));
    before.size=sizeof(before);before.version=1;after=before;
    ScStatus status=ScDropAtRelease(point,&after);
    check(name,status,expected,memcmp(&before,&after,sizeof(before))==0);
}

static ScDropReleasePoint point_for(const ScMessage *message)
{
    ScDropReleasePoint point;memset(&point,0,sizeof(point));point.size=sizeof(point);point.version=1;
    point.source_window=source_window.window;point.source_generation=source_window.generation;
    ScI32 border=(source_window.width-(ScI32)source_window.client_width)/2;
    ScI32 title=source_window.height-(ScI32)source_window.client_height-border;
    point.screen_x=source_window.x+border+(ScI32)message->data[0];
    point.screen_y=source_window.y+title+(ScI32)message->data[1];
    point.actions=SC32_DROP_COPY|SC32_DROP_MOVE;point.input_epoch=message->epoch;
    point.sequence_lo=message->sequence_lo;point.sequence_hi=message->sequence_hi;return point;
}

static ScStatus source_event(ScUiContext *context,const ScUiEvent *event,void *unused)
{
    (void)unused;
    if(!event->message || !(event->flags&SC_UI_EVENT_DEVICE) || !(event->message->data[3]&1u))return 0;
    if(event->type==SC_UI_EVENT_POINTER_DOWN && (event->buttons&1u)){
        ScStatus status=ScUiCapture(context,source_button,1);check("capture",status,0,1);
        armed=status==0;
        if(armed){ScDropReleasePoint point=point_for(event->message);reject("no-UP",&point,SC32_E_STALE);}
        return SC_UI_HANDLED;
    }
    if(event->type!=SC_UI_EVENT_POINTER_UP || !armed || completed)return 0;
    completed=1;ScDropReleasePoint point=point_for(event->message),bad=point;
    bad.sequence_lo^=1u;reject("wrong-sequence",&bad,SC32_E_STALE);
    bad=point;bad.screen_x++;reject("wrong-point",&bad,SC32_E_STALE);
    bad=point;bad.reserved=1;reject("reserved",&bad,SC32_E_INVALID);
    ScDropInfo result;memset(&result,0,sizeof(result));result.size=sizeof(result);result.version=1;
    ScStatus status=ScDropAtRelease(&point,&result);
    check("actual-UP",status,0,status==0 && (result.flags&SC32_DROP_HANDED_OFF)
        && result.source_pid==self.pid && result.source_pid_generation==self.generation
        && result.window==target_window.window && result.generation==target_window.generation
        && result.identity.session==identity.session);
    if(status==0){
        reject("replayed-UP",&point,SC32_E_STALE);
        ScDropToken token={sizeof(token),1,result.ticket_lo,result.ticket_hi};
        check("cancel-ticket",ScDropCancel(&token),0,1);
    }
    check("release-capture",ScUiCapture(context,source_button,0),0,1);
    printf("DROPUP COMPLETE uid=%d session=%u checks_failed=%u\n",identity.uid,identity.session,failures);
    ScUiContextStop(context,failures?1:0);return SC_UI_HANDLED;
}

static ScStatus root_event(ScUiContext *context,const ScUiEvent *event,void *unused)
{
    (void)unused;
    if(event->type==SC_UI_EVENT_CLOSE_REQUEST || (event->type==SC_UI_EVENT_KEY_DOWN && event->key==27)){
        ScUiContextStop(context,2);return SC_UI_VETO;
    }
    return 0;
}

static ScStatus listen(ScUiNode node,ScUiEventProc procedure)
{
    ScUiSubscription desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.phases=SC_UI_TARGET_PHASE;desc.procedure=procedure;ScU32 token;
    return ScUiListen(ui,node,&desc,&token);
}

static ScStatus window(const char *title,ScI32 x,ScUiNode *root,ScUiNode *child,ScU32 kind,const char *label)
{
    ScUiWindowDesc desc;memset(&desc,0,sizeof(desc));desc.size=sizeof(desc);desc.version=1;
    desc.title=text(title);desc.x=x;desc.y=180;desc.client_width=360;desc.client_height=180;desc.style=SC32_STYLE_STANDARD;
    ScStatus status=ScUiWindowCreate(ui,&desc,root);
    ScUiLayoutDesc layout;memset(&layout,0,sizeof(layout));layout.size=sizeof(layout);layout.version=1;
    layout.mode=SC_UI_OVERLAY;layout.width=layout.height=layout.max_width=layout.max_height=SC_UI_AUTO;
    layout.column_span=layout.row_span=1;layout.align_h=layout.align_v=SC_UI_STRETCH;
    for(ScU32 i=0;i<4;i++)layout.padding[i]=12;
    if(status==0)status=ScUiNodeSetLayout(ui,*root,&layout);
    ScUiWidgetDesc widget;memset(&widget,0,sizeof(widget));widget.size=sizeof(widget);widget.version=1;
    widget.kind=kind;widget.parent=*root;widget.text=text(label);widget.flags=SC_UI_FOCUSABLE|SC_UI_TABSTOP;
    if(status==0)status=ScUiWidgetCreate(ui,&widget,child);
    for(ScU32 i=0;i<4;i++)layout.padding[i]=0;
    if(status==0)status=ScUiNodeSetLayout(ui,*child,&layout);
    if(status==0)status=listen(*root,root_event);
    if(status==0)status=ScUiLayout(ui,*root);return status;
}

int main(void)
{
    memset(&identity,0,sizeof(identity));identity.size=sizeof(identity);identity.version=1;
    ScStatus status=ScIdentityQuery(&identity);
    memset(&self,0,sizeof(self));self.size=sizeof(self);self.version=1;
    if(status==0)status=ScProcessSelf(&self);
    if(status<0 || identity.uid!=1000 || identity.realm!=0){printf("DROPUP WRONG IDENTITY status=%d uid=%d\n",status,identity.uid);return 1;}
    ScUiContextDesc context;memset(&context,0,sizeof(context));context.size=sizeof(context);context.version=1;
    context.cache_budget_bytes=524288;context.undo_budget_bytes=4096;context.scheduling_quantum=64;
    status=ScUiContextCreate(&context,&ui);ScUiNode label=0;
    if(status==0)status=window("UP 源窗口",200,&source_root,&source_button,SC_UI_BUTTON,"按住并拖到右侧目标");
    if(status==0)status=listen(source_button,source_event);
    if(status==0)status=window("UP 目标窗口",800,&target_root,&label,SC_UI_LABEL,"在这里松开鼠标");
    memset(&source_window,0,sizeof(source_window));source_window.size=sizeof(source_window);source_window.version=1;
    memset(&target_window,0,sizeof(target_window));target_window.size=sizeof(target_window);target_window.version=1;
    if(status==0)status=ScUiWindowNative(ui,source_root,&source_window);
    if(status==0)status=ScUiWindowNative(ui,target_root,&target_window);
    ScIpcEndpointDesc ipc;memset(&ipc,0,sizeof(ipc));ipc.size=sizeof(ipc);ipc.version=1;
    ipc.namespace_id=SC32_IPC_NAMESPACE_SESSION;ipc.session=identity.session;ipc.name="m10a2.release.probe";
    ipc.queue_budget_bytes=ipc.max_frame_bytes=ipc.pending_budget_bytes=65536;
    ipc.acl_flags=SC32_IPC_ACL_SESSION|SC32_IPC_ACL_UID;ipc.allowed_uid=identity.uid;ipc.allowed_session=identity.session;
    if(status==0)status=ScIpcCreate(&ipc,&endpoint);
    if(status==0)status=ScIpcListen(endpoint);
    ScU32 format=SC32_CLIPBOARD_TEXT;ScDropTargetDesc target;memset(&target,0,sizeof(target));target.size=sizeof(target);target.version=1;
    target.window=target_window.window;target.generation=target_window.generation;target.endpoint=endpoint;
    target.actions=SC32_DROP_COPY|SC32_DROP_MOVE;target.formats=&format;target.format_count=1;
    ScDropTargetRef reference;memset(&reference,0,sizeof(reference));reference.size=sizeof(reference);reference.version=1;
    if(status==0){status=ScDropRegister(&target,&reference);registered=status==0;}
    ScI32 exit_code=1;
    if(status==0){printf("DROPUP READY uid=%d session=%u source=%u target=%u\n",identity.uid,identity.session,source_window.window,target_window.window);
        status=ScUiContextRun(ui,&exit_code);}
    if(registered)ScDropUnregister(target_window.window,target_window.generation);
    if(endpoint)ScIpcClose(endpoint);
    if(source_root)ScUiNodeDestroy(ui,source_root);
    if(target_root)ScUiNodeDestroy(ui,target_root);
    if(ui)ScUiContextRelease(ui);
    if(status<0)printf("DROPUP ERROR %d\n",status);
    return status<0 || !completed?1:exit_code;
}
