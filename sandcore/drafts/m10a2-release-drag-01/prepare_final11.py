"""接收真实native10终态，复用未变布局和编译中间物，准备final11。"""
from pathlib import Path
import ast
import gzip
import hashlib
import json

ROOT = Path(__file__).resolve().parents[2]
HOST = 'build/m10a2-main-release-drag-01/p6-unified-host-20.json'
HOST_SHA = '4f0980b97bb3e8b95b99779a4273269108917df17936b864e5a541c913c658b7'
NATIVE = 'build/m10a2-p6-target-refresh-10/p6-generation.json'
VM = 'build/m10a2-p6-refresh-vm-10/refresh-vm.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    native_path, vm_path = ROOT / NATIVE, ROOT / VM
    native = json.loads(native_path.read_bytes())
    vm = json.loads(vm_path.read_bytes())
    assert native['status'] == 'DECLARED_P6_NATIVE_GENERATION_PASS'
    assert vm['status'] == 'EXPLICIT_LABEL_REFRESH_GENERATED'
    assert sha(native_path) == '1e39068eeec9de3027f1c04e666b0f24b60ec4795d5ef860a7f376994ffad740'
    assert sha(vm_path) == 'd37f10f5904e1ff3e5d9cd472047f20ca7ce7679b82abcf2eabddc78c3ed3b0c'
    assert sha(ROOT / HOST) == HOST_SHA
    assert vm['host_report_sha256'] == HOST_SHA
    for report in (native, vm):
        assert report['current_inputs_unchanged'] and report['source_disk_unchanged']
        assert report['changed_input_output_paths'] == []
    assert native['strict_reader_audit']['checks_not_weakened'] is True
    assert (len(native['objects']), len(native['native_libraries']), len(native['native_artifacts'])) == (27, 8, 21)
    owner = next(row for row in native['cases'] if 'user_clock_cache_proof' in row)
    assert owner['status'] == 'PASS' and owner['unexplained_delta'] == 0 and owner['unexpected'] == []
    assert owner['physical_delta'] == owner['attributed_physical_delta'] == owner['actual_user_clock_glyph_cache_pages']
    clock = owner['user_clock_cache_proof']
    assert clock['same_pause_owner_read'] and not clock['blanket_allowance'] and not clock['original_native07_reclassified']

    out = ROOT / 'drafts/m10a2-p6-final-11-receipt'
    out.mkdir(exist_ok=False)
    sources = {}
    for name in ('bindings.py', 'finalize.py', 'inspect_final.py'):
        source = ROOT / 'drafts/m10a2-p6-final-10-receipt' / name
        body = source.read_text(encoding='utf-8')
        sources[str(source.relative_to(ROOT))] = sha(source)
        # 先保护复用来源：final10的中间物只作为make输入，不能跳过新内核编译。
        body = body.replace("ROOT/'build/m10a2-p6-final-09/core'", "ROOT/'build/CORE_REUSE_SOURCE/core'")
        body = body.replace("b'build/m10a2-p6-final-09/core'", "b'build/CORE_REUSE_SOURCE/core'")
        for before, after in (
            ('final-10', 'final-11'), ('final10', 'final11'),
            ('build/m10a2-p6-target-refresh-09', 'build/m10a2-p6-target-refresh-10'),
            ('build/m10a2-p6-refresh-vm-09', 'build/m10a2-p6-refresh-vm-10'),
            ('build/m10a2-main-appfix-01/p6-unified-host-16.json', HOST),
            ('a38c9b326d9bb89d8daab3761fd95bac3976d504fe3d424c016e5ff96c137e29', HOST_SHA),
            ('57fa08a3e2ce36e350a0f88f6d56ab93a2418789ec22723ce2c762d10813f32b', sha(native_path)),
            ('0a90b72477f46fd02a73fea42d60debab2c7860c0bd3c164893bd7a0f6d5ff69', sha(vm_path)),
            ('HOST16', 'HOST20'), ('native09', 'native10'), ('outer09', 'outer10'), ('refresh09', 'refresh10'),
        ):
            body = body.replace(before, after)
        body = body.replace('CORE_REUSE_SOURCE', 'm10a2-p6-final-10')
        body = body.replace(
            '# CORE所嵌LOGIN/桌面/五库和内核未变；保留旧目录，只复用编译产物，\n    # 新路径依赖与发行头仍由本次make/只读检查核实。',
            '# 只复用final10的编译中间物；本次内核、发行头和两库有新字节，\n    # 必须由make按新依赖重编并由后续ELF读取确认，旧目录不改。')
        ast.parse(body)
        (out / name).write_bytes(body.encode('utf-8'))
    preparation = dict(status='NATIVE10_HOST20_FINAL11_PREPARED_NOT_EXECUTED', original_helpers=sources,
        native_report_sha256=sha(native_path), outer_report_sha256=sha(vm_path), host_report_sha256=HOST_SHA,
        changed_user_targets=['SC32API', 'SCUI', 'INSTALLER2'],
        core_reuses_final10_intermediate_objects=True, new_core_build_required=True, vm_started=False)
    (out / 'PREPARATION.json').write_text(json.dumps(preparation, indent=2) + '\n', encoding='utf-8')

    evidence = ROOT / 'evidence/m10a2-native10'
    evidence.mkdir(exist_ok=False)
    files = []
    for label, source in (('native-generation.json.gz', native_path), ('refresh-vm.json.gz', vm_path)):
        packed = gzip.compress(source.read_bytes(), mtime=0)
        (evidence / label).write_bytes(packed)
        files.append(dict(path=label, sha256=hashlib.sha256(packed).hexdigest(), original_sha256=sha(source)))
    review = dict(status='ACTUAL_NATIVE10_GENERATION_PASS_NOT_NEW_GUI_ACCEPTANCE', qemu_pid=15372,
        actual_driver_exit_code=0, current_inputs_unchanged=True, source_disk_unchanged=True,
        objects=27, ordinary_libraries=8, artifacts=21, changed_targets=['SC32API', 'SCUI', 'INSTALLER2'],
        strict_reader_not_weakened=True,
        owner={k: owner[k] for k in ('before_physical_pages', 'after_physical_pages', 'physical_delta',
            'attributed_physical_delta', 'unexplained_delta', 'unexpected', 'actual_user_clock_glyph_cache_pages')},
        clock=dict(new_glyphs=clock['new_glyphs'], new_arenas=clock['new_arenas'], same_pause_owner_read=True,
            blanket_allowance=False, original_native07_reclassified=False), files=files, full_M10a2=False)
    (evidence / 'review.json').write_text(json.dumps(review, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('NATIVE10_ARCHIVED_FINAL11_PREPARED_NOT_BUILT')


if __name__ == '__main__':
    main()
