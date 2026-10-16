"""本轮固定来源接线；只补副文件witness与旧运行底座布局，不放宽原reader。"""
from contextlib import contextmanager
from pathlib import Path
import hashlib
import importlib.util
import json
import struct
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ALLOWED = ('SCSHELL', 'NOTES2', 'STUDIO2')
PREVIOUS = 'build/m10a2-p6-target-refresh-03/p6-generation.json'
PREVIOUS_SHA = 'fbc1c4519c9f432a67772eb9b7989b166edb7ce29a20a65cc88c5bae63774f2f'
WITNESS = 'build/m10a2-p6-refresh-vm-03/refresh-vm.json'
ORIGINAL = 'build/m10a2-p6-guest-generation-run-03/p6-generation.json'
CORE = 'build/m10a2-p6-final-02/core'
SDK = 'build/m10a2-p6-final-02/sdk'
LAYOUT = 'build/m10a2-p6-gui-coherent-layout-01/layout-proof.json'
HOST07 = 'build/m10a2-main-studio-01/p6-unified-host-07.json'


def sha(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            value.update(block)
    return value.hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def inside(path):
    path = Path(path)
    if not path.is_absolute():
        path = ROOT / path
    path = path.resolve(strict=True)
    if not path.is_relative_to(ROOT) or not path.is_file():
        raise ValueError('只接受仓库内真实文件：' + str(path))
    return path


class Basis:
    def __init__(self):
        self.pins, self.sidecar_audit, self.layout_roles = {}, [], {}
        self.frozen = read(HERE / 'PINS.json')
        for name, row in self.frozen['files'].items():
            self.pin(name, row['sha256'], row['bytes'])
        if self.frozen['files'][PREVIOUS]['sha256'] != PREVIOUS_SHA:
            raise ValueError('不能改写Root指定的完整previous SHA')
        self.old = read(ROOT / PREVIOUS)
        self.outer = read(ROOT / WITNESS)
        self.original = read(ROOT / ORIGINAL)
        for report in (self.old, self.original):
            if (report.get('status') != 'DECLARED_P6_NATIVE_GENERATION_PASS'
                    or report.get('current_inputs_unchanged') is not True
                    or report.get('source_disk_unchanged') is not True
                    or report.get('changed_input_output_paths') != []):
                raise ValueError('原生生成链必须是完整守恒PASS')
        if (self.outer.get('status') != 'THREE_TARGET_REFRESH_GENERATED'
                or self.outer.get('bundle_status') != self.old['status']
                or inside(self.outer['bundle_report']) != inside(ROOT / PREVIOUS)
                or self.outer.get('qemu_pid') != self.old.get('qemu_pid')
                or self.outer.get('current_inputs_unchanged') is not True
                or self.outer.get('source_disk_unchanged') is not True
                or self.outer.get('changed_input_output_paths') != []):
            raise ValueError('外层witness没有绑定同一轮守恒生成')
        if (inside(self.outer['host_report']) != inside(ROOT / HOST07)
                or self.outer['current_input_sha256'].get(str(inside(ROOT / HOST07)))
                != self.frozen['files'][HOST07]['sha256']):
            raise ValueError('外层witness没有固定实际HOST07来源')
        self.manifest = read(ROOT / SDK / 'MANIFEST.json')
        if self.manifest.get('profile') != 'native-preview' or self.manifest.get('inputs_unchanged') is not True:
            raise ValueError('final02 SDK不是严格native-preview来源')
        self.core_receipt = read(ROOT / 'drafts/m10a2-p6-final-02-receipt/core-build-receipt.json')
        self.core_inputs = read(ROOT / 'drafts/m10a2-p6-final-02-receipt/core-build-inputs.json')
        if (self.core_receipt['build_step']['exit_code'] != 0
                or self.core_receipt['source_files_changed_during_build'] != []
                or self.core_receipt['dependency_closure']['sha256']
                != self.frozen['files']['drafts/m10a2-p6-final-02-receipt/core-build-inputs.json']['sha256']):
            raise ValueError('final02 CORE实际构建与依赖凭证不匹配')
        for row in self.core_receipt['artifacts'].values():
            self.pin(row['path'], row['sha256'], row['bytes'])
        self.gets, self.sidecars = {}, {}
        for name, report in ((PREVIOUS, self.old), (ORIGINAL, self.original), (WITNESS, self.outer)):
            for index, row in enumerate(report['cases']):
                if not all(key in row for key in ('guest', 'path', 'bytes', 'sha256', 'generation')):
                    continue
                if row.get('status') != 'PASS' or row['generation'] <= 0 or row.get('qemu_pid') != report['qemu_pid']:
                    raise ValueError('actual-get来源状态/代数/PID不合法')
                path = inside(row['path'])
                self.gets.setdefault(path, []).append((name, index, row))
        for collection in ('native_libraries', 'native_artifacts', 'objects'):
            for name, row in self.old[collection].items():
                self.pin(row['path'], row['sha256'], row['bytes'])
                suffixes = ('.log', '.map') if collection == 'objects' else ('.map',)
                if collection != 'objects' and 'debug' not in row and Path(row['path'] + '.log').is_file():
                    suffixes += ('.log',)
                if collection != 'objects' and 'debug' in row:
                    self.pin(row['path'] + '.sdbg', row['debug']['sha256'], row['debug']['bytes'])
                for suffix in suffixes:
                    path = inside(row['path'] + suffix)
                    if path in self.sidecars:
                        raise ValueError('副文件清单路径重复')
                    self.sidecars[path] = (collection, name, (row.get('inherited_sidecars') or {}).get(suffix))
        self.layout = self.load_layout()

    def pin(self, path, expected, size=None):
        path = inside(path)
        if sha(path) != expected or size is not None and path.stat().st_size != size:
            raise ValueError('固定输入的字节/SHA不同：' + str(path))
        if path in self.pins and self.pins[path] != expected:
            raise ValueError('同一输入出现冲突摘要')
        self.pins[path] = expected
        return path

    def source(self, name, expected):
        # 旧底座已编译正文可从final02 SDK归档取得，不能用HOST08新正文替代它。
        for candidate in (ROOT / SDK / 'SYS/SDK/SRC' / name, ROOT / name):
            if candidate.is_file() and sha(candidate) == expected:
                return self.pin(candidate, expected)
        raise ValueError('缺少旧运行编译闭包的原文：' + name)

    def load_layout(self):
        proof = read(ROOT / LAYOUT)
        if (proof.get('status') != 'ACTUAL_CURRENT_I386_LAYOUT_GENERATED_NO_GUEST'
                or proof.get('source_unchanged') is not True
                or set(proof['files']) != {'layout.json', 'layout.c', 'layout.o', 'layout.bin', 'build.log'}):
            raise ValueError('不能用历史layout头摘要替代当前实编布局')
        folder = (ROOT / LAYOUT).parent
        for name, row in proof['files'].items():
            if Path(name).name != name:
                raise ValueError('布局产物不是叶名')
            self.pin(folder / name, row['sha256'], row['bytes'])
        layout = read(folder / 'layout.json')
        if (layout.get('method') != 'ACTUAL_I386_GCC_OFFSETOF_NO_GUEST_EXECUTION'
                or layout['headers'] != proof['headers']
                or sha(folder / 'layout.c') != layout['source_sha256']
                or (folder / 'layout.bin').read_bytes()
                != struct.pack('<' + 'I' * len(layout['values']), *layout['values'].values())):
            raise ValueError('实编layout TU/bin/JSON/头不一致')
        self.pin(proof['extractor']['relative'], proof['extractor']['sha256'])
        for name, expected in layout['headers'].items():
            if name.startswith('kernel/'):
                if self.core_inputs['non_generated_compile_sources'].get(name) != expected:
                    raise ValueError('布局头不属于实际final02 CORE的.d闭包：' + name)
                role = 'CORE_FINAL02_ACTUAL_D_CLOSURE'
            else:
                label = {'user/scui/SCUIPRIVATE.H': 'SCUI',
                         'user/scshell/desktop/DESKTOP.H': 'DESKTOP'}.get(name)
                if not label or self.old['preflight']['source_closures'][label].get(name) != expected:
                    raise ValueError('布局头不属于真实SCUI/Desktop原生编译闭包：' + name)
                role = label + '_PREVIOUS_ACTUAL_NATIVE_CLOSURE'
            path = self.source(name, expected)
            self.layout_roles[name] = dict(path=str(path), sha256=expected, role=role)
        for path, row in self.core_inputs['d_files'].items():
            self.pin(path, row['sha256'])
        for group in ('non_generated_compile_sources', 'assembler_sources'):
            for name, expected in self.core_inputs[group].items():
                self.source(name, expected)
        for group in ('generated_headers', 'generated_private_sources'):
            for name, expected in self.core_inputs[group].items():
                self.pin(name, expected)
        # runtime底座每份closure仍来自真正原生producer；不改旧preflight.task_layout。
        for label in ('SCLDR', 'SC32API', 'SCCLIB', 'SCLIB', 'SCUI', 'SCSHELL', 'SCGFX', 'SCTERM', 'DESKTOP'):
            for name, expected in self.old['preflight']['source_closures'][label].items():
                self.source(name, expected)
        return layout

    def resolve_sidecar(self, source, expected=None):
        source = inside(source)
        if source not in self.sidecars:
            raise ValueError('副文件不在固定previous的真实产物清单内')
        collection, name, inherited = self.sidecars[source]
        matches = self.gets.get(source, [])
        copied_from = None
        if not matches and inherited:
            copied_from = inside(inherited['source_path'])
            matches = self.gets.get(copied_from, [])
        if len(matches) != 1:
            raise ValueError('副文件缺唯一真实get witness：' + str(source))
        report_path, index, row = matches[0]
        if inherited and (row['sha256'] != inherited['sha256'] or row['bytes'] != inherited['bytes']):
            raise ValueError('继承复制记录与原始actual-get不一致')
        if expected is not None and expected != row['sha256']:
            raise ValueError('调用方formal副文件摘要与actual-get不一致')
        self.pin(source, row['sha256'], row['bytes'])
        if copied_from is not None:
            self.pin(copied_from, row['sha256'], row['bytes'])
        proof = dict(report=str(ROOT / report_path),
                     report_sha256=self.frozen['files'][report_path]['sha256'],
                     case_index=index, case_name=row['name'], guest=row['guest'],
                     generation=row['generation'], qemu_pid=row['qemu_pid'],
                     bytes=row['bytes'], sha256=row['sha256'],
                     inherited_alias_source=str(copied_from) if copied_from else None)
        return source, proof

    def audit_sidecars(self):
        counts = {'direct_actual_get': 0, 'inherited_actual_get_alias': 0}
        for source in self.sidecars:
            _, evidence = self.resolve_sidecar(source)
            key = 'inherited_actual_get_alias' if evidence['inherited_alias_source'] else 'direct_actual_get'
            counts[key] += 1
        return dict(status='FIXED_READ_ONLY_WITNESSES_VERIFIED_NOT_REFRESH_EXECUTION', **counts,
                    changed_labels_not_yet_evaluated=True)

    @contextmanager
    def bind_sidecars(self, target):
        original = target.recorded_sidecar

        def checked(source, destination, report, expected=None):
            if report is not self.old:
                raise ValueError('本witness适配只允许固定previous，不接受任意报告')
            source, witness = self.resolve_sidecar(source, expected)
            body = target.copy_checked(source, destination, witness['sha256'], witness['bytes'])
            value = dict(bytes=len(body), sha256=witness['sha256'], source_path=str(source),
                         actual_get_witness=witness)
            self.sidecar_audit.append(dict(source_path=str(source), **witness))
            return value

        target.recorded_sidecar = checked
        try:
            yield
        finally:
            target.recorded_sidecar = original

    def legacy(self):
        path = inside('drafts/m10a2-p6-refresh-runner/driver.py')
        spec = importlib.util.spec_from_file_location('frozen_refresh_runner', path)
        legacy = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(legacy)
        if (Path(legacy.target.__file__).resolve() != inside('drafts/m10a2-p6-target-refresh/refresh.py')
                or Path(legacy.wait_original_host.__globals__['__file__']).resolve()
                != inside('drafts/m10a2-gui-owner-review/owner_snapshot.py')):
            raise ValueError('实际导入的helper不是固定来源')
        return legacy
