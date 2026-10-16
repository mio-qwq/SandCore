"""固定native04生产链与final04运行底座；保留原refresh严格读写合同。"""
from contextlib import contextmanager
from pathlib import Path
import copy
import hashlib
import importlib.util
import json
import struct
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PREVIOUS = 'build/m10a2-p6-target-refresh-04/p6-generation.json'
PREVIOUS_SHA = '1ac7e951434d5465a665844019a731609a08c35c4a22a5f42bb6c43fac2b5191'
WITNESS = 'build/m10a2-p6-refresh-vm-04/refresh-vm.json'
PRIOR = 'build/m10a2-p6-target-refresh-03/p6-generation.json'
PRIOR_WITNESS = 'build/m10a2-p6-refresh-vm-03/refresh-vm.json'
ORIGINAL = 'build/m10a2-p6-guest-generation-run-03/p6-generation.json'
GENERATION_HOST = 'build/m10a2-main-studio-01/p6-unified-host-09.json'
RUNTIME_HOST = 'build/m10a2-main-studio-01/p6-unified-host-10.json'
CORE = 'build/m10a2-p6-final-04/core'
SDK = 'build/m10a2-p6-final-04/sdk'
LAYOUT = 'build/m10a2-p6-gui-coherent-layout-02/layout-proof.json'
RECEIPT = 'drafts/m10a2-p6-final-04-receipt/'


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


def frozen_module(name, relative, expected):
    path = inside(relative)
    if sha(path) != expected:
        raise ValueError('已审helper来源不同：' + relative)
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_BASE = frozen_module('frozen_v4_base', 'drafts/m10a2-p6-followup-refresh/followup.py',
                      '9f6e7497d11a0edc5d5b3485c68e873191c13b22941ada3e1723443bed956e60')
_V2 = frozen_module('frozen_v4_validation', 'drafts/m10a2-p6-followup-refresh-v2/followup.py',
                    '8db9a192c694eeef65979ffc757a93097b60b36e8b7c4c959834b7358dcb9e18')
validated_labels = _V2.validated_labels
exact_changed_labels = _V2.exact_changed_labels
prepared_header_map = _V2.prepared_header_map


class Basis(_BASE.Basis):
    def __init__(self):
        self.pins, self.sidecar_audit, self.layout_roles = {}, [], {}
        self.frozen = read(HERE / 'PINS.json')
        for name, row in self.frozen['files'].items():
            self.pin(name, row['sha256'], row['bytes'])
        if self.frozen['files'][PREVIOUS]['sha256'] != PREVIOUS_SHA:
            raise ValueError('不能改写Root指定的完整native04 SHA')
        self.reports = {name: read(ROOT / name) for name in
                        (PREVIOUS, WITNESS, PRIOR, PRIOR_WITNESS, ORIGINAL)}
        self.old, self.outer = self.reports[PREVIOUS], self.reports[WITNESS]
        self.original = self.reports[ORIGINAL]
        for name in (PREVIOUS, PRIOR, ORIGINAL):
            report = self.reports[name]
            if (report.get('status') != 'DECLARED_P6_NATIVE_GENERATION_PASS'
                    or report.get('current_inputs_unchanged') is not True
                    or report.get('source_disk_unchanged') is not True
                    or report.get('changed_input_output_paths') != []
                    or len(report['native_libraries']) != 8
                    or len(report['native_artifacts']) != 21 or len(report['objects']) != 27):
                raise ValueError('固定原生生成链不是完整8/21/27守恒PASS：' + name)
        for outer_name, inner_name, status in (
                (WITNESS, PREVIOUS, 'EXPLICIT_LABEL_REFRESH_GENERATED'),
                (PRIOR_WITNESS, PRIOR, 'THREE_TARGET_REFRESH_GENERATED')):
            outer, inner = self.reports[outer_name], self.reports[inner_name]
            if (outer.get('status') != status or outer.get('bundle_status') != inner['status']
                    or inside(outer['bundle_report']) != inside(inner_name)
                    or outer.get('qemu_pid') != inner.get('qemu_pid')
                    or outer.get('current_inputs_unchanged') is not True
                    or outer.get('source_disk_unchanged') is not True
                    or outer.get('changed_input_output_paths') != []):
                raise ValueError('外层actual-get报告没有绑定同一轮守恒生成')
        if (inside(self.outer['host_report']) != inside(GENERATION_HOST)
                or self.outer['host_report_sha256'] != self.frozen['files'][GENERATION_HOST]['sha256']
                or self.outer['current_input_sha256'].get(str(inside(GENERATION_HOST)))
                != self.frozen['files'][GENERATION_HOST]['sha256']):
            raise ValueError('native04 producer必须保持HOST09，不能改写为运行HOST10/未来HOST11')
        self.manifest = read(ROOT / SDK / 'MANIFEST.json')
        if (self.manifest.get('profile') != 'native-preview'
                or self.manifest.get('inputs_unchanged') is not True
                or inside(self.manifest['build_report']) != inside(RUNTIME_HOST)
                or self.manifest['build_report_sha256'] != self.frozen['files'][RUNTIME_HOST]['sha256']):
            raise ValueError('final04 SDK不是固定HOST10严格native-preview')
        self.core_receipt = read(ROOT / RECEIPT / 'core-build-receipt.json')
        self.core_inputs = read(ROOT / RECEIPT / 'core-build-inputs.json')
        final = read(ROOT / 'build/m10a2-p6-final-04/finalization.json')
        if (final['status'] != 'CORE_AND_STRICT_SDK_VERIFIED_NOT_GUEST_BOOT'
                or final['native_report']['sha256'] != PREVIOUS_SHA
                or final['host_report']['sha256'] != self.frozen['files'][RUNTIME_HOST]['sha256']
                or self.core_receipt['build_step']['exit_code'] != 0
                or self.core_receipt['source_files_changed_during_build'] != []
                or self.core_receipt['dependency_closure']['sha256']
                != self.frozen['files'][RECEIPT + 'core-build-inputs.json']['sha256']):
            raise ValueError('final04实际CORE构建、native04与HOST10来源不匹配')
        for row in self.core_receipt['artifacts'].values():
            self.pin(row['path'], row['sha256'], row['bytes'])
        host = read(ROOT / RUNTIME_HOST)
        if host['exit_code'] != 0 or host['source_files_modified_during_build'] != []:
            raise ValueError('旧运行HOST10不是实际守恒成功构建')
        self.runtime_inputs = {row['path']: row['sha256'] for row in host['inputs']}
        self.gets, self.aliases, self.sidecars = {}, {}, {}
        for report_name, report in self.reports.items():
            for index, row in enumerate(report['cases']):
                if not all(key in row for key in ('guest', 'path', 'bytes', 'sha256', 'generation')):
                    continue
                if (row.get('status') != 'PASS' or row['generation'] <= 0
                        or row.get('qemu_pid') != report['qemu_pid']):
                    raise ValueError('actual-get状态/代数/PID不合法')
                self.gets.setdefault(inside(row['path']), []).append((report_name, index, row))
            if 'objects' not in report:
                continue
            for collection in ('native_libraries', 'native_artifacts', 'objects'):
                for name, row in report[collection].items():
                    suffixes = ('.log', '.map') if collection == 'objects' else ('.map',)
                    if collection != 'objects' and 'debug' not in row and Path(row['path'] + '.log').is_file():
                        suffixes += ('.log',)
                    if report_name == PREVIOUS:
                        self.pin(row['path'], row['sha256'], row['bytes'])
                        if 'debug' in row:
                            self.pin(row['path'] + '.sdbg', row['debug']['sha256'], row['debug']['bytes'])
                    for suffix in suffixes:
                        path = inside(row['path'] + suffix)
                        if path in self.aliases:
                            raise ValueError('历史副文件清单路径重复')
                        entry = (report_name, (row.get('inherited_sidecars') or {}).get(suffix))
                        self.aliases[path] = entry
                        if report_name == PREVIOUS:
                            self.sidecars[path] = entry
        self.layout = self.load_layout()

    def source(self, name, expected):
        # 旧运行正文优先取final04 SDK归档；新SCUI正文不能冒充旧观察布局。
        for candidate in (ROOT / SDK / 'SYS/SDK/SRC' / name, ROOT / name):
            if candidate.is_file() and sha(candidate) == expected:
                return self.pin(candidate, expected)
        raise ValueError('缺少旧运行观察所需原文：' + name)

    def load_layout(self):
        proof = read(ROOT / LAYOUT)
        if (proof.get('status') != 'ACTUAL_CURRENT_I386_LAYOUT_GENERATED_NO_GUEST'
                or proof.get('source_unchanged') is not True
                or proof['wrapper_execution']['exit_code'] != 0
                or set(proof['files']) != {'layout.json', 'layout.c', 'layout.o', 'layout.bin', 'build.log'}):
            raise ValueError('layout02必须保持真实i386编译凭证')
        binding = proof['native_source_binding']
        for key, name in (('native_report', PREVIOUS), ('host_report', GENERATION_HOST),
                          ('refresh_vm_report', WITNESS)):
            if (binding[key]['path'] != name or binding[key]['sha256']
                    != self.frozen['files'][name]['sha256']):
                raise ValueError('layout02实际编译来源链不同')
        folder = (ROOT / LAYOUT).parent
        for name, row in proof['files'].items():
            if Path(name).name != name:
                raise ValueError('布局产物不是叶名')
            self.pin(folder / name, row['sha256'], row['bytes'])
        layout = read(folder / 'layout.json')
        if (layout['method'] != 'ACTUAL_I386_GCC_OFFSETOF_NO_GUEST_EXECUTION'
                or layout['headers'] != proof['headers']
                or sha(folder / 'layout.c') != layout['source_sha256']
                or (folder / 'layout.bin').read_bytes()
                != struct.pack('<' + 'I' * len(layout['values']), *layout['values'].values())):
            raise ValueError('layout02 TU/bin/JSON/头不一致')
        self.pin(proof['extractor']['relative'], proof['extractor']['sha256'])
        for name, expected in layout['headers'].items():
            if name.startswith('kernel/'):
                if (self.core_inputs['non_generated_compile_sources'].get(name) != expected
                        or self.runtime_inputs.get(name) != expected):
                    raise ValueError('布局头不属于实际final04/HOST10 CORE闭包')
                role = 'FINAL04_HOST10_ACTUAL_D_CLOSURE'
            else:
                label = {'user/scui/SCUIPRIVATE.H': 'SCUI',
                         'user/scshell/desktop/DESKTOP.H': 'DESKTOP'}.get(name)
                if not label or self.old['preflight']['source_closures'][label].get(name) != expected:
                    raise ValueError('布局头不属于native04实际编译闭包')
                role = label + '_NATIVE04_ACTUAL_CLOSURE'
            self.layout_roles[name] = dict(path=str(self.source(name, expected)), sha256=expected, role=role)
        for name, row in self.core_inputs['d_files'].items():
            self.pin(name, row['sha256'])
        for group in ('generated_headers', 'generated_private_sources'):
            for name, expected in self.core_inputs[group].items():
                self.pin(name, expected)
        # CORE二进制和完整.d凭证固定旧实现，不要求未来当前kernel实现倒退。
        # 观察器实际解释的五个结构头和PRIVATE_SOURCE_PINS仍逐字节核实；
        # 其余历史闭包摘要与HOST10记录核对，不能混写到当前源码watch表。
        for group in ('non_generated_compile_sources', 'assembler_sources'):
            for name, expected in self.core_inputs[group].items():
                if self.runtime_inputs.get(name) != expected:
                    raise ValueError('实际final04 CORE闭包与固定HOST10源记录不同：' + name)
        for label in ('SCLDR', 'SC32API', 'SCCLIB', 'SCLIB', 'SCUI', 'SCSHELL', 'SCGFX', 'SCTERM', 'DESKTOP'):
            for name, expected in self.old['preflight']['source_closures'][label].items():
                self.source(name, expected)
        return layout

    def resolve_sidecar(self, source, expected=None):
        source = inside(source)
        if source not in self.sidecars:
            raise ValueError('副文件不在native04真实清单内')
        current, chain, recorded = source, [], []
        while True:
            if current in chain or current not in self.aliases:
                raise ValueError('副文件继承环或不属于固定历史产物')
            chain.append(current)
            matches = self.gets.get(current, [])
            if matches:
                if len(matches) != 1:
                    raise ValueError('副文件actual-get不唯一')
                report_name, index, row = matches[0]
                break
            inherited = self.aliases[current][1]
            if not inherited:
                raise ValueError('副文件缺真实get或明确继承链：' + str(current))
            recorded.append(inherited)
            current = inside(inherited['source_path'])
        if expected is not None and expected != row['sha256']:
            raise ValueError('调用方副文件摘要与真实get不一致')
        proof = dict(report=str(ROOT / report_name),
                     report_sha256=self.frozen['files'][report_name]['sha256'],
                     case_index=index, case_name=row['name'], guest=row['guest'],
                     generation=row['generation'], qemu_pid=row['qemu_pid'],
                     bytes=row['bytes'], sha256=row['sha256'],
                     inherited_alias_source=str(current) if len(chain) > 1 else None,
                     inherited_alias_chain=[str(path) for path in chain[1:]])
        for inherited in recorded:
            if inherited['bytes'] != row['bytes'] or inherited['sha256'] != row['sha256']:
                raise ValueError('继承记录与最终actual-get字节不同')
            witness = inherited.get('actual_get_witness')
            if witness:
                for key in ('report', 'report_sha256', 'case_index', 'case_name', 'guest',
                            'generation', 'qemu_pid', 'bytes', 'sha256'):
                    if witness.get(key) != proof[key]:
                        raise ValueError('历史actual_get_witness与真实固定get不同：' + key)
                alias = witness.get('inherited_alias_source')
                if alias and inside(alias) not in chain:
                    raise ValueError('历史witness指向非本链别名')
        for path in chain:
            self.pin(path, row['sha256'], row['bytes'])
        return source, proof

    @contextmanager
    def bind_generation(self, target, prepared):
        mapping = prepared_header_map(prepared)
        old_mapping = copy.deepcopy(self.old['preflight']['public_header_sources'])
        original = target.inherit

        def inherit(current, old, out):
            if current is not prepared or old is not self.old or prepared_header_map(current) != mapping:
                raise ValueError('当前prepared/map/previous身份发生变化')
            result = original(current, old, out)
            if result.get('source_snapshot_scope') != target.SCOPE:
                raise ValueError('新报告不是当前准备快照合同')
            result['preflight']['public_header_sources'] = dict(mapping)
            result['source_reuse']['current_public_header_sources'] = dict(
                origin='ACTUAL_CURRENT_PREPARED_MAP', entries=len(mapping),
                previous_map_retained_in_previous_report=True,
                runtime_observer_layout_origin='FROZEN_FINAL04_LAYOUT02_NOT_NEW_SCUI_HEADERS')
            target.save(Path(out) / 'p6-generation.json', result)
            return result

        with self.bind_sidecars(target):
            target.inherit = inherit
            try:
                yield
            finally:
                target.inherit = original
                if self.old['preflight']['public_header_sources'] != old_mapping:
                    raise ValueError('禁止修改历史公开头映射')
