#!/usr/bin/env python3
"""从显式构建与实际原生来源建立新SDK目录；不编译、装盘、运行或发布。"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import traceback

from scl_format import validate, FUNCTION, DATA, NO_ENTRY

ROOT = Path(__file__).resolve().parents[1]
LIBRARIES = {
    'SCLDR.SCL': ('user/runtime/SCLROOT.json', ('user/runtime/',)),
    'SC32API.SCL': ('user/sc32/SC32API.json', ('user/sc32/', 'user/guiapi/', 'user/fontapi/', 'user/brokerapi/', 'user/dropapi/')),
    'SCCLIB.SCL': ('user/stdlib/SCCLIB.json', ('user/stdlib/',)),
    'SCLIB.SCL': ('user/sclib/SCLIB.json', ('user/sclib/',)),
    'SCUI.SCL': ('user/scui/SCUI.json', ('user/scui/',)),
}
SHARED_HEADERS = ('user/SCTYPES.H', 'user/SC32GUI.H')
HEADERS = {
    'SCTYPES.H': 'user/SCTYPES.H', 'SC32API.H': 'user/SC32API.H',
    'SC32GUI.H': 'user/SC32GUI.H', 'SC32FONT.H': 'user/SC32FONT.H',
    'SC32PALETTE.H': 'user/SC32PALETTE.H', 'SC32POPUP.H': 'user/SC32POPUP.H',
    'SC32BROKER.H': 'user/SC32BROKER.H', 'SC32DROP.H': 'user/SC32DROP.H',
    'SCUI.H': 'user/SCUI.H', 'SCLIB.H': 'user/sclib/SCLIB.H',
    # 保持官方头原字节；其../SCTYPES.H必须在INC内正确解析。
    'RUNTIME/SCLDR.H': 'user/runtime/SCLDR.H',
}
STANDARD = ('stddef.h','stdint.h','stdbool.h','stdarg.h','limits.h','errno.h',
            'assert.h','stdlib.h','string.h','ctype.h','stdio.h')
FORBIDDEN = ('PRIVATE','WIRE','NUI','SCAPI','SCIO','SCMEM','KERNEL','SCLFMT','SCDYN')
DOCS = ('M10A2-SDK.md','SCL.md','SC32API.md','SC32-GUI-USER.md','SC32-BROKER.md',
        'SC32-DROP.md','SC32-FONT.md','SCUI.md','SCUI-TEXT.md','SCLIB.md','SCCLIB.md',
        'SCCC-SCL.md','SCCC-AGGREGATE-ABI.md','THIRD-PARTY.md')


def digest(body: bytes) -> str:
    return hashlib.sha256(body).hexdigest()


def sha(path: Path) -> str:
    with path.open('rb') as stream:
        value = hashlib.sha256()
        for block in iter(lambda: stream.read(1 << 20), b''):
            value.update(block)
    return value.hexdigest()


def json_bytes(body: bytes):
    def unique(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('JSON重复字段：' + key)
            result[key] = value
        return result
    return json.loads(body.decode('utf-8'), object_pairs_hook=unique)


def inside(path: Path, directory: Path) -> Path:
    value = path.resolve(strict=True)
    if not value.is_relative_to(directory.resolve()) or not value.is_file():
        raise ValueError('文件不在声明来源目录：' + str(value))
    return value


def relative(name: str) -> Path:
    if not isinstance(name, str) or '\\' in name or PurePosixPath(name).is_absolute():
        raise ValueError('非法源码相对路径：' + repr(name))
    if any(part in ('', '.', '..') for part in name.split('/')):
        raise ValueError('来源路径穿越/别名：' + name)
    return inside(ROOT / name, ROOT)


class Inputs:
    def __init__(self):
        self.pins: dict[Path, str] = {}

    def read(self, path: Path, expected=None, size=None, directory=ROOT) -> bytes:
        path = inside(path, directory)
        body = path.read_bytes()
        value = digest(body)
        if expected is not None and (not re.fullmatch(r'[0-9a-f]{64}', expected) or value != expected):
            raise ValueError('输入SHA与来源不同：' + str(path))
        if size is not None and (type(size) is not int or len(body) != size):
            raise ValueError('输入长度与来源不同：' + str(path))
        if path in self.pins and self.pins[path] != value:
            raise ValueError('读取期间输入改变：' + str(path))
        self.pins[path] = value
        return body

    def json(self, path: Path, expected=None, directory=ROOT):
        # 从同一次已固定的读取解析，避免hash和JSON之间再打开产生时间窗口。
        return json_bytes(self.read(path, expected, directory=directory))

    def unchanged(self):
        return [str(path) for path, value in self.pins.items()
                if not path.is_file() or sha(path) != value]


def verified_build(path: Path, inputs: Inputs):
    proof = inputs.json(path)
    if (proof.get('exit_code') != 0 or proof.get('source_files_modified_during_build') != []
            or not str(proof.get('scope', '')).startswith('M10A2_')):
        raise ValueError('需要显式成功且source_modified=[]的M10a2统一构建')
    recorded, outputs = {}, {}
    for category in ('inputs', 'outputs'):
        if not isinstance(proof.get(category), list) or not proof[category]:
            raise ValueError('构建证明缺完整' + category)
        seen = set()
        for item in proof[category]:
            name, value = item['path'], item['sha256']
            file = relative(name)
            if file in seen:
                raise ValueError('构建证明重复路径：' + name)
            seen.add(file)
            if category == 'outputs' and 'bytes' not in item:
                raise ValueError('构建output缺真实长度：' + name)
            inputs.read(file, value, item.get('bytes'))
            if name in recorded and recorded[name] != value:
                raise ValueError('input/output互相冲突：' + name)
            recorded[name] = value
            if category == 'outputs':
                outputs[file] = item
    return proof, recorded, outputs


def contract(blob: bytes, manifest: dict) -> dict:
    table = validate(blob)
    kind = {'function': FUNCTION, 'data': DATA}
    wanted_imports = {(v['name'], v['dependency'], kind[v['kind']], v['size']) for v in manifest['imports']}
    actual_imports = {(v['name'], v['dependency'], v['kind'], v['size']) for v in table['imports']}
    # 导出清单只声明名字/kind；真实函数正文宽度和数据宽度以SCL表为准。
    wanted_exports = {(v['name'], kind[v['kind']]) for v in manifest['exports']}
    actual_exports = {(v['name'], v['kind']) for v in table['exports']}
    dependencies = lambda values: {(v['name'], v['abi_major'], v['min_minor'], v['min_version']) for v in values}
    if (wanted_imports != actual_imports or wanted_exports != actual_exports
            or len(wanted_exports) != len(manifest['exports']) or len(wanted_imports) != len(manifest['imports'])
            or dependencies(manifest['dependencies']) != dependencies(table['dependencies'])
            or len(dependencies(manifest['dependencies'])) != len(manifest['dependencies'])):
        raise ValueError('SCL动态表与实际manifest不符：' + manifest['name'])
    for field in ('name', 'abi_major', 'abi_minor', 'library_version'):
        if table[field] != manifest[field]:
            raise ValueError('SCL版本/名字不符：' + field)
    for field in ('init','fini'):
        if (table[field + '_rva'] == NO_ENTRY) != (manifest[field] is None):
            raise ValueError('SCL生命周期entry与manifest不符')
    return table


def report_libraries(report):
    if 'native_libraries' in report:
        return report['native_libraries']
    # P3报告保留原schema；其generated_libraries还含两个整体状态位。
    values = report.get('generated_libraries', {})
    return {name: item for name, item in values.items() if name.endswith('.SCL')}


def provenance(path: Path, inputs: Inputs):
    path = inside(path, ROOT)
    value = inputs.json(path)
    out = path.parent.parent
    report_path = inside(path.parent / value['report'], out)
    report = inputs.json(report_path)
    if (value.get('schema') != 1 or value.get('guest_sccc_compiled') is not True
            or not str(report.get('status', '')).startswith('DECLARED_') or not report['status'].endswith('PASS')
            or report.get('current_inputs_unchanged') is not True or report.get('source_disk_unchanged') is not True):
        raise ValueError('不是完整、守恒的实际SCCC generation来源')
    libraries = report_libraries(report)
    applications = report.get('native_artifacts', {})
    if value.get('libraries') != libraries:
        raise ValueError('provenance库表与原始报告不同')
    if 'applications' in value and value['applications'] != applications:
        raise ValueError('provenance应用表与原始报告不同')
    snapshot = value.get('source_input_sha256')
    if not isinstance(snapshot, dict) or not snapshot:
        raise ValueError('provenance缺真实generation源码snapshot')
    for name, expected in snapshot.items():
        if report.get('input_sha256', {}).get(name) != expected:
            raise ValueError('provenance源码snapshot与完整报告不同：' + name)
    return value, report, report_path, out


def actual_native(name: str, provenance_path: Path, manifest: dict, inputs: Inputs, roots=(), public_headers=()):
    value, report, report_path, out = provenance(provenance_path, inputs)
    item = value['libraries'][name]
    item_path = Path(item['path'])
    if not item_path.is_absolute():
        item_path = provenance_path.parent / item_path
    body = inputs.read(item_path, item['sha256'], item['bytes'], out)
    origin = item.get('generation_origin')
    generation_report, generation_path = report, report_path
    source = {k: v for k, v in report.get('input_sha256', {}).items() if k.startswith(('user/', 'tests/'))}
    if origin:
        if value.get('source_snapshot_scope') != 'CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN':
            raise ValueError('refresh整体snapshot未区分旧生成来源')
        if value.get('artifact_generation', {}).get(name) != origin:
            raise ValueError('refresh逐产物generation来源不符')
        if origin['mode'] == 'REUSED':
            generation_path = inside(Path(origin['report']), ROOT)
            generation_report = inputs.json(generation_path, origin['report_sha256'])
            if (not str(generation_report.get('status', '')).startswith('DECLARED_')
                    or not generation_report['status'].endswith('PASS')
                    or generation_report.get('current_inputs_unchanged') is not True
                    or generation_report.get('source_disk_unchanged') is not True):
                raise ValueError('复用generation原报告不是守恒成功轮')
            old = report_libraries(generation_report)[name]
            if (old['sha256'] != item['sha256'] or old['bytes'] != item['bytes']
                    or old['compile_case'] != item['compile_case']
                    or item.get('generated_this_vm') is not False):
                raise ValueError('复用byte不是原generation输出')
        elif origin['mode'] != 'FRESH' or item.get('generated_this_vm') is not True:
            raise ValueError('未知或不真实的generation类别')
        source = origin['source_input_sha256']
        for key, expected in source.items():
            if generation_report.get('input_sha256', {}).get(key) != expected:
                raise ValueError('逐产物snapshot不是原generation源码：' + key)
    cases = [v for v in generation_report['cases'] if v['name'] == item['compile_case']]
    if (len(cases) != 1 or cases[0].get('status') != 'PASS' or cases[0].get('exit_code') != 0
            or cases[0].get('generated_sha256') != item['sha256']
            or cases[0].get('generated_bytes') != len(body)
            or item.get('guest_sccc_compiled') is not True):
        raise ValueError('缺实际成功compiler case：' + name)
    if cases[0].get('stdout_path'):
        # 小型归档保留完整原报告，其stdout绝对路径仍指向原验证目录。
        # 不猜另一份同名日志；只读取报告给出的精确路径及SHA，且不离开仓库。
        inputs.read(Path(cases[0]['stdout_path']), cases[0]['stdout_sha256'], cases[0]['stdout_bytes'])
    required = set(public_headers) | set(SHARED_HEADERS)
    for folder in roots:
        files = [p for p in (ROOT / folder).rglob('*') if p.is_file() and p.suffix.lower() in ('.c','.h','.inc','.asm','.json')
                 and 'draft' not in p.name.lower()]
        for file in files:
            key = file.relative_to(ROOT).as_posix()
            required.add(key)
    for key in sorted(required):
        if key not in source:
            raise ValueError('native generation缺自身源码/public布局集合：' + key)
        inputs.read(relative(key), source[key])
    table = contract(body, manifest)
    return body, table, dict(source='ACTUAL_SCCC_NATIVE', provenance=str(provenance_path),
        provenance_sha256=sha(provenance_path), origin_report=str(generation_path), origin_report_sha256=sha(generation_path),
        generation_case=item['compile_case'], generation_source_input_sha256=source,
        current_unchanged_source_closure={key: source[key] for key in sorted(required)},
        source_reuse_rule='OWN_IMPLEMENTATION_AND_SHARED_LAYOUT_EXACT_PROVIDER_ABI_AND_IMPORT_CLOSURE',
        compiler_sha256=cases[0].get('compiler_sha256'), bytes=len(body), sha256=digest(body),
        prior_generated_file_executed=item.get('executed_this_generated_file', False),
        staging_is_execution_evidence=False)


def compiler(path: Path, inputs: Inputs, recorded: dict):
    value, report, report_path, out = provenance(path, inputs)
    item = value.get('applications', {}).get('SCCC1.SCX')
    if not item:
        raise ValueError('compiler provenance缺真实SCCC1.SCX')
    target = Path(item['path'])
    if not target.is_absolute():
        target = path.parent / target
    body = inputs.read(target, item['sha256'], item['bytes'], out)
    if len(body) < 36 or body[:8] != b'SCX1MIO\0':
        raise ValueError('G1不是旧SCX容器')
    entry, load, bss, stack, flags, base, author = struct.unpack_from('<7I',body,8)
    if (entry >= load or len(body) != 36+load or load > 262108 or load+bss > 0x3d0000
            or stack != 131072 or flags or base != 0x400000 or author != 0x004f494d):
        raise ValueError('G1 load/BSS/stack/入口不符')
    origin = item.get('generation_origin')
    if origin and (value.get('artifact_generation', {}).get('SCCC1.SCX') != origin
                   or value.get('source_snapshot_scope') != 'CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN'):
        raise ValueError('G1逐产物generation来源不符')
    if origin and origin['mode'] == 'REUSED':
        report_path = inside(Path(origin['report']), ROOT)
        report = inputs.json(report_path, origin['report_sha256'])
        old = report['native_artifacts']['SCCC1.SCX']
        if old['sha256'] != item['sha256'] or old['bytes'] != item['bytes'] or old['compile_case'] != item['compile_case']:
            raise ValueError('复用G1不是原byte')
    elif origin and (origin['mode'] != 'FRESH' or item.get('generated_this_vm') is not True):
        raise ValueError('G1未知generation类别')
    cases = [v for v in report['cases'] if v['name'] == item['compile_case']]
    if (len(cases) != 1 or cases[0].get('status') != 'PASS' or cases[0].get('exit_code') != 0
            or cases[0].get('generated_sha256') != item['sha256']
            or cases[0].get('generated_bytes') != len(body)
            or item.get('guest_sccc_compiled') is not True or item.get('executed_this_generated_file') is not True):
        raise ValueError('G1缺真实生成及曾执行来源')
    inputs.read(Path(cases[0]['stdout_path']), cases[0]['stdout_sha256'], cases[0]['stdout_bytes'])
    # 编译器自身实现须与当前指定build一致；未启用GUI源码不进入CLI闭包。
    for name, value in report['input_sha256'].items():
        if name.startswith('user/s3c') or name in ('user/sccc.c','user/COMPILECLI.inc','user/SCAPI.H','user/SCIO.H'):
            if recorded.get(name) != value:
                raise ValueError('当前compiler源与G1不同：' + name)
    return body, dict(source='ACTUAL_GUEST_SCCC_G1_CLI', bytes=len(body), sha256=digest(body),
        provenance=str(path), provenance_sha256=sha(path), generation_case=item['compile_case'],
        original_generation_source_input_sha256=report['input_sha256'],
        origin_report=str(report_path), origin_report_sha256=sha(report_path), old_SCX_container=True,
        source_filename='sccc.c', SDK_filename='SYS/BIN/SCCC.SCX')


def closure(libraries):
    visiting, done, order = set(), set(), []
    def visit(name):
        if name in visiting:
            raise ValueError('SCL依赖环：' + name)
        if name in done:
            return
        visiting.add(name)
        table = libraries[name]['table']
        for dep in table['dependencies']:
            if dep['name'] not in libraries:
                raise ValueError('SDK缺实际依赖：' + dep['name'])
            provider = libraries[dep['name']]['table']
            if (provider['abi_major'] != dep['abi_major'] or provider['abi_minor'] < dep['min_minor']
                    or provider['library_version'] < dep['min_version']):
                raise ValueError('SDK依赖版本不满足：' + dep['name'])
            visit(dep['name'])
        for symbol in table['imports']:
            provider = libraries[symbol['dependency']]['table']
            targets = [v for v in provider['exports'] if v['name'] == symbol['name'] and v['kind'] == symbol['kind']]
            if len(targets) != 1 or targets[0]['size'] < symbol['size']:
                raise ValueError('SDK导入缺符号/宽度：' + symbol['name'])
        visiting.remove(name);done.add(name);order.append(name)
    for name in sorted(libraries):
        visit(name)
    return order


def header_allowed(source: Path, target: str):
    name = source.name
    if ('kernel' in source.relative_to(ROOT).parts or source.suffix not in ('.H','.h')
            or any(value in name.upper() for value in FORBIDDEN)):
        raise ValueError('私有/旧bridge/内核头不能进入public INC：' + str(source))
    if name not in STANDARD and not re.fullmatch(r'[A-Z][A-Z0-9_]*\.H', name):
        raise ValueError('非标准自有头必须全大写：' + name)
    if not target.startswith('SYS/INC/'):
        raise ValueError('header target必须public INC')


def public_include_graph(plan):
    headers = {name[len('SYS/INC/'):]: value for name, value in plan.items() if name.startswith('SYS/INC/')}
    for name, value in headers.items():
        text = value['body'].decode('utf-8')
        for directive in re.findall(r'(?m)^\s*#\s*include\s+([^\n]+)', text):
            if not directive.startswith(('"', '<')):
                raise ValueError('SDK public include不能使用不可枚举的宏路径：' + name)
        for quote, angle in re.findall(r'(?m)^\s*#\s*include\s*(?:"([^"]+)"|<([^>]+)>)',text):
            requested = quote or angle
            candidate = str(PurePosixPath(name).parent / requested) if quote else requested
            parts = []
            for part in candidate.split('/'):
                if part == '..':
                    if not parts:
                        raise ValueError('public include逃出INC：' + name)
                    parts.pop()
                elif part not in ('','.'):parts.append(part)
            resolved = '/'.join(parts)
            if resolved not in headers:
                # angle旧式公共头也可能按INC根定位。
                if requested not in headers:
                    raise ValueError('public include闭包缺项：' + name + ' -> ' + requested)


def library_headers(name):
    if name == 'SC32API.SCL':
        return tuple(source for source in HEADERS.values()
                     if source.startswith('user/SC32') or source == 'user/runtime/SCLDR.H')
    if name == 'SCUI.SCL':
        return tuple(source for source in HEADERS.values() if source != 'user/runtime/SCLDR.H') + ('user/textdata/SCUTEXTDATA.H',)
    if name == 'SCLIB.SCL':
        return ('user/sclib/SCLIB.H',)
    if name == 'SCLDR.SCL':
        return ('user/runtime/SCLDR.H',)
    return ()


def plugin_roots(values):
    if not isinstance(values, list) or not values:
        raise ValueError('plugin需完整、显式source_roots')
    result = []
    for value in values:
        if (not isinstance(value, str) or not value.startswith('user/') or not value.endswith('/')
                or any(part in ('', '.', '..') for part in value[:-1].split('/'))):
            raise ValueError('plugin source_roots须为user内明确目录：' + repr(value))
        path = (ROOT / value).resolve(strict=True)
        if not path.is_relative_to(ROOT / 'user') or not path.is_dir():
            raise ValueError('plugin source_root离开用户库源码')
        result.append(value)
    if len(set(result)) != len(result):
        raise ValueError('重复plugin source_root')
    return tuple(result)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--build-report',type=Path,required=True)
    parser.add_argument('--profile',choices=('native-preview','host-preview'),default='native-preview')
    parser.add_argument('--native-library',action='append',default=[],metavar='NAME=PROVENANCE_JSON')
    parser.add_argument('--compiler-provenance',type=Path)
    parser.add_argument('--plugin-manifest',type=Path,action='append',default=[])
    args=parser.parse_args();args.out=args.out.resolve()
    if args.out.exists():raise FileExistsError('SDK staging目录必须全新，拒绝覆盖')
    inputs=Inputs();plan={};libraries={};providers={};created=False;license_components=[]
    def add(target,body,source):
        if ('\\' in target or PurePosixPath(target).is_absolute() or len(target.encode('ascii'))>63
                or any(p in ('','..','.') for p in target.split('/'))):
            raise ValueError('非法/超过SandFS63B目标：' + target)
        if target.upper() in {name.upper() for name in plan}:
            raise ValueError('SDK目标名字冲突（SandFS不区分ASCII大小写）：' + target)
        plan[target]=dict(body=body,source=source,bytes=len(body),sha256=digest(body))
    try:
        inputs.read(Path(__file__).resolve())
        inputs.read(relative('tools/scl_format.py'))
        proof,recorded,outputs=verified_build(args.build_report.resolve(strict=True),inputs)
        build=args.build_report.resolve().parent
        for entry in args.native_library:
            name,separator,path=entry.partition('=')
            if not separator or name in providers:raise ValueError('native-library格式/重复：' + entry)
            providers[name]=Path(path).resolve(strict=True)
        plugin_specs=[]
        for path in args.plugin_manifest:
            spec=inputs.json(path.resolve(strict=True))
            if set(spec)!={'schema','library_manifest','native_provenance','source_roots','public_headers','resources','license_manifests'} or spec['schema']!=1:
                raise ValueError('plugin SDK manifest缺字段/未知字段')
            plugin_roots(spec['source_roots'])
            for key in ('public_headers', 'resources', 'license_manifests'):
                if not isinstance(spec[key], list):
                    raise ValueError('plugin字段须为列表：' + key)
            declared=inputs.json(relative(spec['library_manifest']))
            name=declared['name']
            if name in LIBRARIES or any(v[0]==name for v in plugin_specs):raise ValueError('plugin库重复核心/扩展名字')
            if name in providers:raise ValueError('plugin来源已由其manifest提供，不能重复native-library')
            providers[name]=relative(spec['native_provenance']);plugin_specs.append((name,spec,declared))
        if args.profile=='native-preview' and set(providers)!=set(LIBRARIES)|{v[0] for v in plugin_specs}:
            raise ValueError('native-preview必须逐名显式提供完整核心五库/所有plugin真实来源')
        if args.profile=='host-preview' and (providers or args.compiler_provenance):
            raise ValueError('HOST profile不混入未标记native source；选择native-preview')
        specs=[(name,manifest,roots,None,library_headers(name)) for name,(manifest,roots) in LIBRARIES.items()]
        specs += [(name,spec['library_manifest'],plugin_roots(spec['source_roots']),declared,spec['public_headers']) for name,spec,declared in plugin_specs]
        for name,manifest_name,roots,declared,public_headers in specs:
            if manifest_name not in recorded:
                raise ValueError('library manifest不在当前build：' + manifest_name)
            manifest_path=relative(manifest_name);manifest_body=inputs.read(manifest_path,recorded[manifest_name])
            declared=declared or json_bytes(manifest_body)
            if args.profile=='native-preview':body,table,origin=actual_native(name,providers[name],declared,inputs,roots,public_headers)
            else:
                file=inside(build/name,build)
                if file not in outputs:raise ValueError('HOST库未在指定build输出：'+name)
                body=inputs.read(file,outputs[file]['sha256'],outputs[file]['bytes']);table=contract(body,declared)
                origin=dict(source='HOST_GCC_BUILD_ONLY_NOT_NATIVE',bytes=len(body),sha256=digest(body),staging_is_execution_evidence=False)
            libraries[name]=dict(table=table,origin=origin)
            add('SYS/LIB/'+name,body,origin)
            add('SYS/SDK/LINK/'+name[:-4]+'.JSON',manifest_body,str(manifest_path))
        order=closure(libraries)
        if args.profile=='native-preview':
            if not args.compiler_provenance:raise ValueError('native-preview需实际G1 compiler provenance')
            compiler_body,compiler_origin=compiler(args.compiler_provenance.resolve(strict=True),inputs,recorded)
        else:
            file=inside(build/'fs/bin/sccc.scx',build)
            if file not in outputs:raise ValueError('HOST SCCC CLI不在指定build')
            compiler_body=inputs.read(file,outputs[file]['sha256'],outputs[file]['bytes'])
            compiler_origin=dict(source='HOST_GCC_SCCC0_NOT_NATIVE',bytes=len(compiler_body),sha256=digest(compiler_body))
        add('SYS/BIN/SCCC.SCX',compiler_body,compiler_origin)
        for target,name in HEADERS.items():
            file=relative(name);header_allowed(file,'SYS/INC/'+target)
            if name not in recorded:raise ValueError('public header未被当前build固定：'+name)
            add('SYS/INC/'+target,inputs.read(file,recorded[name]),name)
        for name in STANDARD:
            source='user/stdlib/'+name;file=relative(source);header_allowed(file,'SYS/INC/'+name)
            if source not in recorded:raise ValueError('standard header未被当前build固定：'+source)
            add('SYS/INC/'+name,inputs.read(file,recorded[source]),source)
        license_names=['third_party/M10A2-SOURCES.json','third_party/M10-SOURCES.json']
        for name,spec,_ in plugin_specs:
            for source in spec['public_headers']:
                file=relative(source);header_allowed(file,'SYS/INC/'+file.name)
                if source not in recorded:raise ValueError('plugin public header不在当前build：'+source)
                add('SYS/INC/'+file.name,inputs.read(file,recorded[source]),source)
            for resource in spec['resources']:
                if set(resource)!={'source','target'}:raise ValueError('plugin resource只接受source/target')
                if resource['target'].upper().startswith(('SYS/INC/','SYS/LIB/','SYS/BIN/')):raise ValueError('resource不能覆盖API/代码')
                source=resource['source'];file=relative(source)
                if source not in recorded:raise ValueError('plugin resource未被build固定：'+source)
                add(resource['target'],inputs.read(file,recorded[source]),source)
            license_names.extend(spec['license_manifests'])
        public_include_graph(plan)
        # Unicode与凤凰原件/完整许可原样放入；不是一行作者替代实际原文。
        for license_name in dict.fromkeys(license_names):
            index=inputs.json(relative(license_name),recorded.get(license_name))
            selected=index['components']
            if license_name=='third_party/M10-SOURCES.json':selected=[v for v in selected if v['source']=='third_party/vonwaon']
            for component in selected:
                if not component['guest'].startswith('SYS/LICENSE/'):
                    raise ValueError('license component只能进入SYS/LICENSE：'+component['name'])
                if component['license'] not in index['allowed_licenses']:
                    raise ValueError('license component未经其固定清单登记：'+component['name'])
                license_components.append(dict(index=license_name,name=component['name'],
                    license=component['license'],revision=component['revision'],upstream=component['upstream'],
                    source=component['source'],guest=component['guest']))
                for file,value in component['files'].items():
                    source=component['source']+'/'+file
                    add(component['guest']+'/'+file,inputs.read(relative(source),value),dict(license=component['license'],source=source,upstream=component['upstream']))
                for file in component.get('local_files',[]):
                    source=component['source']+'/'+file;add(component['guest']+'/'+file,inputs.read(relative(source)),source)
            target='SYS/LICENSE/'+Path(license_name).name.upper()
            add(target,inputs.read(relative(license_name)),license_name)
        for source,target in (('assets/ui/UAX15_1.SCU','SYS/UI/UAX15_1.SCU'),
                ('assets/ui/tests/GRAPHEME.SCT','SYS/UI/TEST/GRAPHEME.SCT'),
                ('assets/ui/tests/WORD.SCT','SYS/UI/TEST/WORD.SCT'),
                ('assets/ui/tests/PROVENANCE.json','SYS/UI/TEST/PROVENANCE.JSON')):
            if source not in recorded:raise ValueError('Unicode资源未被当前build固定：'+source)
            add(target,inputs.read(relative(source),recorded[source]),source)
        for source,target in (('third_party/vonwaon/VonwaonBitmap-12px.ttf','SYS/FONT/PHOENIX12.TTF'),
                              ('third_party/vonwaon/VonwaonBitmap-16px.ttf','SYS/FONT/PHOENIX16.TTF')):
            add(target,inputs.read(relative(source)),source)
        project_license=ROOT.parent/'LICENSE'
        add('SYS/LICENSE/SDK/LICENSE.TXT',inputs.read(project_license,directory=ROOT.parent),str(project_license))
        examples=ROOT/'examples/m10a2-sdk'
        for file in sorted(examples.iterdir()):
            if not file.is_file() or file.suffix not in ('.C','.json','.md'):continue
            add('SYS/SDK/EXAMPLES/'+file.name.upper(),inputs.read(file),str(file))
        for name in DOCS:
            file=relative('docs/'+name)
            add('SYS/SDK/DOC/'+name.upper(),inputs.read(file),str(file))
        commands=['/SYS/BIN/SCCC.SCX /SYS/SDK/EXAMPLES/EDIT2.C /HOME/EDIT2.SCX --dynamic /SYS/SDK/EXAMPLES/EDIT2.JSON',
                  '/SYS/BIN/SCCC.SCX /SYS/SDK/EXAMPLES/UTILITY.C /HOME/UTILITY.SCX --dynamic /SYS/SDK/EXAMPLES/UTILITY.JSON']
        source_map={str(path):value for path,value in inputs.pins.items()}
        provenance_body=(json.dumps(dict(schema=1,scope='SDK_COPY_SOURCE_AND_GENERATION_ORIGINS_ONLY',
            profile=args.profile,build_report=str(args.build_report.resolve()),
            build_report_sha256=inputs.pins[args.build_report.resolve()],
            compiler=compiler_origin,libraries={name:value['origin'] for name,value in libraries.items()},
            license_components=license_components,original_input_sha256=source_map,
            staging_is_execution_evidence=False,full_M10a2=False),ensure_ascii=False,indent=2)+'\n').encode('utf-8')
        add('PROVENANCE.json',provenance_body,'GENERATED_FROM_PINNED_INPUTS')
        add('GUEST-COMMANDS.txt',('\n'.join(commands)+'\n').encode('ascii'),'GENERATED_COMMANDS_NOT_EXECUTED')
        manifest=dict(schema=1,status='PREVIEW_STAGING_ONLY_NOT_RELEASE_OR_RUNTIME_ACCEPTANCE',profile=args.profile,
            build_report=str(args.build_report.resolve()),build_report_sha256=sha(args.build_report),
            compiler=compiler_origin,libraries={name:dict(origin=value['origin'],abi_major=value['table']['abi_major'],
                abi_minor=value['table']['abi_minor'],library_version=value['table']['library_version'],
                imports=value['table']['imports'],exports=value['table']['exports'],dependencies=value['table']['dependencies']) for name,value in libraries.items()},
            dependency_initialization_order=order,guest_compile_commands=commands,license_components=license_components,
            examples_status='SOURCE_WRITTEN_NOT_COMPILED_OR_RUN',full_M10a2=False,Phase7_complete=False,
            installed_into_live_SYS=False,ZIP_created=False,SDK_runtime_verified=False,
            files={name:{k:v for k,v in row.items() if k!='body'} for name,row in plan.items()})
        changed=inputs.unchanged()
        if changed:raise ValueError('preflight输入改变：'+repr(changed))
        args.out.mkdir(parents=True,exist_ok=False);created=True
        for name,row in plan.items():
            file=args.out/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(row['body'])
            if sha(file)!=row['sha256']:raise ValueError('staging副本SHA错误：'+name)
        changed=inputs.unchanged()
        if changed:raise ValueError('staging期间输入改变：'+repr(changed))
        manifest['inputs_unchanged']=True
        manifest['input_sha256']={str(path):value for path,value in inputs.pins.items()}
        manifest_body=(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n').encode('utf-8')
        (args.out/'MANIFEST.json').write_bytes(manifest_body)
        hashes={name:dict(bytes=row['bytes'],sha256=row['sha256']) for name,row in sorted(plan.items())}
        hashes['MANIFEST.json']=dict(bytes=len(manifest_body),sha256=digest(manifest_body))
        # 摘要文件不自引用；SHA256SUMS覆盖HASHES、MANIFEST、来源、命令与全部副本。
        hash_body=(json.dumps(dict(schema=1,self_excluded=True,files=hashes),ensure_ascii=False,indent=2)+'\n').encode('utf-8')
        (args.out/'HASHES.json').write_bytes(hash_body)
        hashes['HASHES.json']=dict(bytes=len(hash_body),sha256=digest(hash_body))
        (args.out/'SHA256SUMS').write_bytes(''.join(row['sha256']+'  '+name+'\n' for name,row in sorted(hashes.items())).encode('ascii'))
        for name,row in hashes.items():
            if (args.out/name).stat().st_size != row['bytes'] or sha(args.out/name) != row['sha256']:
                raise ValueError('最终staging副本/metadata摘要错误：'+name)
        changed=inputs.unchanged()
        if changed:raise ValueError('metadata提交期间输入改变：'+repr(changed))
        print('SDK_PREVIEW_STAGED files=%d bytes=%d runtime_verified=false' % (len(plan),sum(v['bytes'] for v in plan.values())))
    except BaseException as error:
        if created:
            (args.out/'STAGING-FAILED.json').write_text(json.dumps(dict(status='FAILED_NOT_USABLE_SDK',error=repr(error),
                traceback=traceback.format_exc(),changed_inputs=inputs.unchanged(),no_source_removed=True),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raise


if __name__=='__main__':main()
