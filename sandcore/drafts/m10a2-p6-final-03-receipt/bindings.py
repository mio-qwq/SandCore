"""final03共同只读绑定；当前源码、原生producer与新观察布局分别保留来源。"""
from pathlib import Path
import hashlib
import importlib.util
import json
import struct
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = ROOT / 'build/m10a2-p6-final-03'
NATIVE = ROOT / 'build/m10a2-p6-target-refresh-04'
HOST = ROOT / 'build/m10a2-main-studio-01/p6-unified-host-09.json'
HOST_SHA = '879ceb99215972f60d1dfc76f0102d1a2255c8616685dd980eb4e8d5aba77ae4'
VM_REPORT = ROOT / 'build/m10a2-p6-refresh-vm-04/refresh-vm.json'
LAYOUT = ROOT / 'build/m10a2-p6-gui-coherent-layout-02/layout-proof.json'
OLD_LAYOUT = ROOT / 'build/m10a2-p6-gui-coherent-layout-01/layout-proof.json'
LAYOUT_ENTRY = ROOT / 'drafts/m10a2-p6-gui-coherent/layout_current.py'
LAYOUT_ENTRY_SHA = 'b55d66589377df5b65584172ba46ab5ad1dd20786672e1354e15f5a93363b815'
CORE_READER = ROOT / 'drafts/m10a2-p6-native-core-receipt/make_receipt.py'
CORE_READER_SHA = 'cd462220af530d171dd20de8b619915b3a7c2b1bf6645cc59c743f4c92c41337'
LIBS = ('SCLDR.SCL', 'SC32API.SCL', 'SCCLIB.SCL', 'SCLIB.SCL', 'SCUI.SCL', 'SCSHELL.SCL', 'SCGFX.SCL', 'SCTERM.SCL')
sys.path.insert(0, str(ROOT / 'tools'))
import stage_m10a2_sdk as sdk


def sha(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for body in iter(lambda: stream.read(1048576), b''):
            value.update(body)
    return value.hexdigest()


def require(value, why):
    if not value:
        raise ValueError(why)


def rel(path):
    return Path(path).resolve().relative_to(ROOT).as_posix()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def pin(path):
    path = Path(path)
    return dict(path=rel(path), bytes=path.stat().st_size, sha256=sha(path))


def artifact_path(row):
    path = Path(row['path'])
    path = path if path.is_absolute() else NATIVE / path
    path = path.resolve(strict=True)
    require(path.is_relative_to(NATIVE) and path.is_file(), 'native当前bundle路径不合法')
    require(path.stat().st_size == row['bytes'] and sha(path) == row['sha256'], 'native artifact精确字节改变')
    return path


class Binding:
    def __init__(self, native_sha, refresh_sha):
        require(native_sha and refresh_sha, '必须显式固定native04和outer refresh04完整SHA')
        self.checked = sdk.Inputs()
        require(sha(HOST) == HOST_SHA, 'Root明确HOST09完整报告不同')
        self.host, self.recorded, self.outputs = sdk.verified_build(HOST, self.checked)
        self.native = self.checked.json(NATIVE / 'p6-generation.json', native_sha)
        n = self.native
        require(n.get('status') == 'DECLARED_P6_NATIVE_GENERATION_PASS'
                and n.get('current_inputs_unchanged') is True and n.get('source_disk_unchanged') is True
                and n.get('changed_input_output_paths') == [], 'native04不是完整守恒PASS')
        require(len(n['objects']) == 27 and set(n['native_libraries']) == set(LIBS)
                and len(n['native_artifacts']) == 21
                and n.get('strict_reader_audit', {}).get('checks_not_weakened') is True, 'native04集合或严格reader不完整')
        require(n.get('source_snapshot_scope') == 'CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN', '当前准备态与原producer来源未分层')
        self.snapshot = n['source_input_sha256']
        require(self.snapshot and self.snapshot == n['input_sha256'], 'native当前source snapshot不一致')
        for name, wanted in self.snapshot.items():
            require(self.recorded.get(name) == wanted, 'native04当前源不等于HOST09 recorded：' + name)
            self.checked.read(sdk.relative(name), wanted)
        model_path = ROOT / 'drafts/m10a2-p6-guest-generation/model.py'
        spec = importlib.util.spec_from_file_location('final03_current_source_contract', model_path)
        model = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(model)
        current, _ = model.source_plan()
        require(current['source_input_sha256'] == self.snapshot, 'native04不是实际当前完整source_plan')
        for field in ('compiler_closure', 'source_closures', 'public_header_sources'):
            require(n['preflight'][field] == current[field], 'native04新preflight不是实际当前prepared：' + field)
        self.checked.read(model_path, '3087b2c093dd99350e4049214478bf188a23edb585a4766f529dbc16e50f5bb2')
        self.vm = self.checked.json(VM_REPORT, refresh_sha)
        v = self.vm
        require(v.get('status') == 'EXPLICIT_LABEL_REFRESH_GENERATED'
                and v.get('bundle_status') == 'DECLARED_P6_NATIVE_GENERATION_PASS', 'outer refresh04尚未显式完成')
        require(Path(v['bundle_report']).resolve() == (NATIVE / 'p6-generation.json').resolve()
                and Path(v['host_report']).resolve() == HOST.resolve()
                and v.get('qemu_pid') == n.get('qemu_pid'), 'outer不是同一HOST09/native04生成轮')
        host_pins = [value for name, value in v['current_input_sha256'].items() if Path(name).resolve() == HOST.resolve()]
        require(host_pins == [HOST_SHA], 'outer没有固定实际HOST09摘要')
        require(v.get('current_inputs_unchanged') is True and v.get('source_disk_unchanged') is True
                and v.get('changed_input_output_paths') == [], 'outer refresh04最终不守恒')
        self.roles = {}
        header_names = ('kernel/task_store.h', 'kernel/task.h', 'kernel/objpool.h',
                        'user/scui/SCUIPRIVATE.H', 'user/scshell/desktop/DESKTOP.H')
        for name in header_names:
            if name.startswith('kernel/'):
                expected, role = self.recorded[name], 'HOST09_CORE_COMPILE_INPUT'
            else:
                label, row = ('SCUI', n['native_libraries']['SCUI.SCL']) if '/scui/' in name else ('DESKTOP', n['native_artifacts']['SCSHELL.SCX'])
                closure = row.get('generation_origin', {}).get('include_manifest_closure', n['preflight']['source_closures'][label])
                expected, role = closure[name], label + '_ACTUAL_NATIVE_COMPILE_CLOSURE'
                require(self.snapshot.get(name) == expected, '新布局头不属于实际原生producer：' + name)
            require(self.recorded.get(name) == expected, '新布局头与HOST09不符：' + name)
            self.checked.read(ROOT / name, expected)
            self.roles[name] = dict(sha256=expected, role=role)
        self.metadata = dict(native_report=pin(NATIVE / 'p6-generation.json'), host_report=pin(HOST),
                             refresh_vm_report=pin(VM_REPORT), layout_header_source_roles=self.roles,
                             current_prepared_public_header_sources_exact=True,
                             historical_preflight_task_layout_not_used_for_new_layout=True)
        for path in HERE.glob('*.py'):
            self.checked.read(path)

    def layout(self, path, expected):
        require(expected, '必须显式固定实际新layout完整SHA')
        path = Path(path).resolve(strict=True)
        require(path.is_relative_to(ROOT / 'build') and path.parent.name.startswith('m10a2-p6-gui-coherent-layout-')
                and path.name == 'layout-proof.json' and path != OLD_LAYOUT.resolve(), 'final03只接受独立新layout凭证')
        proof = self.checked.json(path, expected)
        require(proof.get('status') == 'ACTUAL_CURRENT_I386_LAYOUT_GENERATED_NO_GUEST'
                and proof.get('source_unchanged') is True and proof.get('native_source_binding') == self.metadata,
                '新layout没有本轮真实原生/host来源绑定')
        require(set(proof['files']) == {'layout.json', 'layout.c', 'layout.o', 'layout.bin', 'build.log'}, '新layout实际编译文件不完整')
        for name, row in proof['files'].items():
            require(Path(name).name == name, 'layout文件不是叶名')
            self.checked.read(path.parent / name, row['sha256'], row['bytes'])
        layout = load(path.parent / 'layout.json')
        require(layout.get('method') == 'ACTUAL_I386_GCC_OFFSETOF_NO_GUEST_EXECUTION'
                and proof['headers'] == layout['headers'] == {name: row['sha256'] for name, row in self.roles.items()},
                '新layout头与实际当前原生/host不一致')
        require(sha(path.parent / 'layout.c') == layout['source_sha256']
                and (path.parent / 'layout.bin').read_bytes()
                == struct.pack('<' + 'I' * len(layout['values']), *layout['values'].values()), '新layout TU/bin/JSON不一致')
        require((path.parent / 'layout.o').read_bytes()[:7] == b'\x7fELF\x01\x01\x01'
                and '-m32' in layout['command'], '新layout不是实际i386 ELF编译')
        self.checked.read(ROOT / proof['extractor']['relative'], proof['extractor']['sha256'])
        self.checked.read(LAYOUT_ENTRY, LAYOUT_ENTRY_SHA)
        return proof
