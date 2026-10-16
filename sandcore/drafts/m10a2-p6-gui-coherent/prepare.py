"""从固定原driver生成新目录内的接线；只写本目录，不构建或启动VM。"""
from pathlib import Path
import difflib
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASE = HERE.parent/'m10a2-p6-acceptance/driver.py'
EXPECTED = 'e2ed3b1ff27ae0162121e9162d642700ed8d20fe8fa8209482e7842009759878'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('实际基线原文不是唯一匹配：' + old[:80])
    return text.replace(old, new, 1)


def main():
    if sha(BASE) != EXPECTED:
        raise ValueError('Root原acceptance driver已改变，不能覆盖新变化')
    before = BASE.read_text(encoding='utf-8')
    after = once(before, 'from runtime import GuiRuntime, serve',
                 'from runtime import serve\nfrom coherent_runtime import CoherentRuntime, ObserverPlan')
    after = once(after, "    parser.add_argument('--execute', action='store_true')",
                 "    parser.add_argument('--layout-report', type=Path, required=True)\n    parser.add_argument('--execute', action='store_true')")
    after = once(after, '    pins = dict(candidate.input_pins.pins)',
                 '    owner_plan = ObserverPlan(candidate, args.layout_report)\n    pins = dict(candidate.input_pins.pins)\n    pins.update(owner_plan.pins)')
    after = once(after, "                      HERE.parent/'m10a2-p6-gui-runtime', HERE.parent/'m10a2-p6-final-flow'):",
                 "                      HERE.parent/'m10a2-p6-gui-runtime', HERE.parent/'m10a2-p6-final-flow',\n                      HERE.parent/'m10a2-gui-owner-review'):")
    after = once(after, '            qemu_started=False), ensure_ascii=False))',
                 '            qemu_started=False, coherent_owner_source_binding=owner_plan.metadata), ensure_ascii=False))')
    after = once(after, '                runtime = GuiRuntime(flow, harness, session_parser=sessions, cold_font=True)',
                 '                runtime = CoherentRuntime(flow, harness, owner_plan=owner_plan, session_parser=sessions, cold_font=True)')
    after = once(after, '''                before, observed = harness.quiet()
                runtime.baseline = (before, observed, harness.resources())
                report['baseline'] = dict(pages=before, observations=observed, resources=runtime.baseline[2])
''', '')
    after = once(after, "                report['original_active_session'] = next(row['id'] for row in original if row['active'])",
                 "                active = [row for row in original if row['active']]\n                if len(active) != 1:raise ValueError('缺唯一真实原SID')\n                report['original_active_session'] = active[0]['id']\n                runtime.establish_baseline(active[0])")
    (HERE/'driver.py').write_text(after, encoding='utf-8', newline='\n')
    patch = ''.join(difflib.unified_diff(before.splitlines(True), after.splitlines(True),
        fromfile='a/drafts/m10a2-p6-acceptance/driver.py', tofile='b/drafts/m10a2-p6-gui-coherent/driver.py'))
    (HERE/'driver.patch').write_text(patch, encoding='utf-8', newline='\n')
    record = dict(status='SOURCE_ONLY_NOT_BUILT_NOT_VM_EXECUTED', base=str(BASE), base_sha256=EXPECTED,
                  new_driver_sha256=sha(HERE/'driver.py'), patch_sha256=sha(HERE/'driver.patch'),
                  original_files_modified=False,
                  runtime_base_sha256=sha(HERE.parent/'m10a2-p6-gui-runtime/runtime.py'),
                  owner_helper_sha256=sha(HERE.parent/'m10a2-gui-owner-review/owner_snapshot.py'))
    (HERE/'PINS.json').write_text(json.dumps(record, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(record, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
