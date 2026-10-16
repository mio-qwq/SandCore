"""GUI03退出后按真实前像合入局部修复，不改写旧证据。"""
from pathlib import Path
import difflib
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sha = lambda body: hashlib.sha256(body).hexdigest()
report_path = ROOT/'build/m10a2-p6-gui-run-03/p6-gui.json'
report = json.loads(report_path.read_bytes())
assert report['status'] == 'FAIL_OR_INTERRUPTED'
assert report['current_inputs_unchanged'] and report['source_disk_unchanged']
assert not report['changed_input_output_paths']
check = subprocess.run(['powershell', '-NoProfile', '-Command',
    'if(Get-Process -Id 18952 -ErrorAction SilentlyContinue){exit 1}'], capture_output=True)
assert check.returncode == 0, 'GUI03仍在运行'
rows = {}

def change(path, old, new):
    assert path not in rows
    assert (ROOT/path).read_bytes() == old
    rows[path] = (old, new)

def replace(path, before, after):
    old = rows[path][1] if path in rows else (ROOT/path).read_bytes()
    eol = '\r\n' if b'\r\n' in old else '\n'
    source = before.replace('\n', eol).encode()
    target = after.replace('\n', eol).encode()
    assert old.count(source) == 1, (path, before[:80])
    new = old.replace(source, target, 1)
    if path in rows:
        rows[path] = (rows[path][0], new)
    else:
        change(path, old, new)

for directory in ('m10a2-image-service-loader-if', 'm10a2-dialog-button-flow'):
    base = ROOT/'drafts'/directory
    pins = json.loads((base/'PINS.json').read_bytes())
    for path, pin in pins['modified'].items():
        old = (ROOT/path).read_bytes()
        overlay = (base/'overlay'/path).read_bytes()
        assert sha(old) == pin['before_sha256'], path
        assert sha(overlay) == pin['result_sha256'], path
        # 保留未改行的原始CRLF，仅把审过的新行按目标换行接入。
        a, b = old.splitlines(keepends=True), overlay.splitlines(keepends=True)
        matcher = difflib.SequenceMatcher(None,
            [v.rstrip(b'\r\n') for v in a], [v.rstrip(b'\r\n') for v in b], autojunk=False)
        eol = b'\r\n' if b'\r\n' in old else b'\n'
        pieces = []
        for tag, i, j, k, l in matcher.get_opcodes():
            pieces.extend(a[i:j] if tag == 'equal' else
                [v.rstrip(b'\r\n')+eol for v in b[k:l]])
        new = b''.join(pieces)
        assert new.replace(b'\r\n', b'\n') == overlay.replace(b'\r\n', b'\n')
        change(path, old, new)

replace('user/studio2/STUDIO2.H',
    '    U phase,generation,epoch,next,input_count,linking,cancel,publishing;',
    '    U phase,generation,epoch,next,input_count,linking,cancel,publishing,publication_serial;')
replace('user/studio2/studio_build.c',
    '    ScShellFileJobDesc d;\n    rec(&d,sizeof(d));\n    d.operation=SC_SHELL_COPY;',
    '''    /* 每次发布保留独立REC；重复导出不能覆盖上次作业的持久记录。
     * build目录由本进程代数/构建代数独占，计数耗尽明确拒绝。 */
    if(b->publication_serial==0xffffffffu)return SC32_E_RANGE;
    char recovery[68],journal[24];
    snprintf(journal,sizeof(journal),"P%08X.REC",++b->publication_serial);
    ScStatus recovery_status=join(recovery,b->directory,journal);
    if(recovery_status<0)return recovery_status;
    ScShellFileJobDesc d;
    rec(&d,sizeof(d));
    d.operation=SC_SHELL_COPY;''')
replace('user/studio2/studio_build.c',
    '    d.conflict_policy=SC_SHELL_REPLACE;',
    '    d.recovery_path=recovery;')
replace('user/studio2/studio_build.c',
    '    if(q.state<SC_SHELL_JOB_FINISHED)return;',
    '''    if(q.state==SC_SHELL_JOB_CONFLICT){
        ScShellFileResult conflict;
        rec(&conflict,sizeof(conflict));
        s=ScShellFileOperationConflict(b->filejob,&conflict);
        if(s<0){ScShellFileOperationCancel(b->filejob);build_fail(s);return;}
        ScShellFileItem *item=conflict.item&&conflict.item<=3u?b->items+conflict.item-1u:0;
        /* Build只替换提交时已知的同一目标修订；后来创建/修改的用户文件
         * 不获得预设覆盖授权。决定绑定worker本次epoch与源/目标修订。 */
        if(!item || !(item->flags&SC_SHELL_ITEM_EXPECT_DESTINATION)
            || item->expected_destination_kind!=SC32_PATH_FILE
            || item->expected_source_revision!=conflict.source_revision
            || item->expected_destination_revision!=conflict.destination_revision
            || !same_name(item->source,conflict.source)
            || !same_name(item->destination,conflict.destination)){
            ScShellFileOperationCancel(b->filejob);build_fail(SC32_E_STALE);return;
        }
        ScShellConflictDecision decision;
        rec(&decision,sizeof(decision));
        decision.item=conflict.item;
        decision.conflict_epoch=conflict.conflict_epoch;
        decision.decision=SC_SHELL_REPLACE;
        decision.expected_source_revision=conflict.source_revision;
        decision.expected_destination_revision=conflict.destination_revision;
        s=ScShellFileOperationResolveConflict(b->filejob,&decision);
        if(s<0 && s!=SC_SHELL_E_BUSY){ScShellFileOperationCancel(b->filejob);build_fail(s);}
        return;
    }
    if(q.state<SC_SHELL_JOB_FINISHED)return;''')

path = 'user/studio2/STUDIO2.JSON'
old = (ROOT/path).read_bytes()
manifest = json.loads(old)
for name in ('ScShellFileOperationConflict', 'ScShellFileOperationResolveConflict'):
    assert all(row['name'] != name for row in manifest['imports'])
    manifest['imports'].append(dict(name=name, dependency='SCSHELL.SCL', kind='function', size=1))
encoded = (json.dumps(manifest, indent=2)+'\n').encode()
if b'\r\n' in old:encoded=encoded.replace(b'\n',b'\r\n')
change(path, old, encoded)

replace('user/settings2/settings_view.inc',
    '    settings.preview=d.preview_seconds;\n    if(!settings.preview)settings.owns_preview=0;',
    '''    /* deadline刚到时查询可先于内核display_poll：剩余秒已为0，模式
     * 尚未回退。由预览拥有者结清这一终态并重读，不能停表后留旧比例。 */
    if(settings.owns_preview && !d.preview_seconds){
        s=ScDisplayApply(0,0,0,SC32_DISPLAY_CANCEL);
        if(s<0)return s;
        INIT(d);
        s=ScDisplayQuery(&d);
        if(s<0)return s;
    }
    settings.preview=d.preview_seconds;
    if(!settings.preview)settings.owns_preview=0;''')

for path, (old, new) in rows.items():
    assert (ROOT/path).read_bytes() == old
for path, (old, new) in rows.items():
    (ROOT/path).write_bytes(new)
receipt = dict(status='FOCUSED_GUI03_FIXES_INTEGRATED_NOT_BUILT',
    gui_report_sha256=sha(report_path.read_bytes()), gui03_exit_code=1,
    gui03_final_resource_account='NOT_OBTAINED',
    heartbeat_original_exception='TimeoutError: 管理请求 10 无有效回复',
    modified={path:dict(before_sha256=sha(old),after_sha256=sha(new))
        for path,(old,new) in rows.items()},
    expected_native_refresh_labels=['SCUI','STUDIO2','SETTINGS2'],
    public_ABI_changed=False, build_started=False, guest_fix_verification=False)
(HERE/'integration.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(status=receipt['status'],files=len(rows)),ensure_ascii=False))
