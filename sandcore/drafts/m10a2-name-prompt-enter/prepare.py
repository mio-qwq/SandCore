"""只准备固定前像的两个局部hunk，不应用、不编译、不运行客体。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
NAME='user/scshell/shell_name_prompt.inc'


def digest(body):
    return hashlib.sha256(body).hexdigest()


def once(text,before,after):
    if text.count(before)!=1:
        raise ValueError('anchor not unique: '+before[:80])
    return text.replace(before,after,1)


body=(ROOT/NAME).read_bytes()
pin_file=HERE/'PINS.json'
if pin_file.exists():
    assert digest(body)==json.loads(pin_file.read_text(encoding='utf-8'))['before_sha256']
before=body.decode('utf-8')
newline='\r\n' if '\r\n' in before else '\n'
text=before.replace('\r\n','\n')
text=once(text,
    '    if(event->type==SC_UI_EVENT_DESTROY&&event->node==m->prompt) {',
    '''    if(event->phase==SC_UI_CAPTURE_PHASE) {
        /* context的Enter快捷键先于default_button；只在本名称框拦截，不能改宿主其它控件的按键语义。 */
        if(event->root==m->prompt&&event->node==m->prompt_name&&event->type==SC_UI_EVENT_KEY_DOWN&&
            !(event->modifiers&(SC32_MOD_SHIFT|SC32_MOD_CONTROL|SC32_MOD_ALT))&&
            (event->key=='\\n'||event->key==27)) {
            ScU32 intent=event->key==27?SC_UI_DIALOG_CANCEL:SC_UI_DIALOG_OK;
            if(!m->prompt_intent||intent==SC_UI_DIALOG_CANCEL)m->prompt_intent=intent;
            handled=1;
        }
    } else if(event->type==SC_UI_EVENT_DESTROY&&event->node==m->prompt) {''')
text=once(text,
    '    listener.phases=SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;',
    '    listener.phases=SC_UI_CAPTURE_PHASE|SC_UI_TARGET_PHASE|SC_UI_BUBBLE_PHASE;')
result=text.replace('\n',newline).encode('utf-8')
(HERE/'result').mkdir(exist_ok=True)
(HERE/'result/shell_name_prompt.inc').write_bytes(result)
patch=''.join(difflib.unified_diff(before.replace('\r\n','\n').splitlines(True),text.splitlines(True),
    fromfile='a/'+NAME,tofile='b/'+NAME,n=3)).encode('utf-8')
(HERE/'name-enter.patch').write_bytes(patch)
pin=dict(path=NAME,status='SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED',
    before_sha256=digest(body),result_sha256=digest(result),patch_sha256=digest(patch),
    before_bytes=len(body),result_bytes=len(result),local_hunks=2)
pin_file.write_text(json.dumps(pin,indent=2)+'\n',encoding='utf-8')
sources=('user/scui/ui_dispatch.c','user/scui/ui_menu.c','user/scui/ui_controls.c',
         'user/scui/ui_edit.c','user/files2/files2_view.inc','user/scshell/shell_menu.c',
         'user/scshell/SCSHELL.json','user/SCUI.H')
source_pins={p:digest((ROOT/p).read_bytes()) for p in sources}
(HERE/'SOURCE-PINS.json').write_text(json.dumps(source_pins,indent=2)+'\n',encoding='utf-8')
ast.parse(Path(__file__).read_text(encoding='utf-8'))
assert digest((ROOT/NAME).read_bytes())==pin['before_sha256']
assert patch.count(b'@@ ')==2
files={p.relative_to(HERE).as_posix():digest(p.read_bytes()) for p in sorted(HERE.rglob('*'))
       if p.is_file() and p.name!='DELIVERY.json'}
(HERE/'DELIVERY.json').write_text(json.dumps(dict(status=pin['status'],
    static_checks=['python_ast','source_before_unchanged','two_local_hunks'],files=files),indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(before_sha256=pin['before_sha256'],result_sha256=pin['result_sha256'],
    patch_sha256=pin['patch_sha256'],delivery_sha256=digest((HERE/'DELIVERY.json').read_bytes()),status=pin['status'])))
