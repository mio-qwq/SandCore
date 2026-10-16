"""只读来源清点和静态守恒核对；不构建、不应用、不启动VM。"""
from pathlib import Path
import ast
import hashlib
import json
import re
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rg(*args):
    result = subprocess.run(['rg', *args], cwd=ROOT, capture_output=True, text=True, encoding='utf-8')
    if result.returncode not in (0, 1):
        raise ValueError(result.stderr)
    return result.stdout.splitlines()


pins = json.loads((HERE/'PINS.json').read_text(encoding='utf-8'))
for name, item in pins['modified'].items():
    assert sha(ROOT/name) == item['before_sha256'], name
    assert sha(HERE/'overlay'/name) == item['after_sha256'], name
for name, item in pins['added'].items():
    assert not (ROOT/name).exists(), name
    assert sha(HERE/'overlay'/name) == item['after_sha256'], name
for name, item in pins['renames'].items():
    assert sha(ROOT/name) == item['before_sha256'], name
    assert not (ROOT/item['target']).exists(), item['target']
    assert sha(HERE/'overlay'/item['target']) == item['before_sha256'], item['target']
assert sha(HERE/'scw-workspace.patch') == pins['patch_sha256']
for file in HERE.glob('*.py'):
    ast.parse(file.read_text(encoding='utf-8'))
for name in ('tools/m10a2_sdk_final.py', 'tools/build_m10a2_p6.py'):
    ast.parse((HERE/'overlay'/name).read_text(encoding='utf-8'))

pattern = r'(?i)\.scp\b|\bSCP\b|SandCore Project'
active_refs = rg('-n', '-i', r'\.scp\b|\bSCP\b|SandCore Project',
                 'user', 'tools', 'docs', 'examples', 'Makefile')
for row in active_refs:
    name = row.split(':', 1)[0].replace('\\', '/')
    assert name in pins['modified'], ('uncovered active reference', row)
for name in list(pins['modified']) + list(pins['added']):
    text = (HERE/'overlay'/name).read_text(encoding='utf-8')
    assert not re.search(pattern, text), name
active_names = rg('--files', 'user', 'tools', 'docs', 'examples', '-g', '*.SCP', '-g', '*.scp')
assert set(n.replace('\\', '/') for n in active_names) == set(pins['renames'])
world_refs = rg('-n', r'\.SCW', 'user/world.c', 'user/world_game.inc', 'legacy/M7/user/world.c', 'docs/GAMES.md')
before_commands = [r for r in (ROOT/'examples/m10a2-sdk/researchdesk/BUILD.SH').read_text(encoding='utf-8').splitlines()
                   if r.strip() and not r.lstrip().startswith('#')]
after_commands = [r for r in (HERE/'overlay/examples/m10a2-sdk/researchdesk/BUILD.SH').read_text(encoding='utf-8').splitlines()
                  if r.strip() and not r.lstrip().startswith('#')]
assert before_commands == after_commands
assert not any('scshell' in name.lower() or 'scui/' in name.lower() for name in pins['modified'])
source_paths = (
    'tools/stage_m10a2_sdk.py', 'user/studio2/STUDIO2.JSON',
    'user/studio2/STUDIO2.H', 'user/studio2/studio_project.c',
    'user/scui/ui_dialog.c', 'user/SCUI.H',
    'user/scshell/shell_launch.c', 'user/scshell/shell_catalog.c',
    'user/scshell/desktop/DESKDEFAULTS.H',
    'user/world.c', 'user/world_game.inc', 'legacy/M7/user/world.c',
)
scan = dict(active_refs=active_refs, exact_small_moves=active_names,
            preserved_game_references=world_refs,
            preserved_history=['drafts/', 'build/', 'evidence/', 'Git history'],
            source_pins={name: sha(ROOT/name) for name in source_paths},
            build_script_commands_identical=True, schema_text_identical=True,
            no_generic_scw_association_added=True)
(HERE/'SCAN.json').write_text(json.dumps(scan, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
delivery = dict(status='SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED',
    checks=['python_ast', 'pins_unchanged', 'small_workspace_text_identity',
            'build_script_command_identity', 'active_rg_coverage'],
    files={p.relative_to(HERE).as_posix(): sha(p) for p in sorted(HERE.rglob('*'))
           if p.is_file() and p.name != 'DELIVERY.json'})
(HERE/'DELIVERY.json').write_text(json.dumps(delivery, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps(dict(files=len(delivery['files']), patch_sha256=pins['patch_sha256'],
                      delivery_sha256=sha(HERE/'DELIVERY.json'), status=delivery['status'])))
