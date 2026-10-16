"""只读当前main，输出局部前像固定补丁；不应用、不构建、不运行。"""
from pathlib import Path
import ast,difflib,hashlib,json
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
OLD={};BODIES={};NEW={}


def sha(body):return hashlib.sha256(body).hexdigest()
def source(name):
    if name not in OLD:
        body=(ROOT/name).read_bytes();BODIES[name]=body
        OLD[name]=body.decode('utf-8').replace('\r\n','\n');NEW[name]=OLD[name]
    return NEW[name]
def once(text,before,after):
    if text.count(before)!=1:raise ValueError('anchor not unique: '+before[:110])
    return text.replace(before,after,1)
def edit(name,before,after):NEW[name]=once(source(name),before,after)
def add(name,local):
    assert not (ROOT/name).exists(),name
    NEW[name]=(HERE/local).read_text(encoding='utf-8')
def manifest(name,version=None,dependency=None,exports=(),remove=(),imports=()):
    original=json.loads(source(name));value=json.loads(source(name))
    if version:value['abi_minor'],value['library_version']=version
    if dependency:
        for dep in value['dependencies']:
            if dep['name'] in dependency:dep['min_minor'],dep['min_version']=dependency[dep['name']]
    for symbol in exports:
        assert symbol not in [e['name'] for e in value['exports']]
        value['exports'].append(dict(name=symbol,kind='function',size=1))
    value['imports']=[e for e in value['imports'] if e['name'] not in remove]
    for symbol,provider in imports:
        if symbol not in [e['name'] for e in value['imports']]:value['imports'].append(dict(name=symbol,kind='function',dependency=provider,size=1))
    assert value.get('exports',[])[:len(original.get('exports',[]))]==original.get('exports',[])
    assert len(value['imports'])==len(set(e['name'] for e in value['imports']))
    NEW[name]=json.dumps(value,ensure_ascii=False,indent=2)+'\n'


def scui():
    add('user/SCUIDROP.H','SCUIDROP.H')
    add('user/scui/scui_drop_deferred.inc','scui_drop_deferred.inc')
    edit('user/scui/SCUIPRIVATE.H','#include "../SCUI.H"','#include "../SCUI.H"\n#include "../SCUIDROP.H"')
    p='user/scui/ui_data.c'
    edit(p,'    ScU32 listed;\n};\nstruct UiDrag',
        '    ScU32 listed;\n    UiDropPeer *receipt_next;\n'
        '    ScU32 receipt_id,receipt_state,deferred,allowed_actions;ScI32 receipt_status;\n'
        '    ScUiDropResult receipt_result;\n};\nstruct UiDrag')
    edit(p,'    ScU32 cancelled,captured;',
        '    ScU32 cancelled,captured;\n'
        '    ScU32 deferred_opt,deferred_stage,deferred_actions,deferred_notified;ScStatus deferred_failure;')
    edit(p,'static ScU32 ui_drag_nonce=1;',
        'static ScU32 ui_drag_nonce=1;\n#include "scui_drop_deferred.inc"')
    edit(p,'e->version==1 && e->type>=SC32_DRAG_OFFER && e->type<=SC32_DRAG_REJECT',
        '((e->version==1 && e->type>=SC32_DRAG_OFFER && e->type<=SC32_DRAG_REJECT) || '
        '(e->version==2 && e->type>=SC32_DRAG_OFFER && e->type<=UI_D2_FINAL))')
    edit(p,'    e.version=1;\n    e.body_bytes=bytes;',
        '    e.version=envelope->version==2?2:1;\n    e.body_bytes=bytes;')
    edit(p,'static void ui_drop_peer_close(UiDropPeer *p)\n{if(p->listed){p->listed=0;',
        'static void ui_drop_peer_close(UiDropPeer *p)\n{if(p->listed){ui_d2_fail(p,p->receipt_status<0?p->receipt_status:SC_UI_E_CANCELLED);p->listed=0;')
    edit(p,'    s=target->description.procedure(node->owner,&event,target->description.context);',
        '    const ScUiEvent *previous_drop_event=ui_drop_current_event;ui_drop_current_event=&event;\n'
        '    s=target->description.procedure(node->owner,&event,target->description.context);\n'
        '    ui_drop_current_event=previous_drop_event;')
    edit(p,'static ScStatus ui_drop_peer_step(UiDropPeer *p)\n{\n    ScStatus s;',
        'static ScStatus ui_drop_peer_step(UiDropPeer *p)\n{\n    if(p->deferred)return ui_d2_peer_step(p);\n    ScStatus s;')
    edit(p,'e->format!=p->format || e->chosen_action!=p->action)',
        'e->format!=p->format || e->chosen_action!=p->action || e->version!=p->envelope.version)')
    edit(p,'            s=ui_drop_callback(target,SC_UI_EVENT_DROP,&data,current.client_x,current.client_y);',
        '            UiDropPeer *previous_peer=ui_drop_current_peer;ui_drop_current_peer=p;\n'
        '            s=ui_drop_callback(target,SC_UI_EVENT_DROP,&data,current.client_x,current.client_y);\n'
        '            ui_drop_current_peer=previous_peer;')
    edit(p,'            p->phase=s<0?SC32_DRAG_REJECT:SC32_DRAG_COMPLETE;',
        '            if(s<0&&p->deferred)ui_d2_fail(p,s);\n'
        '            p->phase=s<0?SC32_DRAG_REJECT:p->deferred?SC32_DRAG_TRANSFER:SC32_DRAG_COMPLETE;')
    edit(p,'                p->action=data.chosen_action;',
        '                p->action=data.chosen_action;p->allowed_actions=actions;')
    edit(p,'SC32_DRAG_TRANSFER,p->format,p->information.actions,p->action,p->total,p->body,p->sender',
        'SC32_DRAG_TRANSFER,p->format,p->envelope.version==2?p->allowed_actions:p->information.actions,p->action,p->total,p->body,p->sender')
    edit(p,'    if(d->channel){ScIpcClose(d->channel);',
        '    if(d->channel && cancel && d->deferred_stage && !d->deferred_notified){\n'
        '        ScDragEnvelope reply=d->last;reply.type=SC32_DRAG_CANCEL;reply.format=d->format;reply.status=SC_UI_E_CANCELLED;\n'
        '        reply.chosen_action=0;ui_drop_send(d->channel,&reply,0,0);\n'
        '    }\n    if(d->channel){ScIpcClose(d->channel);')
    edit(p,'    ui_drag_disconnect(d,1);\n    UiNode *source=',
        '    if(d->deferred_opt&&!d->deferred_notified&&!quiet&&!ui_quiet){\n'
        '        ScUiDropResult result;ui_d2_result(&result,d->deferred_failure<0?d->deferred_failure:SC_UI_E_CANCELLED);\n'
        '        result.action=d->action;if(d->deferred_stage || d->phase==SC32_DRAG_COMPLETE)result.uncertain=1;\n'
        '        d->deferred_notified=1;ui_d2_notice(d,SC32_DRAG_CANCEL,&result);\n'
        '    }\n    ui_drag_disconnect(d,1);\n    UiNode *source=')
    edit(p,'desc->actions&~3u || desc->flags || !ui_data_reserved(desc->reserved,4)',
        'desc->actions&~3u || desc->flags&~SC_UI_DRAG_DEFERRED || !ui_data_reserved(desc->reserved,4)')
    edit(p,'if(drag->context==context)return SC_UI_E_BUSY;',
        'if(drag->context==context&&!drag->deferred_stage)return SC_UI_E_BUSY;')
    edit(p,'    d->actions=desc->actions;\n    d->token=',
        '    d->actions=desc->actions;d->deferred_opt=(desc->flags&SC_UI_DRAG_DEFERRED)!=0;\n    d->token=')
    edit(p,'    d->last=offer;', '    offer.version=d->deferred_opt?2:1;\n    d->last=offer;')
    edit(p,'static ScStatus ui_drag_source_step(UiDrag *d)\n{\n    ScWideCounter now;',
        'static ScStatus ui_drag_source_step(UiDrag *d)\n{\n    if(d->deferred_stage)return ui_d2_source_step(d);\n    ScWideCounter now;')
    edit(p,'if(!ui_drop_same_round(e,&d->last) || e->status<0)',
        'if(!ui_drop_same_round(e,&d->last) || e->version!=d->last.version || e->status<0)')
    edit(p,'    else if(d->phase==SC32_DRAG_COMPLETE && e->type==SC32_DRAG_COMPLETE',
        '''    else if(d->deferred_opt && d->phase==SC32_DRAG_COMPLETE && e->version==2 && e->type==UI_D2_HOLD){
        ScU32 allowed=d->actions&d->target.actions;
        if(e->format!=d->format || !e->allowed_actions || (e->allowed_actions&~allowed) || e->chosen_action!=d->action || !(e->allowed_actions&d->action))s=SC_UI_E_INVALID;
        else {ScDropToken ticket=ui_drop_token(&d->target);s=ScDropComplete(&ticket,d->action);
            if(s==0){d->deferred_stage=1;d->deferred_actions=e->allowed_actions;
                d->target.ticket_lo=d->target.ticket_hi=0;
                ScStatus notice=ui_d2_notice(d,SC_UI_DRAG_HANDOFF,0);if(notice<0)d->context->notification_error=notice;
                ui_d2_detach_pointer(d);
            }
        }
    }
    else if(d->phase==SC32_DRAG_COMPLETE && e->type==SC32_DRAG_COMPLETE''')
    edit(p,'        if(s==0){UiNode *source=ui_node(d->context,d->source);',
        '        if(s==0){d->deferred_notified=1;UiNode *source=ui_node(d->context,d->source);')
    edit(p,'if(budget && root->owner_live && peer->listed)',
        'if(budget && root->owner_live && peer->listed && ui_d2_peer_work(peer))')
    edit(p,'if(s==SC_UI_END || (s<0 && s!=SC32_E_AGAIN)){ui_drop_peer_close(peer);',
        'if(s==SC_UI_END || (s<0 && s!=SC32_E_AGAIN)){if(s<0)ui_d2_fail(peer,s);ui_drop_peer_close(peer);')
    edit(p,'if(drag->context==context && budget){',
        'if(drag->context==context && budget && ui_d2_source_work(drag)){')
    edit(p,'if(s==SC_UI_END || (s<0 && s!=SC32_E_AGAIN)){ui_drag_close(drag);',
        'if(s==SC_UI_END || (s<0 && s!=SC32_E_AGAIN)){if(s<0)drag->deferred_failure=s;ui_drag_close(drag);')
    edit(p,'if(d->context==root->owner && d->root==root->root)break;',
        'if(d->context==root->owner && d->root==root->root&&!d->deferred_stage)break;')
    start=NEW[p].index('static ScU32 ui_drag_wait(');end=NEW[p].index('static void ui_drag_state(',start)
    NEW[p]=NEW[p][:start]+'''static ScU32 ui_drag_wait(ScUiContext *context,ScU32 timeout)
{
    for(UiDrag *d=ui_drags;d;d=d->next)if(d->context==context){
        if(!d->deferred_stage){if(timeout>1)timeout=1;}
        else {if(ui_d2_source_work(d))return 0;if(d->deferred_stage==1)timeout=ui_d2_timeout(&d->deadline,timeout);}
    }
    for(UiDropRoot *r=ui_drop_roots;r;r=r->next)if(r->context==context)for(UiDropPeer *p=r->peers;p;p=p->next)if(p->listed){
        if(!p->deferred){if(timeout>1)timeout=1;}
        else {if(ui_d2_peer_work(p))return 0;if(p->deferred<3)timeout=ui_d2_timeout(&p->deadline,timeout);}
    }
    return timeout;
}
'''+NEW[p][end:]
    edit(p,'static void ui_drag_finish(void)\n{\n    while(ui_drags)',
        'static void ui_drag_finish(void)\n{\n'
        '    while(ui_drop_receipts){UiDropPeer *p=ui_drop_receipts;ScUiContext *owner=p->root->context;ui_drop_receipts=p->receipt_next;p->receipt_next=0;p->receipt_id=0;ui_object_release(&p->object);ScUiContextRelease(owner);}\n'
        '    while(ui_drags)')
    manifest('user/scui/SCUI.json',(4,5),exports=('ScUiDropDefer','ScUiDropQuery','ScUiDropReply','ScUiDropRelease'),imports=(('ScBrokerReady','SC32API.SCL'),))


def shell():
    add('user/SCSHELLDROP.H','SCSHELLDROP.H')
    add('user/scshell/shell_drop_deferred.inc','shell_drop_deferred.inc')
    p='user/scshell/SCSHELLPRIVATE.H'
    edit(p,'#include "SCSHELL.H"','#include "SCSHELL.H"\n#include "SCSHELLDROP.H"')
    edit(p,'    ScU32 clip_operation,closed,cancel_requested;unsigned char *scratch;',
        '    ScU32 clip_operation,closed,cancel_requested;unsigned char *scratch;\n'
        '    ScUiContext *drop_ui;ScUiDropReceipt drop_receipt;ScUiDropResult drop_result;\n'
        '    ScU32 drop_index,drop_reviewed,drop_done,drop_cancelled;')
    edit(p,'static void sh_menu_provider_release(ShProvider *);',
        'static void sh_menu_provider_release(ShProvider *);\n'
        'static void sh_drop_poll(ScShellFileJob *);\nstatic void sh_drop_release(ScShellFileJob *);')
    p='user/scshell/shell_job.c'
    edit(p,'    ScBufferRelease(job->clip);','    ScBufferRelease(job->clip);\n    sh_drop_release(job);')
    edit(p,'    if(job->closed)return 0;\n    sh_enter(&job->base);',
        '    if(job->closed)return 0;\n    sh_drop_poll(job);\n    sh_enter(&job->base);')
    p='user/scshell/shell_clipboard.c'
    edit(p,'ScStatus ScShellDragBegin(ScUiContext *ui,ScUiNode source,const ScShellFileItem *items,ScU32 count,ScU32 actions,ScU32 *token) {',
        'static ScStatus sh_drag_begin(ScUiContext *ui,ScUiNode source,const ScShellFileItem *items,ScU32 count,ScU32 actions,ScU32 flags,ScU32 *token) {')
    edit(p,'    if(!actions||actions&~3u||!token)return SC_SHELL_E_INVALID;',
        '    if(!actions||actions&~3u||flags&~SC_SHELL_DRAG_DEFERRED||!token)return SC_SHELL_E_INVALID;')
    edit(p,'    d.offer_bytes=n;\n    if(s>=0)s=ScUiDragBegin',
        '    d.offer_bytes=n;d.flags=flags&SC_SHELL_DRAG_DEFERRED?SC_UI_DRAG_DEFERRED:0;\n    if(s>=0)s=ScUiDragBegin')
    marker='ScStatus ScShellDropFiles(const ScUiDragData *data,'
    edit(p,marker,'''ScStatus ScShellDragBegin(ScUiContext *ui,ScUiNode source,const ScShellFileItem *items,ScU32 count,ScU32 actions,ScU32 *token)
{return sh_drag_begin(ui,source,items,count,actions,0,token);}
ScStatus ScShellDragBeginEx(ScUiContext *ui,ScUiNode source,const ScShellFileItem *items,ScU32 count,ScU32 actions,ScU32 flags,ScU32 *token)
{return sh_drag_begin(ui,source,items,count,actions,flags,token);}
'''+marker)
    NEW[p]+='\n#include "shell_drop_deferred.inc"\n'
    manifest('user/scshell/SCSHELL.json',(2,3),{'SCUI.SCL':(4,5)},
        exports=('ScShellDragBeginEx','ScShellDropFilesDeferred','ScShellDropReply'),
        imports=tuple((n,'SCUI.SCL') for n in ('ScUiDropQuery','ScUiDropReply','ScUiDropRelease')))


def files():
    add('user/files2/files2_drop_deferred.inc','files2_drop_deferred.inc')
    p='user/files2/FILES2.H'
    text=source(p)
    if '#include <SCSHELL.H>' in text:edit(p,'#include <SCSHELL.H>','#include <SCSHELLDROP.H>')
    else:edit(p,'#include "SCSHELL.H"','#include "SCSHELLDROP.H"')
    edit(p,'    ScU32 decision;','    ScU32 decision,deferred;')
    edit(p,'    ScUiDragData pending_drop;',
        '    ScUiDragData pending_drop;ScUiDropReceipt drop_receipt;ScU32 drop_stage;')
    p='user/files2/files2_actions.inc'
    edit(p,'ScShellDragBegin(f2.ui,f2.table,items,count,SC_UI_DRAG_COPY|SC_UI_DRAG_MOVE,&f2.drag_token)',
        'ScShellDragBeginEx(f2.ui,f2.table,items,count,SC_UI_DRAG_COPY|SC_UI_DRAG_MOVE,SC_SHELL_DRAG_DEFERRED,&f2.drag_token)')
    edit(p,'static ScStatus f2_drop_reply(', '#include "files2_drop_deferred.inc"\nstatic ScStatus f2_drop_reply(')
    start=NEW[p].index('static ScStatus f2_drop_reply(');end=NEW[p].index('static ScStatus f2_drop(',start)
    NEW[p]=NEW[p][:start]+'''static ScStatus f2_drop_reply(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;(void)context;if(event->type!=SC_UI_EVENT_DIALOG_COMPLETE)return 0;
    ScUiDialog *dialog=(ScUiDialog *)event->data;ScUiDialogInfo info;memset(&info,0,sizeof(info));info.size=sizeof(info);info.version=1;
    ScStatus s=ScUiDialogQuery(dialog,&info);
    if(s==0 && info.stage==SC_UI_DIALOG_DONE && (info.command==F2_DROP_COPY || info.command==F2_DROP_MOVE)){
        ScU32 action=info.command==F2_DROP_COPY?SC_UI_DRAG_COPY:SC_UI_DRAG_MOVE;
        ScPathInfo target;s=f2_query(f2.drop_destination,&target);
        if(s==0 && (target.kind!=SC32_PATH_DIRECTORY || target.generation!=f2.drop_revision))s=SC_SHELL_E_STALE;
        ScShellFileJob *job=0;if(s==0)s=f2_recovery();
        if(s==0)s=ScShellDropFilesDeferred(f2.ui,&f2.drop_receipt,action,f2.drop_destination,f2.recovery_path,&job);
        if(s==0){f2.drop_stage=0;s=f2_job_adopt(job);if(s==0)f2.jobs->deferred=1;}
        if(s<0)f2_drop_abort(s);
    }else f2_drop_abort(s<0?s:SC_UI_E_CANCELLED);
    f2_drop_forget();
    if(f2.dialog==dialog){f2.dialog=0;ScUiDialogRelease(dialog);}if(s<0)f2_error(s,f2.drop_destination);return 0;
}
'''+NEW[p][end:]
    edit(p,'if(data->phase==SC32_DRAG_COMPLETE){f2.drag_token=0;return 0;}',
        'if(f2_drop_notice(event,data))return 0;')
    edit(p,'if(f2.dialog || f2.scan.active || f2.exit_pending || !f2.current)',
        'if(f2.dialog || f2.drop_receipt.id || f2.scan.active || f2.exit_pending || !f2.current)')
    start=NEW[p].index('    ScUiMenuItem buttons[3];',NEW[p].index('static ScStatus f2_drop('))
    end=NEW[p].index('\n}\nstatic ScStatus f2_exit_reply',start)
    NEW[p]=NEW[p][:start]+'''    memset(&f2.drop_receipt,0,sizeof(f2.drop_receipt));f2.drop_receipt.size=sizeof(f2.drop_receipt);f2.drop_receipt.version=1;
    s=ScUiDropDefer(ui,event,&f2.drop_receipt);
    if(s<0){f2_drop_forget();return s;}
    f2.drop_stage=1;
    /* 只有READY才开模态；此回调仅接收正文，不把传输ACK当文件提交。 */
    return SC_UI_HANDLED;'''+NEW[p][end:]
    edit(p,'        if(job->reviewed && !job->conflict && (dialog==SC_SHELL_END',
        '''        if(job->reviewed && job->deferred){
            ScStatus ack=ScShellDropReply(job->handle);
            if(ack==SC_SHELL_READY)*ready=1;
            if(ack<0&&!first)first=ack;
            if(ack!=SC_SHELL_END){link=&job->next;continue;}
            job->deferred=0;
        }
        if(job->reviewed && !job->conflict && (dialog==SC_SHELL_END''')
    p='user/files2/files2_view.inc'
    edit(p,'if(drop->phase==SC32_DRAG_COMPLETE){f2.drag_token=0;return 0;}',
        'if(f2_drop_notice(event,drop))return 0;')
    edit(p,'    ScStatus first=f2_catalog_step(ready),s;',
        '    ScStatus first=f2_catalog_step(ready),s;\n    s=f2_drop_step(ready);if(s<0&&!first)first=s;')
    edit(p,'    ScHashRelease(f2.icons);ScHashRelease(f2.tree_index);ScUiContextRelease(f2.ui);',
        '    if(f2.drop_receipt.id)ScUiDropRelease(f2.ui,&f2.drop_receipt);\n'
        '    ScHashRelease(f2.icons);ScHashRelease(f2.tree_index);ScUiContextRelease(f2.ui);')
    # 退出检查也等待已确认job的真实结果发送，不能仅凭reviewed销receipt。
    edit(p,'if(!job->final)jobs_active=1;', 'if(!job->final || job->deferred)jobs_active=1;')
    manifest('user/files2/FILES2.json',dependency={'SCUI.SCL':(4,5),'SCSHELL.SCL':(2,3)},
        remove=('ScShellDragBegin','ScShellDropFiles'),imports=tuple((n,'SCSHELL.SCL') for n in
        ('ScShellDragBeginEx','ScShellDropFilesDeferred','ScShellDropReply'))+tuple((n,'SCUI.SCL') for n in
        ('ScUiDropDefer','ScUiDropQuery','ScUiDropReply','ScUiDropRelease')))


def desktop():
    add('user/scshell/desktop/desktop_drop_deferred.inc','desktop_drop_deferred.inc')
    p='user/scshell/desktop/DESKTOP.H'
    edit(p,'#include <SCSHELL.H>','#include <SCSHELLDROP.H>')
    edit(p,'    ScUiDragData pending_drop;void *drop_bytes;',
        '    ScUiDragData pending_drop;void *drop_bytes;\n'
        '    ScUiDropReceipt drop_receipt;ScUiDropResult drop_result;ScU32 drop_stage,drop_revision,job_deferred;')
    p='user/scshell/desktop/desktop_view.inc'
    NEW[p]='#include "desktop_drop_deferred.inc"\n'+source(p)
    edit(p,'if(data->phase==SC32_DRAG_COMPLETE){desk.external_drag=0;return 0;}',
        'if(desk_drop_notice(event,data))return 0;')
    p='user/scshell/desktop/desktop_actions.inc'
    edit(p,'ScShellDragBegin(desk.ui,desk.canvas,items,count,SC_UI_DRAG_COPY|SC_UI_DRAG_MOVE,&desk.external_drag)',
        'ScShellDragBeginEx(desk.ui,desk.canvas,items,count,SC_UI_DRAG_COPY|SC_UI_DRAG_MOVE,SC_SHELL_DRAG_DEFERRED,&desk.external_drag)')
    start=source(p).index('static ScStatus desk_drop_reply(');end=NEW[p].index('static ScStatus desk_drop(',start)
    block=NEW[p][start:end]
    block=once(block,'    if(s==0 && (info.command==DESK_DROP_COPY || info.command==DESK_DROP_MOVE)){',
        '''    if(s==0 && info.stage==SC_UI_DIALOG_DONE && info.command!=SC_UI_DIALOG_CANCEL){
        ScPathInfo target;memset(&target,0,sizeof(target));target.size=sizeof(target);target.version=1;
        s=ScPathQuery(desk.drop_destination,&target);
        if(s==0&&(target.kind!=SC32_PATH_DIRECTORY||target.generation!=desk.drop_revision))s=SC_SHELL_E_STALE;
    }
    if(s==0 && info.stage==SC_UI_DIALOG_DONE && (info.command==DESK_DROP_COPY || info.command==DESK_DROP_MOVE)){''')
    block=once(block,'        if(s==0)s=ScShellDropFiles(&desk.pending_drop,desk.drop_destination,desk.recovery_path,&job);\n        if(s==0)s=desk_job_display(job);',
        '        if(s==0)s=ScShellDropFilesDeferred(desk.ui,&desk.drop_receipt,desk.pending_drop.chosen_action,desk.drop_destination,desk.recovery_path,&job);\n'
        '        if(s==0){desk.drop_stage=0;s=desk_job_display(job);if(desk.job==job)desk.job_deferred=1;}')
    block=once(block,'    }else if(s==0 && info.command==DESK_DROP_LINK){',
        '    }else if(s==0 && info.stage==SC_UI_DIALOG_DONE && info.command==DESK_DROP_LINK){\n'
        '        memset(&desk.drop_result,0,sizeof(desk.drop_result));desk.drop_result.size=sizeof(desk.drop_result);desk.drop_result.version=1;desk.drop_result.action=SC_UI_DRAG_COPY;')
    block=once(block,'            ScU32 offset=sizeof(header);',
        '            desk.drop_result.items_total=header.count;\n            ScU32 offset=sizeof(header);')
    block=once(block,'ScShellShortcutRelease(link);if(s==0)desk.refresh_pending=1;offset+=row.record_bytes;',
        'ScShellShortcutRelease(link);if(s==0){if(!revision){s=SC_SHELL_E_UNKNOWN;break;}desk.drop_result.items_durable++;desk.refresh_pending=1;}offset+=row.record_bytes;')
    block=once(block,'    free(desk.drop_bytes);desk.drop_bytes=0;memset(&desk.pending_drop,0,sizeof(desk.pending_drop));',
        '''    if(desk.drop_receipt.id){
        if(info.command==DESK_DROP_LINK && desk.drop_result.items_total){
            desk.drop_result.status=s<0?s:0;
            if(s==0&&desk.drop_result.items_durable==desk.drop_result.items_total)desk.drop_result.flags=SC_UI_DROP_COMMITTED;
            else {desk.drop_result.failed=1;if(s>=0)desk.drop_result.status=SC_SHELL_E_UNKNOWN;}
            ScStatus replied=ScUiDropReply(desk.ui,&desk.drop_receipt,&desk.drop_result);desk.drop_stage=3;
            if(replied<0){ScUiDropRelease(desk.ui,&desk.drop_receipt);desk.drop_stage=0;if(s==0)s=replied;}
        }else desk_drop_abort(s<0?s:SC_UI_E_CANCELLED);
    }
    desk_drop_forget();''')
    NEW[p]=NEW[p][:start]+block+NEW[p][end:]
    edit(p,'if(desk.job || desk.dialog){data->chosen_action=0;',
        'if(desk.job || desk.dialog || desk.drop_receipt.id){data->chosen_action=0;')
    edit(p,'if(desk.job || desk.dialog)return SC_SHELL_E_BUSY;',
        'if(desk.job || desk.dialog || desk.drop_receipt.id)return SC_SHELL_E_BUSY;')
    start=NEW[p].index('        ScUiMenuItem buttons[4];',NEW[p].index('static ScStatus desk_drop('))
    end=NEW[p].index('\n    }\n    return 0;\n}',start)
    NEW[p]=NEW[p][:start]+'''        ScPathInfo metadata;memset(&metadata,0,sizeof(metadata));metadata.size=sizeof(metadata);metadata.version=1;
        ScStatus s=ScPathQuery(desk.drop_destination,&metadata);
        if(s==0&&metadata.kind!=SC32_PATH_DIRECTORY)s=SC_SHELL_E_INVALID;
        if(s==0){desk.drop_revision=metadata.generation;memset(&desk.drop_receipt,0,sizeof(desk.drop_receipt));
            desk.drop_receipt.size=sizeof(desk.drop_receipt);desk.drop_receipt.version=1;s=ScUiDropDefer(ui,event,&desk.drop_receipt);}
        if(s<0){desk_drop_forget();return s;}
        desk.drop_stage=1;return SC_UI_HANDLED;'''+NEW[p][end:]
    p='user/scshell/desktop/SCSHELL.C'
    edit(p,'        ScStatus activated=desk_activate();',
        '        ScStatus drop_step=desk_drop_step(&business_ready);if(drop_step<0)desk_error(drop_step,desk.drop_destination);\n'
        '        ScStatus activated=desk_activate();')
    edit(p,'                if(dialog==SC_SHELL_END || (info.state==SC_SHELL_JOB_FINISHED',
        '''                ScU32 drop_wait=0;
                if(desk.job_deferred){ScStatus ack=ScShellDropReply(desk.job);
                    if(ack==SC_SHELL_READY)business_ready=1;
                    if(ack==SC_SHELL_END)desk.job_deferred=0;else drop_wait=1;
                    if(ack<0)desk_error(ack,desk.drop_destination);
                }
                if(!drop_wait && (dialog==SC_SHELL_END || (info.state==SC_SHELL_JOB_FINISHED''')
    edit(p,'                    && !(info.flags&SC_SHELL_SOURCE_RETAINED))){',
        '                    && !(info.flags&SC_SHELL_SOURCE_RETAINED)))){')
    edit(p,'    ScShellCatalogRelease(desk.catalog);ScUiContextRelease(desk.ui);',
        '    if(desk.drop_receipt.id)ScUiDropRelease(desk.ui,&desk.drop_receipt);\n'
        '    ScShellCatalogRelease(desk.catalog);ScUiContextRelease(desk.ui);')
    manifest('user/scshell/desktop/SCSHELL.json',dependency={'SCUI.SCL':(4,5),'SCSHELL.SCL':(2,3)},
        remove=('ScShellDragBegin','ScShellDropFiles'),imports=tuple((n,'SCSHELL.SCL') for n in
        ('ScShellDragBeginEx','ScShellDropFilesDeferred','ScShellDropReply'))+tuple((n,'SCUI.SCL') for n in
        ('ScUiDropDefer','ScUiDropQuery','ScUiDropReply','ScUiDropRelease')))


def docs_sdk():
    add('docs/SCUI-DROP-DEFERRED.md','SCUI-DROP-DEFERRED.md')
    p='m10a2-apps.mk'
    edit(p,'M10A2_APP_HEADERS := $(wildcard user/SC*.H user/sclib/*.H)',
        'M10A2_APP_HEADERS := $(filter-out user/SCUIDROP.H user/SCSHELLDROP.H,$(wildcard user/SC*.H user/sclib/*.H))')
    edit(p,'$(BUILD)/files2-native.o: user/files2/FILES2.C ',
        '$(BUILD)/files2-native.o: user/files2/FILES2.C user/SCUIDROP.H user/SCSHELLDROP.H ')
    p='m10a2.mk'
    edit(p,'$(wildcard user/scui/*.c user/scui/*.H)',
        'user/SCUIDROP.H $(wildcard user/scui/*.c user/scui/*.H user/scui/*.inc)')
    edit(p,'$(wildcard user/scshell/*.c user/scshell/*.H)',
        'user/SCUIDROP.H user/SCSHELLDROP.H $(wildcard user/scshell/*.c user/scshell/*.H user/scshell/*.inc)')
    edit(p,'$(BUILD)/scshell-desktop.o: user/scshell/desktop/SCSHELL.C ',
        '$(BUILD)/scshell-desktop.o: user/scshell/desktop/SCSHELL.C user/SCUIDROP.H user/SCSHELLDROP.H ')

    p='tools/stage_m10a2_sdk.py'
    edit(p,"    'SCUI.H': 'user/SCUI.H', 'SCLIB.H': 'user/sclib/SCLIB.H',",
        "    'SCUIDROP.H':'user/SCUIDROP.H', 'SCSHELLDROP.H':'user/SCSHELLDROP.H',\n"
        "    'SCUI.H': 'user/SCUI.H', 'SCLIB.H': 'user/sclib/SCLIB.H',")
    edit(p,"DOCS = ('M10A2-SDK.md','SCL.md'", "DOCS = ('SCUI-DROP-DEFERRED.md','M10A2-SDK.md','SCL.md'")
    p='docs/SCUI.md'
    edit(p,'公开入口是完全大写 [SCUI.H](../user/SCUI.H)，158个正式接口由Ring3 `SCUI.SCL` ABI1.0/library_version1实现。',
        '公开基础入口在完全大写 [SCUI.H](../user/SCUI.H)，异步Drop追加入口在 [SCUIDROP.H](../user/SCUIDROP.H)。本组源码manifest为174入口、Ring3 `SCUI.SCL` ABI1.4/library_version5；新增部分尚待统一构建与客体验证。')
    edit(p,'仅列真实158公开定义和实际导入', '保留原170公开入口并追加4项Drop定义，导入继续按真实对象核对')
    for p,addition in (
        ('docs/SCUI.md','SCUI ABI1.4/library5在独立SCUIDROP.H追加Defer/Query/Reply/Release，ScUiDragDesc显式flag才选v2，旧SCUI.H和flags0即时回调/ACK保持。新FILE拖放先HOLD/READY消费短传输票，再确认/真实job/最终结果；等待不强制1tick。详见SCUI-DROP-DEFERRED，隔离源码尚未构建或客体验证。'),
        ('docs/SCSHELL-DESIGN.md','SCSHELL ABI1.2/library3在独立SCSHELLDROP.H追加DragBeginEx、DropFilesDeferred和DropReply，旧96入口及SCSHELL.H保持；旧Begin严格flags0。新FILE路径只从认证receipt建绑定job，逐项DONE+DURABLE与RENAME/SOURCE_REMOVED证明来源改变，最后才通知一次视图失效，不据传输ACK删第二次。详见SCUI-DROP-DEFERRED；本组隔离源码，尚未构建或运行。'),
        ('docs/SC32-DROP.md','旧ScDropComplete仍只消费传输轮次、不证明文件提交。显式SCUI v2在HOLD/READY前消费30秒输入票，后续只在已认证channel/nonce/owner上传真实job终态，不延长旧票或增加内核权限。源视图只在最终结果后复核实际FS，旧v1语义保持。详见SCUI-DROP-DEFERRED，待统一构建与客体验证。')):
        NEW[p]=source(p)+'\n2026-10-16：'+addition+'\n'


def main():
    scui();shell();files();desktop();docs_sdk()
    pin_file=HERE/'PINS.json'
    previous=json.loads(pin_file.read_text(encoding='utf-8')) if pin_file.exists() else None
    if previous:
        for p,body in BODIES.items():
            assert p not in previous['modified'] or sha(body)==previous['modified'][p]['before_sha256'],p
    patches=[];pins=dict(status='SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED',modified={},added={})
    for p,text in NEW.items():
        path=HERE/'overlay'/p;path.parent.mkdir(parents=True,exist_ok=True)
        data=text.encode('utf-8');path.write_bytes(data)
        if p in OLD:
            pins['modified'][p]=dict(before_sha256=sha(BODIES[p]),result_sha256=sha(data),before_bytes=len(BODIES[p]),result_bytes=len(data))
            patches.extend(difflib.unified_diff(OLD[p].splitlines(True),text.splitlines(True),fromfile='a/'+p,tofile='b/'+p,n=3))
        else:
            pins['added'][p]=dict(result_sha256=sha(data),result_bytes=len(data))
            patches.extend(difflib.unified_diff([],text.splitlines(True),fromfile='/dev/null',tofile='b/'+p,n=3))
    patch=''.join(patches).encode('utf-8');(HERE/'drop-source-refresh.patch').write_bytes(patch)
    pins['patch_sha256']=sha(patch);pin_file.write_text(json.dumps(pins,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    assert all(sha((ROOT/p).read_bytes())==sha(body) for p,body in BODIES.items())
    for p in ('user/SCUI.H','user/SCSHELL.H','user/SC32DROP.H','kernel/sc32_drop.c'):
        assert p not in NEW,p
    ast.parse(Path(__file__).read_text(encoding='utf-8'))
    print(json.dumps(dict(modified=len(pins['modified']),added=len(pins['added']),patch_sha256=pins['patch_sha256'],status=pins['status'])))


if __name__=='__main__':main()
