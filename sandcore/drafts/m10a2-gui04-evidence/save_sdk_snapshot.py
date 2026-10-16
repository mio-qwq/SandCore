"""保存主代理已亲自查看的SDK/调试/PNG/显示回退阶段证据。"""
from pathlib import Path
import hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[2]
VM=ROOT/'build/m10a2-p6-gui-run-04'
OUT=ROOT/'evidence/m10a2-p6-gui04-sdk'
OUT.mkdir(exist_ok=False)
raw=(VM/'p6-gui.json').read_bytes();report=json.loads(raw)
assert report['status']=='RUNNING'
sha=lambda b:hashlib.sha256(b).hexdigest()
names=['p6-gui4-studio-rdstats-build-result.png','p6-gui4-studio-rdesk-build-settled.png',
    'p6-gui4-rdesk-maximized-settled.png','p6-gui4-debug-rdesk-ready.png',
    'p6-gui4-debug-first-step.png','p6-gui4-debug-runtime-module-table.png',
    'p6-gui4-debug-breakpoint-hit-settled.png','p6-gui4-debug-breakpoint-removed.png',
    'p6-gui4-debug-target-normal-exit.png','p6-gui4-studio-after-debug-closed.png',
    'p6-gui4-lens-default-png-result.png','p6-gui4-settings-preview-active.png',
    'p6-gui4-settings-preview-timeout-result.png','gui4-studio-publication-audit.json',
    'gui4-rdstats-publication-actual.rec','gui4-rdesk-publication.rec',
    'gui4-rdstats.SCL','gui4-rdstats.SCL.map','gui4-rdstats.SCL.sdbg',
    'gui4-rdesk.SCX','gui4-rdesk.SCX.map','gui4-rdesk.SCX.sdbg']
screens={Path(s['path']).name:s for s in report['screenshots']}
files={}
for name in names:
    body=(VM/name).read_bytes()
    if name.endswith('.png'):assert sha(body)==screens[name]['sha256']
    shutil.copyfile(VM/name,OUT/name)
    files[name]=dict(bytes=len(body),sha256=sha(body))
actual_gets={name:row for name,row in report['p6_gui_runtime']['actions'].items()
    if row['op']=='get' and row.get('result',{}).get('path') and Path(row['result']['path']).name in names}
summary=dict(status='SDK_GUI_NATIVE_BUILD_RUN_DEBUG_OBSERVED_FINAL_RESOURCE_PENDING',
    original_report_at_observation_sha256=sha(raw),report_still_running=True,
    native_report=report['native_report'],native_report_sha256=report['native_report_sha256'],
    host_report=report['host_report'],host_report_sha256=report['host_report_sha256'],
    core_binding=report['core_binding'],actual_get_actions=actual_gets,
    root_observations=dict(studio_scl_build_and_publication=True,four_independent_tu_link=True,
        app_native_gui_and_runtime_plugin=True,rows=4,sum=40,mean=10,minimum=-4,maximum=32,
        debugger_entry_eip='00400000',single_step_eip='00400003',
        runtime_module_count=7,verified_symbol_count=1509,rdstats_runtime_base='50101000',
        breakpoint_address='500110EE',breakpoint_hits=1,breakpoint_removed=True,
        owned_target_exit_code=0,debugger_and_studio_normally_closed=True,
        original_png_decoded_and_displayed=[1672,941],
        display_preview_scale=100,display_reverted_scale=150,display_preview_seconds_after=0),
    final_resource_account='NOT_YET_REQUESTED',full_M10a2=False,files=files)
(OUT/'SNAPSHOT.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Saved',len(files),'files',sum(row['bytes'] for row in files.values()),'bytes; final owner still pending')
