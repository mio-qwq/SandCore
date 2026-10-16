"""只保存已查看的代表画面和原始业务记录，不复制数据盘或改写FAIL。"""
from pathlib import Path
import hashlib
import json
import shutil
ROOT=Path(__file__).resolve().parents[2]
source=ROOT/'build/m10a2-p6-gui-run-03'
out=ROOT/'evidence/m10a2-p6-gui03'
out.mkdir(exist_ok=False)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
report=json.loads((source/'p6-gui.json').read_bytes())
assert report['status']=='FAIL_OR_INTERRUPTED' and report['current_inputs_unchanged'] and report['source_disk_unchanged']
names=['p6-gui.json','gui3-drop-audit.json','gui3-desktop-copy.rec','gui3-desktop-move.rec',
    'gui3-note-saved.txt','gui3-after-cancel-note.txt',
    'p6-gui3-note-default-opened.png','p6-gui3-move-auto-refresh.png',
    'p6-gui3-drop-cancelled-source-retained.png','p6-gui3-calculator-to-notes-text.png',
    'p6-gui3-studio-rdstats-link-diagnostic.png','p6-gui3-settings-preview.png',
    'p6-gui3-settings-timeout-revert.png','p6-gui3-monitor-self-properties.png',
    'p6-gui3-notes-default.png']
shots={Path(row['path']).name:row for row in report['screenshots']}
files={}
for name in names:
    original=source/name
    if name.endswith('.png'):assert sha(original)==shots[name]['sha256']
    shutil.copyfile(original,out/name)
    assert sha(out/name)==sha(original)
    files[name]=dict(bytes=original.stat().st_size,sha256=sha(original))
receipt=dict(status='PARTIAL_GUI_WORKFLOW_EVIDENCE_WITH_TRANSPORT_FAILURE',
    report_status=report['status'],original_report_sha256=sha(source/'p6-gui.json'),
    root_viewed_representative_pngs=True,
    actual_passed=['Notes save and association','Deferred file copy and move with durable journals',
        'Automatic source desktop refresh','Drop cancellation preserves source',
        'Calculator text paste after source exit','Monitor process properties window'],
    partial=['Studio SCO compilation and SCL link; publication failed -401',
        'Settings startup and 100 percent preview; timeout state text stale'],
    failed=['Default Lens PNG launch -112','Management heartbeat timeout'],
    final_resource_account='NOT_OBTAINED',full_M10a2=False,
    stderr_observed=dict(exit_code=1,cause='TimeoutError: 管理请求 10 无有效回复',
        call='verify_m9.Guest.connect.heartbeat -> ManagementClient.checked(10) -> request',
        final_error='RuntimeError: 串口会话错误',
        evidence='Actual exec session 5218 termination output; no failure fields reset'),files=files)
(out/'SUMMARY.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Saved',len(files),'files',sum(row['bytes'] for row in files.values()),'bytes; original FAIL retained')
