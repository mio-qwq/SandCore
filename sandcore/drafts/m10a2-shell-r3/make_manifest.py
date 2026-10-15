#!/usr/bin/env python3
"""Derive a candidate SCSHELL manifest from public declarations and source references.

This is a static audit only. Undefined ELF symbols and actual SCCC compilation
must independently confirm the candidate before integration is accepted.
"""
import hashlib
import json
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)

def code(text):
    return SOURCE.sub(lambda m: '\n' * m[0].count('\n') + ' ', text)

def declarations(text, prefix):
    return set(re.findall(r'\b(?:ScStatus|void)\s+(' + prefix + r'\w+)\s*\(', code(text)))

def main():
    sources = [HERE / 'SCSHELL.C'] + sorted(HERE.glob('shell_*.c'))
    bodies = '\n'.join(code(path.read_text(encoding='utf-8')) for path in sources)
    header = (HERE / 'SCSHELL.H').read_text(encoding='utf-8')
    exports = declarations(header, 'ScShell')
    definitions = re.findall(r'\b(ScShell\w+)\s*\([^;{}]*\)\s*\{', bodies)
    missing = exports - set(definitions)
    duplicate = sorted(name for name in set(definitions) if definitions.count(name) != 1)
    extra = set(definitions) - exports - {'ScShellFinalize'}
    if missing or duplicate or extra or 'ScShellFinalize' not in definitions:
        raise ValueError(dict(missing=sorted(missing), duplicate=duplicate, extra=sorted(extra)))
    providers = {}
    manifests = ['user/sc32/SC32API.json', 'user/stdlib/SCCLIB.json',
                 'user/sclib/SCLIB.json', 'user/scui/SCUI.json']
    inputs = sources + [HERE / 'SCSHELL.H', HERE / 'SCSHELLPRIVATE.H', HERE / 'SCSHELLWIRE.H']
    for name in manifests:
        path = ROOT / name
        data = json.loads(path.read_text(encoding='utf-8'))
        inputs.append(path)
        for entry in data['exports']:
            symbol = entry['name']
            if symbol in providers:
                raise ValueError('Duplicate provider: ' + symbol)
            providers[symbol] = (data['name'], entry.get('kind', 'function'))
    for relative in ['drafts/m10a2-shell-host/user/SC32HOST.H', 'drafts/m10a2-shell-fs/user/SC32LEASE.H']:
        path = ROOT / relative
        inputs.append(path)
        for symbol in declarations(path.read_text(encoding='utf-8'), 'Sc'):
            providers[symbol] = ('SC32API.SCL', 'function')
    for symbol in ['ScUiPainterClear', 'ScUiPainterTextColor', 'ScUiPainterBounds', 'ScUiContextWaitTimeout']:
        providers[symbol] = ('SCUI.SCL', 'function')
    identifiers = set(re.findall(r'\b[A-Za-z_]\w*\b', bodies))
    used = identifiers & providers.keys()
    # ISO headers introduce these runtime calls through their public macros.
    if identifiers & {'stdin', 'stdout', 'stderr'}:
        used.add('__scc_standard')
    if 'errno' in identifiers:
        errno_header = ROOT / 'user/stdlib/errno.h'
        inputs.append(errno_header)
        errno_text = errno_header.read_text(encoding='utf-8')
        for symbol in re.findall(r'\b(__scc_\w+)\s*\(', errno_text):
            if symbol in providers:
                used.add(symbol)
    unresolved = sorted(set(re.findall(r'\b(Sc[A-Z]\w*)\s*\(', bodies)) - exports - {'ScShellFinalize'} - providers.keys())
    if unresolved:
        raise ValueError('Unknown public call references: ' + ', '.join(unresolved))
    imports = [dict(name=name, kind=providers[name][1], dependency=providers[name][0], size=1)
               for name in sorted(used)]
    versions = {'SC32API.SCL': (1, 3, 4), 'SCUI.SCL': (1, 1, 2),
                'SCCLIB.SCL': (1, 0, 1), 'SCLIB.SCL': (1, 0, 1)}
    dependencies = [dict(name=name, abi_major=versions[name][0], min_minor=versions[name][1],
                         min_version=versions[name][2]) for name in sorted({x['dependency'] for x in imports})]
    manifest = dict(name='SCSHELL.SCL', abi_major=1, abi_minor=0, library_version=1,
                    init=None, fini='ScShellFinalize', dependencies=dependencies,
                    exports=[dict(name=name, kind='function') for name in sorted(exports)], imports=imports)
    (HERE / 'SCSHELL.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    info = dict(status='STATIC_CANDIDATE_NOT_COMPILED_NOT_RUN', exports=len(exports), imports=len(imports),
                definitions=len(definitions), macro_note='stdio/errno public macro runtime references included',
                inputs=[dict(path=path.relative_to(ROOT).as_posix(), bytes=path.stat().st_size,
                             sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for path in sorted(set(inputs))])
    (HERE / 'source-manifest.json').write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({k:v for k,v in info.items() if k != 'inputs'}, ensure_ascii=False))

if __name__ == '__main__':
    main()
