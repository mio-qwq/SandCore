"""仅在 GUI02 已正常结束且前像匹配时合入已经审阅的局部修复。"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sha = lambda body: hashlib.sha256(body).hexdigest()
report = json.loads((ROOT/'build/m10a2-p6-gui-run-02/p6-gui.json').read_text(encoding='utf-8'))
assert report['status'] == 'P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING'
assert report['current_inputs_unchanged'] and report['source_disk_unchanged']
assert not report['changed_input_output_paths']
actions = report['p6_gui_runtime']['actions']
assert actions['finish-gui2-coherent-owner-account']['result']['ledger']['unexplained_delta'] == 0
rows = []

def add(path, source, before, after):
    target, origin = ROOT/path, ROOT/'drafts'/source
    old, new = target.read_bytes(), origin.read_bytes()
    assert sha(old) == before, str(target)
    assert sha(new) == after, str(origin)
    rows.append((target, new, dict(path=path, source=str(origin.relative_to(ROOT)),
                                 before_sha256=before, after_sha256=after, bytes=len(new))))

name = json.loads((ROOT/'drafts/m10a2-name-prompt-enter/PINS.json').read_text(encoding='utf-8'))
add(name['path'], 'm10a2-name-prompt-enter/result/shell_name_prompt.inc', name['before_sha256'], name['result_sha256'])
notes = json.loads((ROOT/'drafts/m10a2-app-path-arguments/PINS.json').read_text(encoding='utf-8'))['applications']['NOTES2']
add(notes['entry'], 'm10a2-app-path-arguments/overlay/'+notes['entry'], notes['before_sha256'], notes['result_sha256'])
for directory in ('m10a2-studio-sco-source-path', 'm10a2-settings-startup-review'):
    pins = json.loads((ROOT/'drafts'/directory/'PINS.json').read_text(encoding='utf-8'))
    for path, row in pins['modified'].items():
        add(path, directory+'/overlay/'+path, row['before_sha256'], row['result_sha256'])
calc = json.loads((ROOT/'drafts/m10a2-calculator-labels/DELIVERY.json').read_text(encoding='utf-8'))
add(calc['path'], 'm10a2-calculator-labels/calc_view.inc', calc['before_sha256'], calc['result_sha256'])
for target, new, row in rows:
    target.write_bytes(new)
    assert sha(target.read_bytes()) == row['after_sha256']
result = dict(status='REVIEWED_SMALL_FIXES_INTEGRATED_NOT_BUILT',
              gui_report_sha256=sha((ROOT/'build/m10a2-p6-gui-run-02/p6-gui.json').read_bytes()),
              modified=[row for _, _, row in rows], public_ABI_changed=False,
              build_started=False, guest_validation_started=False)
(HERE/'small-integration.json').write_bytes((json.dumps(result, ensure_ascii=False, indent=2)+'\n').encode('utf-8'))
print(json.dumps(dict(status=result['status'], files=len(rows)), ensure_ascii=False))
