"""只准备共享systemtools通知引用与Sound键符的精确草稿，不改主树或启动VM。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
EXPECTED={
    'user/systemtools/TOOLS.H':'764161356e162eda97a6efb03771d2864cbaf01f33f0fa9b970242e6f3b57066',
    'user/systemtools/tools_ui.inc':'8ac3d66668684d0f4eff64c6c6055412517d668db2f8b1675852c442676e82ff',
    'user/sound2/sound_ui.c':'a4802d52f1bf9593c520efa0a99d7befd7d23903b84bff65268b4fc34ee822b5',
}

OLD_ERROR='''    ScUiDialog *dialog=0;
    if(tool.ui && tool.root && ScShellErrorDialog(tool.ui,tool_dialog_owner(),&error,operation,&dialog)==0)ScUiDialogRelease(dialog);'''
NEW_ERROR='''    /* ScShellErrorDialog没有完成回调入口；不能创建后立即丢掉唯一引用。
     * 保留同样的底层错误字段，交本应用有生命周期的只读通知显示。 */
    char message[512];
    snprintf(message,sizeof(message),"操作未完成。\\n%s\\n错误 %d · 原因 %d · 域 %u · 底层值 %d",
        operation?operation:"",error.code,error.cause,error.domain,error.raw);
    if(tool.ui && tool.root)tool_message("操作未完成",message);'''

OLD_MESSAGE='''static ScStatus tool_message(const char *title,const char *message)
{
    ScUiMessageBoxDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.owner=tool_dialog_owner();
    d.title=tool_text(title);
    d.message=tool_text(message);
    ScUiDialog *dialog=0;
    ScStatus s=ScUiMessageBox(tool.ui,&d,&dialog);
    ScUiDialogRelease(dialog);
    return s;
}'''
NEW_MESSAGE='''static ScStatus tool_notice_complete(ScUiContext *ui,const ScUiEvent *event,void *context)
{
    (void)ui;
    (void)context;
    if(event->type==SC_UI_EVENT_DIALOG_COMPLETE && event->data==tool.notice){
        ScUiDialog *done=tool.notice;
        tool.notice=0;
        /* 先脱开本地引用；Release可能进入析构，不能留下可再次领取的指针。 */
        ScUiDialogRelease(done);
    }
    return 0;
}
static ScStatus tool_message(const char *title,const char *message)
{
    if(tool.notice)return SC_UI_E_BUSY;
    ScUiMessageBoxDesc d;
    memset(&d,0,sizeof(d));
    d.size=sizeof(d);
    d.version=1;
    d.owner=tool_dialog_owner();
    d.title=tool_text(title);
    d.message=tool_text(message);
    d.completion=tool_notice_complete;
    ScUiDialog *dialog=0;
    ScStatus s=ScUiMessageBox(tool.ui,&d,&dialog);
    /* 成功返回OPEN时，caller仍拥有唯一引用；保存至完成或tool_close。
     * 已有未关通知绝不被新的通知覆盖，也不改SCUI的最后Release取消合同。 */
    if(s==0)tool.notice=dialog;
    else ScUiDialogRelease(dialog);
    return s;
}'''

OLD_CLOSE='''static void tool_close(void)
{
    if(tool.ui && tool.root)ScUiNodeDestroy(tool.ui,tool.root);'''
NEW_CLOSE='''static void tool_close(void)
{
    ScUiDialog *notice=tool.notice;
    tool.notice=0;
    /* 最后Release会取消仍OPEN的通知；回调看到已脱链，不会二次释放。 */
    ScUiDialogRelease(notice);
    if(tool.ui && tool.root)ScUiNodeDestroy(tool.ui,tool.root);'''


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def once(text,old,new):
    if text.count(old)!=1:
        raise ValueError('固定原文不是唯一匹配：'+old[:65])
    return text.replace(old,new,1)


def main():
    gui=json.loads((ROOT/'build/m10a2-p6-gui-run-02/p6-gui.json').read_text(encoding='utf-8'))
    transforms={}
    for name,expected in EXPECTED.items():
        if sha(ROOT/name)!=expected:
            raise ValueError('固定原像已改变：'+name)
    header=(ROOT/'user/systemtools/TOOLS.H').read_text(encoding='utf-8')
    transforms['user/systemtools/TOOLS.H']=once(header,'    ScUiMenu *menus;\n','    ScUiMenu *menus;\n    ScUiDialog *notice;\n')
    helper=(ROOT/'user/systemtools/tools_ui.inc').read_text(encoding='utf-8')
    transforms['user/systemtools/tools_ui.inc']=once(once(once(helper,OLD_ERROR,NEW_ERROR),OLD_MESSAGE,NEW_MESSAGE),OLD_CLOSE,NEW_CLOSE)
    sound=(ROOT/'user/sound2/sound_ui.c').read_text(encoding='utf-8')
    for old,new in (("S2_OPEN,'O',SC32_MOD_CONTROL","S2_OPEN,'o',SC32_MOD_CONTROL"),
                    ("S2_REPLAY,'R',SC32_MOD_CONTROL","S2_REPLAY,'r',SC32_MOD_CONTROL"),
                    ("S2_MINI,'M',SC32_MOD_CONTROL","S2_MINI,'m',SC32_MOD_CONTROL")):
        sound=once(sound,old,new)
    transforms['user/sound2/sound_ui.c']=sound
    templates=('user/systemtools/CALC2.json','user/systemtools/PALETTE2.json',
               'user/systemtools/MONITOR2.json','user/sound2/SOUND2.JSON')
    for relative in templates:
        body=json.loads((ROOT/relative).read_text(encoding='utf-8'))
        removed=[row for row in body['imports'] if row['name']=='ScShellErrorDialog']
        if len(removed)!=1:
            raise ValueError('HOST模板缺唯一旧导入：'+relative)
        body['imports']=[row for row in body['imports'] if row['name']!='ScShellErrorDialog']
        transforms[relative]=json.dumps(body,ensure_ascii=False,indent=2)+'\n'
    pins,gui_pins,patches={},{},dict(lifetime=[],sound_shortcuts=[])
    for relative,after in transforms.items():
        path=ROOT/relative
        raw=path.read_bytes();before=raw.decode('utf-8').replace('\r\n','\n')
        newline='\r\n' if b'\r\n' in raw else '\n'
        expected=sha(path)
        recorded={k:v for k,v in gui['input_sha256'].items() if k.replace('\\','/').endswith('/'+relative)}
        if not recorded or any(v!=expected for v in recorded.values()):
            raise ValueError('来源与真实GUI02 pin不同：'+relative)
        gui_pins.update(recorded)
        target=HERE/'overlay'/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(after.replace('\n',newline).encode('utf-8'))
        pins[relative]=dict(before_sha256=expected,result_sha256=sha(target),before_bytes=len(raw),result_bytes=target.stat().st_size)
        key='sound_shortcuts' if relative=='user/sound2/sound_ui.c' else 'lifetime'
        patches[key].extend(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/'+relative,tofile='b/'+relative))
    for name,lines in patches.items():
        (HERE/(name+'.patch')).write_text(''.join(lines),encoding='utf-8',newline='\n')
    native_path=ROOT/'build/m10a2-p6-target-refresh-03/p6-generation.json'
    native=json.loads(native_path.read_text(encoding='utf-8'))
    closures={}
    for label in ('CALC2','PALETTE2','MONITOR2','SOUND2'):
        closure=native['preflight']['source_closures'][label]
        for path in ('user/systemtools/TOOLS.H','user/systemtools/tools_ui.inc'):
            if closure.get(path)!=EXPECTED[path]:raise ValueError('实际native引用闭包不匹配：'+label+' '+path)
        closures[label]={path:closure[path] for path in ('user/systemtools/TOOLS.H','user/systemtools/tools_ui.inc')}
    facts=dict(status='SOURCE_PATCH_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED',modified=pins,
        patches={name:sha(HERE/(name+'.patch')) for name in patches},
        refresh_labels=['CALC2','PALETTE2','MONITOR2','SOUND2'],actual_native_consumer_closures=closures,
        native_report=str(native_path),native_report_sha256=sha(native_path),
        gui_report=str(ROOT/'build/m10a2-p6-gui-run-02/p6-gui.json'),gui_input_sha256=gui_pins,
        error_dialog_same_last_reference_defect=True,error_operation_and_code_cause_domain_raw_preserved=True,
        duplicate_notice_policy='BUSY_KEEP_EXISTING_OPEN_NOTICE',completion_then_clear_reference=True,
        tool_close_clears_before_last_release=True,no_new_public_API_or_import=True,
        HOST_templates_removed_import='ScShellErrorDialog',NATIVE_manifests_not_modified=True,
        Sound_shortcuts=dict(before=['O','R','M'],result=['o','r','m'],displayed_shortcut_labels_unchanged=True),
        dependencies={name:sha(ROOT/name) for name in ('user/scui/ui_base.c','user/scui/ui_dialog.c','user/scui/ui_menu.c',
                    'user/scui/ui_dispatch.c','user/scshell/shell_dialog.c','kernel/keyboard.c','kernel/wm.c')},
        original_sources_unchanged=all(sha(ROOT/name)==row['before_sha256'] for name,row in pins.items()),
        palette_resize_patch=False,monitor_terminate_confirm_not_modified=True)
    (HERE/'PINS.json').write_text(json.dumps(facts,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    ast.parse((HERE/'prepare.py').read_text(encoding='utf-8'))
    delivery={p.relative_to(HERE).as_posix():dict(sha256=sha(p),bytes=p.stat().st_size)
              for p in sorted(HERE.rglob('*')) if p.is_file() and p.name!='DELIVERY.json'}
    (HERE/'DELIVERY.json').write_text(json.dumps(dict(status=facts['status'],files=delivery),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:facts[k] for k in ('status','modified','patches','refresh_labels','original_sources_unchanged')},ensure_ascii=False,indent=2))
    print('DELIVERY_SHA256',sha(HERE/'DELIVERY.json'))


if __name__=='__main__':
    main()
