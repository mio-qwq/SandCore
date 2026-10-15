"""只从本目录真实调用和公开头生成候选manifest，不编译或改工作源。"""
from pathlib import Path
import hashlib
import json
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

def clean(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', ' ', text, flags=re.S)

inputs = sorted([*HERE.glob('*.C'), *HERE.glob('*.inc'), *HERE.glob('*.H')])
source = '\n'.join(clean(p.read_text(encoding='utf-8')) for p in inputs)
calls = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', source))
providers = {}
for rel in ('user/sc32/SC32API.json', 'user/stdlib/SCCLIB.json', 'user/sclib/SCLIB.json', 'user/scui/SCUI.json'):
    manifest = json.loads((ROOT / rel).read_text(encoding='utf-8'))
    for row in manifest['exports']:
        providers[row['name']] = (manifest['name'], row.get('kind', 'function'), row.get('size', 1))

shell_header = ROOT / 'drafts/m10a2-shell-r3/SCSHELL.H'
host_header = ROOT / 'drafts/m10a2-shell-host/user/SC32HOST.H'
for header, library in ((shell_header, 'SCSHELL.SCL'), (host_header, 'SC32API.SCL')):
    for name in re.findall(r'\b(Sc\w+)\s*\(', clean(header.read_text(encoding='utf-8'))):
        providers[name] = (library, 'function', 1)
for name in ('ScUiPainterClear', 'ScUiPainterTextColor', 'ScUiPainterBounds', 'ScUiContextWaitTimeout'):
    providers[name] = ('SCUI.SCL', 'function', 1)
unresolved = sorted(n for n in calls if n.startswith('Sc') and n not in providers)
if unresolved:
    raise SystemExit('公开合同尚未收齐: ' + ', '.join(unresolved))
if re.search(r'\bstderr\b', source):
    calls.add('__scc_standard')
imports = []
for name in sorted(calls & providers.keys()):
    dependency, kind, size = providers[name]
    imports.append(dict(name=name, dependency=dependency, kind=kind, size=size))
versions = {'SC32API.SCL': (1, 3, 4), 'SCUI.SCL': (1, 1, 2),
            'SCSHELL.SCL': (1, 0, 1), 'SCCLIB.SCL': (1, 0, 1), 'SCLIB.SCL': (1, 0, 1)}
dependencies = [dict(name=name, abi_major=versions[name][0], min_minor=versions[name][1], min_version=versions[name][2])
                for name in sorted({row['dependency'] for row in imports})]
manifest = dict(kind='application', name='SCSHELL.SCX', dependencies=dependencies, imports=imports)
(HERE / 'SCSHELL.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n')
proof = dict(status='SOURCE_CALL_MANIFEST_NOT_COMPILED_NOT_RUN', imports=len(imports),
             sources={p.name: dict(bytes=p.stat().st_size, sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in inputs},
             contracts={str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (shell_header, host_header)})
(HERE / 'source-manifest.json').write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8', newline='\n')
print(json.dumps(dict(status=proof['status'], imports=len(imports), source_bytes=sum(p.stat().st_size for p in inputs))))
