"""保存GUI04真实终账、声音和已查看画面，失败业务保留为未通过。"""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[2]
VM=ROOT/'build/m10a2-p6-gui-run-04'
OUT=ROOT/'evidence/m10a2-p6-gui04-final'
OUT.mkdir(exist_ok=False)
raw=(VM/'p6-gui.json').read_bytes();report=json.loads(raw)
assert report['status']=='P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING'
assert report['current_inputs_unchanged'] and report['source_disk_unchanged']
assert report['changed_input_output_paths']==[]
ledger=report['p6_gui_runtime']['actions']['gui4-final-consistent-owner']['result']['ledger']
assert ledger['physical_delta']==ledger['unexplained_delta']==0 and ledger['unexpected']==[]
names=['p6-gui.json','console.txt','gui4-sound-replay-pcm.json','p6-gui4-sound-replay',
    'p6-gui4-sound-wave-first-play.png','p6-gui4-sound-first-eof.png',
    'p6-gui4-sound-replay-result.png','p6-gui4-sound-seek-result.png',
    'p6-gui4-desktop-after-sound.png','p6-gui4-debug-context-error-memory.png',
    'p6-gui4-debug-settings-app-memory.png','p6-gui4-debug-tabselect-breakpoint-result.png',
    'p6-gui4-files-flow-confirm-settled.png','p6-gui4-after-flow-cancel-settled.png']
files={}
sha=lambda b:hashlib.sha256(b).hexdigest()
screens={Path(s['path']).name:s for s in report['screenshots']}
for name in names:
    body=(VM/name).read_bytes()
    if name.endswith('.png'):assert sha(body)==screens[name]['sha256']
    shutil.copyfile(VM/name,OUT/name)
    files[name]=dict(bytes=len(body),sha256=sha(body))
result=dict(status='GUI04_RESOURCE_ACCOUNT_PASS_WITH_BUSINESS_DEFECTS_RETAINED',
    final_report_sha256=sha(raw),ledger=ledger,inputs_unchanged=True,source_disk_unchanged=True,
    verified_session_disk_sha256=report['verified_session_disk_sha256'],
    observations=dict(sound_replay_after_eof=True,sound_seek_to_seconds=4,
        sound_pcm= json.loads((VM/'gui4-sound-replay-pcm.json').read_bytes()),
        classic_theme_saved_and_visible=True,four_drop_buttons_visible=True,
        settings_programs_and_environment_provider='FAILED_CHILD_AT_AND_INDEX_OF_CONTRACT',
        installer_business='NOT_EXERCISED_PENDING_SAME_PROVIDER_FIX',
        files_source_after_cancel='WINDOW_GONE_PS_CONFIRMED_PROCESS_GONE_CAUSE_UNRESOLVED'),
    full_M10a2=False,whole_gui_workflow_pass=False,files=files)
(OUT/'SNAPSHOT.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Saved',len(files),'files',sum(r['bytes'] for r in files.values()),'bytes')
