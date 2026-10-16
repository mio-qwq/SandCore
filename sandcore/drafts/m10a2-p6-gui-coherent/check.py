"""只做本目录语法/接线/原像检查，不构建、不运行预检或VM，不制造模拟内存。"""
from pathlib import Path
import ast
import hashlib
import json
import difflib
import struct

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    pins = json.loads((HERE/'PINS.json').read_text(encoding='utf-8'))
    base = HERE.parent/'m10a2-p6-acceptance/driver.py'
    runtime = HERE.parent/'m10a2-p6-gui-runtime/runtime.py'
    observer = HERE.parent/'m10a2-gui-owner-review/owner_snapshot.py'
    assert sha(base) == pins['base_sha256']
    assert sha(runtime) == pins['runtime_base_sha256']
    assert sha(observer) == pins['owner_helper_sha256']
    assert sha(HERE/'driver.py') == pins['new_driver_sha256']
    modules = {}
    for path in HERE.glob('*.py'):
        modules[path.name] = ast.parse(path.read_text(encoding='utf-8'))
    driver = (HERE/'driver.py').read_text(encoding='utf-8')
    connected = (HERE/'coherent_runtime.py').read_text(encoding='utf-8')
    assert 'runtime.establish_baseline(active[0])' in driver
    assert driver.index('runtime.establish_baseline(active[0])') < driver.index("'ordinary-gui-login'")
    assert 'runtime = CoherentRuntime(' in driver
    assert 'harness.quiet()' not in driver and 'self.h.quiet()' not in connected
    assert 'owner.wait_original_host(' in connected and 'self.h.ledger(' in connected
    assert "shot['physical_pages'], resources_before, shot['resources']" in connected
    assert "view.report['input_sha256']" not in connected
    assert "input_sha256=dict(self.runtime_sources)" in connected
    assert "harness.report['current_input_sha256'] = dict(harness.report['input_sha256'])" in connected
    assert "self.original['pid'], self.original['generation']" in connected
    for name in ('ScuiInput(', 'FlowInput(', 'QemuSession(', 'data_prepare=candidate.install',
                 'audio=True', 'serve(runtime)', "report['status'] = 'P6_GUI_INPUT_SESSION_FINISHED_REVIEW_PENDING'"):
        assert name in driver
    old = ROOT/'build/m10a2-p6-final-01/sdk/SYS/SDK/SRC/user/scui/SCUIPRIVATE.H'
    current = ROOT/'user/scui/SCUIPRIVATE.H'
    old_text, new_text = old.read_text(encoding='utf-8'), current.read_text(encoding='utf-8')
    addition = 'static ScU32 ui_argb_over_partial(ScU32,ScU32);\n'
    assert new_text.count(addition) == 1 and new_text.replace(addition, '', 1) == old_text
    proof_path = ROOT/'build/m10a2-p6-gui-coherent-layout-01/layout-proof.json'
    assert sha(proof_path) == '95d1108d29e095223dca4f912e4ed3af57821d3ccbc0c7fdac3b699280ad79bd'
    proof = json.loads(proof_path.read_text(encoding='utf-8'))
    assert proof['status'] == 'ACTUAL_CURRENT_I386_LAYOUT_GENERATED_NO_GUEST' and proof['source_unchanged'] is True
    for name, info in proof['files'].items():
        path = proof_path.parent/name
        assert sha(path) == info['sha256'] and path.stat().st_size == info['bytes']
    layout = json.loads((proof_path.parent/'layout.json').read_text(encoding='utf-8'))
    assert proof['headers'] == layout['headers']
    assert all(sha(ROOT/name) == value for name, value in layout['headers'].items())
    assert sha(ROOT/proof['extractor']['relative']) == proof['extractor']['sha256']
    assert sha(proof_path.parent/'layout.c') == layout['source_sha256']
    assert (proof_path.parent/'layout.bin').read_bytes() == struct.pack('<'+'I'*len(layout['values']), *layout['values'].values())
    assert tuple(layout['values'][k] for k in ('desk_size', 'context_size', 'root_size')) == (1248, 672, 184)
    report = dict(status='STATIC_SOURCE_WIRING_ONLY_NOT_BUILT_NOT_VM_EXECUTED',
        parsed_files=sorted(modules), frozen_originals_unchanged=True,
        actual_new_layout_generated_by_this_agent=False, actual_Root_layout_read_only_verified=True,
        Root_layout_report=str(proof_path), Root_layout_report_sha256=sha(proof_path),
        Root_layout_sizes=dict(desk=1248, context=672, root=184),
        Candidate_preflight_executed=False, qemu_started=False,
        original_GUI01_failure_exempted=False,
        header_delta=dict(old_actual_archive=str(old), old_sha256=sha(old), current_path=str(current),
            current_sha256=sha(current), only_added_function_declaration=addition.strip(),
            all_other_header_text_identical=True,
            diff=''.join(difflib.unified_diff(old_text.splitlines(True), new_text.splitlines(True), fromfile=str(old), tofile=str(current)))))
    (HERE/'STATIC-REVIEW.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    files = {path.name: dict(sha256=sha(path), bytes=path.stat().st_size) for path in sorted(HERE.iterdir())
             if path.is_file() and path.name != 'DELIVERY.json'}
    (HERE/'DELIVERY.json').write_text(json.dumps(dict(status=report['status'], files=files), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))
    print('DELIVERY_SHA256', sha(HERE/'DELIVERY.json'))


if __name__ == '__main__':
    main()
