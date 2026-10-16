"""只静态核验本准备稿及当前源引用，绝不连接/启动客体。"""
from pathlib import Path
import ast
import hashlib
import json
import re

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
REFERENCES=[
 'tools/verify_m10a2_ui_gui.py','tools/verify_m10a2_shell_runtime.py','tools/m10a2_shell_desktop_flow.py',
 'tools/scui_input.py','tools/scui_workflow.py','tools/m10a2_shell_inspect.py','tools/verify_m10a2_ui_platform.py',
 'tools/audit_m10a2_file_job_results.py','tools/keys.py','tools/mouse.py',
 'user/files2/FILES2.H','user/files2/files2_view.inc','user/files2/files2_actions.inc','user/files2/files2_storage.inc',
 'user/scshell/desktop/desktop_view.inc','user/scshell/desktop/desktop_actions.inc',
 'user/notes2/NOTES2.H','user/notes2/notes_ui.c','user/notes2/notes_io.c',
 'user/studio2/STUDIO2.H','user/studio2/studio_ui.c','user/studio2/studio_actions.c','user/studio2/studio_build.c',
 'user/debug2/D2APP.H','user/debug2/ui.c','user/debug2/model.c','user/debug2/sidecar.c','user/debug2/DEBUG2.C',
 'user/imageapps/IMAGEAPP.H','user/imageapps/ui.c','user/imageapps/actions.c',
 'user/settings2/SETTINGS2.H','user/settings2/settings_view.inc','user/settings2/app_ui.inc',
 'user/systemtools/tools_ui.inc','user/systemtools/calc_view.inc','user/systemtools/palette_view.inc','user/systemtools/monitor_view.inc',
 'user/terminal2/TERMINAL2.H','user/terminal2/TERMINAL2.C','user/terminal2/terminal_ui.c','user/scterm/term_widget.c',
 'user/sound2/SOUND2.H','user/sound2/sound_ui.c',
 'user/session2/S2APP.H','user/session2/ui.c','user/session2/actions.c',
 'user/scui/ui_menu.c','user/scui/ui_dispatch.c','user/scui/ui_dialog.c',
 'examples/m10a2-sdk/researchdesk/RDESK.SCP','examples/m10a2-sdk/researchdesk/RDSTATS.SCP',
 'examples/m10a2-sdk/researchdesk/MAIN.C','examples/m10a2-sdk/researchdesk/MODEL.C','examples/m10a2-sdk/researchdesk/BUILD.SH',
]
EXPECTED={
 'user/files2/FILES2.H':{'F2_ADDRESS':0x8115,'F2_DROP_COPY':0x8116,'F2_DROP_MOVE':0x8117,'F2_EDIT':0x810a,'F2_DEBUG':0x810b},
 'user/notes2/NOTES2.H':{'N_SAVE':0x8102,'N_SAVE_AS':0x8103,'N_COPY':0x8109,'N_PASTE':0x810a},
 'user/studio2/STUDIO2.H':{'S_PROJECT_OPEN':0x8607,'S_BUILD':0x8618,'S_CANCEL':0x8619,'S_RUN':0x861a,'S_DEBUG':0x861b},
 'user/debug2/D2APP.H':{'D2_LAUNCH':100,'D2_CONTINUE':200,'D2_PAUSE':201,'D2_STEP':202,'D2_BREAK_ADD':302,'D2_BREAK_REMOVE':303},
 'user/imageapps/IMAGEAPP.H':{'I_NEW':100,'I_SAVE_AS':103,'I_UNDO':106,'I_REDO':107,'I_TOOL_FIRST':200},
 'user/settings2/SETTINGS2.H':{'S2_PREVIEW':0x5200,'S2_KEEP':0x5201,'S2_REVERT':0x5202},
 'user/sound2/SOUND2.H':{'S2_OPEN':0x6900,'S2_FORWARD':0x6905,'S2_SEEK':0x6906,'S2_CANCEL':0x690a},
 'user/session2/S2APP.H':{'S_SIGNIN':100,'S_REQUEST_SIGNIN':101,'S_CANCEL':102,'S_LOCK':108},
}
def sha(data):return hashlib.sha256(data).hexdigest()
def enums(text):
    text=re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)
    values={}
    for block in re.findall(r'\benum\s*\{([^}]*)\}',text):
        value=-1
        for item in block.split(','):
            parts=item.strip().split('=',1);name=parts[0].strip()
            if not re.fullmatch(r'[A-Za-z_]\w*',name):continue
            if len(parts)==2:
                try:value=int(parts[1].strip().rstrip('uUlL'),0)
                except ValueError:value=None
            elif value is not None:value+=1
            if value is not None:values[name]=value
    return values
def main():
    references={name:sha((ROOT/name).read_bytes()) for name in REFERENCES}
    constants={}
    for path,expected in EXPECTED.items():
        actual=enums((ROOT/path).read_text(encoding='utf-8'))
        for name,value in expected.items():
            if actual.get(name)!=value:raise ValueError('文档命令号与源码不符：'+name)
        constants[path]={name:dict(decimal=value,hex='0x%x'%value) for name,value in expected.items()}
    ast.parse((HERE/'flow_input.py').read_text(encoding='utf-8'))
    source_map=dict(status='STATIC_PREPARATION_ONLY_NO_VM',input_reference_sha256=references,commands=constants,
        helpers_executed=False,guest=False,build=False,packaging=False,
        observed_gaps=['SESSION_LOGIN_PANEL_IS_NOT_ASSERTED_SAME_SID_UNLOCK',
                       'SCUIDEMO_F12_SNAPSHOT_IS_NOT_FORMAL_APPLICATION_API'])
    source_map['shortcut_wiring']='ROOT_FIXED_FOUR_HELPERS_PUBLIC_COMMANDSET_GUEST_KEYS_UNVERIFIED'
    (HERE/'SOURCE-MAP.json').write_text(json.dumps(source_map,indent=2)+'\n')
    files=[]
    for path in sorted(HERE.iterdir()):
        if path.is_file() and path.name!='DELIVERY.json':
            data=path.read_bytes();files.append(dict(path=path.name,bytes=len(data),sha256=sha(data)))
    result=dict(status='FROZEN_PREPARATION_ONLY_NOT_EXECUTED',files=files,guest=False,build=False,
                main_modified=False,git_used=False,disk_copied=False)
    (HERE/'DELIVERY.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(files=len(files),references=len(references),command_constants=sum(map(len,EXPECTED.values())),
                         delivery_sha256=sha((HERE/'DELIVERY.json').read_bytes()))))
if __name__=='__main__':main()
