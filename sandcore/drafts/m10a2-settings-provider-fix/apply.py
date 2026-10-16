"""GUI04已结束后接入三个平表合同与应用持有对话框；保留原换行。"""
from pathlib import Path
import hashlib,json,subprocess

ROOT=Path(__file__).resolve().parents[2]
HERE=Path(__file__).resolve().parent
report=json.loads((ROOT/'build/m10a2-p6-gui-run-04/p6-gui.json').read_bytes())
assert report['status']=='P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING'
assert report['current_inputs_unchanged'] and report['source_disk_unchanged']
assert subprocess.run(['powershell','-NoProfile','-Command',
    'if(Get-Process -Id 16116 -ErrorAction SilentlyContinue){exit 1}'],capture_output=True).returncode==0
changes={}
def replace(name,before,after):
    path=ROOT/name
    old=path.read_bytes() if name not in changes else changes[name][1]
    eol='\r\n' if b'\r\n' in old else '\n'
    a=before.replace('\n',eol).encode();b=after.replace('\n',eol).encode()
    assert old.count(a)==1,(name,before[:70])
    changes[name]=(changes.get(name,(old,old))[0],old.replace(a,b,1))

replace('user/settings2/APP.H','    ScUiMenu *menus;AppCommand command;AppEvent event;',
    '    ScUiMenu *menus;ScUiDialog *notice;AppCommand command;AppEvent event;')
replace('user/settings2/APP.H','static ScStatus message(const char *title,const char *body);',
'''static ScStatus message(const char *title,const char *body);
static ScStatus notice_complete(ScUiContext *,const ScUiEvent *,void *);
static ScStatus flat_provider(const ScUiDataRequest *,ScUiDataReply *,ScU32 count,ScU32 revision,ScU32 *index);''')
helper='''/* 平表只有根的直属行。CHILD_AT的index是序号，ITEM/CELL的item是
 * 身份；混用会把item=0下溢并让正常重绘变成BUSY。此私有解析器
 * 统一三张表的边界，INDEX_OF按公开合同通过total返回序号。 */
static ScStatus flat_provider(const ScUiDataRequest *q,ScUiDataReply *r,ScU32 count,ScU32 revision,ScU32 *index)
{
    r->revision=revision;
    r->parent=0;
    r->total=count;
    if(q->operation==SC_UI_DATA_COUNT)return SC_UI_HANDLED;
    if(q->operation==SC_UI_DATA_CHILD_COUNT){
        if(q->parent>count)return SC_UI_E_NOT_FOUND;
        if(q->parent)r->total=0;
        return SC_UI_HANDLED;
    }
    if(q->operation==SC_UI_DATA_CHILD_AT){
        if(q->parent>count)return SC_UI_E_NOT_FOUND;
        if(q->parent || q->index>=count)return SC_UI_END;
        *index=q->index;
    }else if(q->operation==SC_UI_DATA_ITEM || q->operation==SC_UI_DATA_CELL || q->operation==SC_UI_DATA_INDEX_OF){
        if(!q->item || q->item>count)return SC_UI_E_NOT_FOUND;
        *index=q->item-1;
    }else return SC_UI_E_NOT_SUPPORTED;
    r->item=*index+1;
    r->state=SC_UI_VISIBLE|SC_UI_ENABLED;
    if(q->operation==SC_UI_DATA_INDEX_OF)r->total=*index;
    return q->operation==SC_UI_DATA_CELL?0:SC_UI_HANDLED;
}

'''
for name in ('SETTINGS2','INSTALLER2'):
    replace('user/settings2/'+name+'.C','#include "app_io.inc"',
        '#include "app_io.inc"\n#include "app_provider.inc"')
assert not (ROOT/'user/settings2/app_provider.inc').exists()
changes['user/settings2/app_provider.inc']=(b'',helper.replace('\n','\r\n').encode())
for file,prefix,count,revision in (
    ('settings_programs.inc','settings','row_count','program_model_revision'),
    ('settings_model.inc','settings','env_count','env_model_revision'),
    ('install_model.inc','installer','count','revision')):
    replace('user/settings2/'+file,f'''    r->revision={prefix}.{revision};
    r->total={prefix}.{count};
    if(q->operation==SC_UI_DATA_COUNT)return 0;
    ScU32 index=q->operation==SC_UI_DATA_ITEM?q->index:q->item-1;
    if(index>={prefix}.{count})return SC_UI_END;
    r->item=index+1;
    if(q->operation==SC_UI_DATA_INDEX_OF){{r->value.type=SC_VALUE_U32;
        r->value.lo=index;
        return 0;
    }}''',f'''    ScU32 index=0;
    ScStatus parsed=flat_provider(q,r,{prefix}.{count},{prefix}.{revision},&index);
    if(parsed)return parsed==SC_UI_HANDLED?0:parsed;''')

replace('user/settings2/app_ui.inc','''    ScShellErrorInfo e;
    INIT(e);
    e.code=e.cause=code;
    ScUiDialog *d=0;
    ScShellErrorDialog(app.ui,app.root,&e,operation,&d);
    ScUiDialogRelease(d);
    fprintf(stderr,"setup error=%d %s\\n",code,operation);''','''    char body[384];
    snprintf(body,sizeof(body),"%s\\n错误 %d",operation?operation:"操作未完成",code);
    status(body);
    if(app.ui && app.root)message("操作未完成",body);
    fprintf(stderr,"setup error=%d %s\\n",code,operation?operation:"");''')
replace('user/settings2/app_ui.inc','''static ScStatus message(const char *title,const char *body)
{
    ScUiMessageBoxDesc d;''','''static ScStatus notice_complete(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;
    (void)context;
    if(event->type==SC_UI_EVENT_DIALOG_COMPLETE && event->data==app.notice){
        ScUiDialog *done=app.notice;
        app.notice=0;
        /* 完成回调仍在库的活跃引用中；先脱开应用指针再领取唯一持有。 */
        ScUiDialogRelease(done);
    }
    return 0;
}
static ScStatus message(const char *title,const char *body)
{
    if(app.notice)return SC_UI_E_BUSY;
    ScUiMessageBoxDesc d;''')
replace('user/settings2/app_ui.inc','''    d.message=text(body);
    ScUiDialog *dialog=0;
    ScStatus s=ScUiMessageBox(app.ui,&d,&dialog);
    ScUiDialogRelease(dialog);''','''    d.message=text(body);
    d.completion=notice_complete;
    ScUiDialog *dialog=0;
    ScStatus s=ScUiMessageBox(app.ui,&d,&dialog);
    /* OPEN最后Release会取消窗口；持有到完成或app_close，不能创建即丢弃。 */
    if(s==0)app.notice=dialog;
    else ScUiDialogRelease(dialog);''')
replace('user/settings2/app_ui.inc','''static void app_close(void)
{
    if(app.ui && app.root)ScUiNodeDestroy(app.ui,app.root);''','''static void app_close(void)
{
    ScUiDialog *notice=app.notice;
    app.notice=0;
    if(notice)ScUiDialogClose(notice,SC_UI_DIALOG_CANCEL);
    ScUiDialogRelease(notice);
    if(app.ui && app.root)ScUiNodeDestroy(app.ui,app.root);''')
replace('user/settings2/app_ui.inc','''static ScStatus property(const char *title,ScConfig *initial,const ScUiFieldDesc *fields,ScU32 count,AppEvent apply,void *context)
{
    ScUiPropertySheetDesc d;''','''static ScStatus property(const char *title,ScConfig *initial,const ScUiFieldDesc *fields,ScU32 count,AppEvent apply,void *context)
{
    if(app.notice)return SC_UI_E_BUSY;
    ScUiPropertySheetDesc d;''')
replace('user/settings2/app_ui.inc','''    d.apply=apply;
    d.context=context;
    ScUiDialog *dialog=0;
    ScStatus s=ScUiPropertySheet(app.ui,&d,fields,count,&dialog);
    ScUiDialogRelease(dialog);''','''    d.apply=apply;
    d.context=context;
    d.completion=notice_complete;
    ScUiDialog *dialog=0;
    ScStatus s=ScUiPropertySheet(app.ui,&d,fields,count,&dialog);
    if(s==0)app.notice=dialog;
    else ScUiDialogRelease(dialog);''')
for name in ('SETTINGS2','INSTALLER2'):
    path='user/settings2/'+name+'.json'
    old=(ROOT/path).read_text(encoding='utf-8')
    data=json.loads(old)
    entry=next(x for x in data['imports'] if x['name']=='ScShellErrorDialog')
    # 只删除不再调用的旧导入，保留其余排序和原始缩进/换行。
    text=json.dumps(entry,ensure_ascii=False,indent=2)
    block='    '+text.replace('\n','\n    ')+',\n'
    replace(path,block,'')
result={}
for name,(old,new) in changes.items():
    path=ROOT/name
    assert path.read_bytes()==old if path.exists() else old==b''
    path.write_bytes(new)
    result[name]=dict(before_sha256=hashlib.sha256(old).hexdigest(),after_sha256=hashlib.sha256(new).hexdigest())
(HERE/'integration.json').write_text(json.dumps(dict(status='APPLIED_NOT_COMPILED_OR_GUEST_VERIFIED',
    gui04_report_sha256=hashlib.sha256((ROOT/'build/m10a2-p6-gui-run-04/p6-gui.json').read_bytes()).hexdigest(),
    changed=result),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Applied',len(result),'files; native SETTINGS2 and INSTALLER2 refresh required')
