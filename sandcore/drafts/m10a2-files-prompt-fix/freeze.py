"""仅核对本交付及只读依据；不编译、不应用补丁。"""
from pathlib import Path
import ast
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


pins = json.loads((HERE / 'PINS.json').read_text(encoding='utf-8'))
for rel, value in pins['modified'].items():
    assert digest(ROOT / rel) == value['before_sha256'], rel
    assert digest(HERE / 'overlay' / rel) == value['after_sha256'], rel
for rel, value in pins['added'].items():
    assert not (ROOT / rel).exists(), rel
    assert digest(HERE / 'overlay' / rel) == value['after_sha256'], rel
assert digest(HERE / 'files-name-prompt.patch') == pins['patch_sha256']
for script in HERE.glob('*.py'):
    ast.parse(script.read_text(encoding='utf-8'))
sources = (
    'user/SCUI.H', 'user/scui/ui_dialog.c', 'user/scui/ui_tree.c',
    'user/scui/ui_dispatch.c', 'user/scshell/shell_dialog.c',
    'user/files2/files2_actions.inc',
    'build/m10a2-p6-gui-run-01/p6-native-files-mkdir-dialog.png',
)
source_pins = {rel: {'sha256': digest(ROOT / rel), 'bytes': (ROOT / rel).stat().st_size}
               for rel in sources}
(HERE / 'SOURCE-PINS.json').write_text(json.dumps(source_pins, indent=2) + '\n', encoding='utf-8')
manifest = json.loads((HERE / 'overlay/user/scshell/SCSHELL.json').read_text(encoding='utf-8'))
assert len(manifest['exports']) == 96
names = [v['name'] for v in manifest['imports']]
assert len(names) == len(set(names)) == 165
assert {'ScUiMetricsQuery', 'ScUiEditSelectAll'} <= set(names)
assert not {'ScUiPropertySheet', 'ScUiDialogCopyValues'} & set(names)
delivery = {
    'status': 'SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED',
    'checks': ['before_image_sha_unchanged', 'patch_and_overlay_sha', 'python_ast',
               'manifest_json_and_import_delta_only'],
    'files': {str(p.relative_to(HERE)).replace('\\', '/'): digest(p)
              for p in sorted(HERE.rglob('*')) if p.is_file() and p.name != 'DELIVERY.json'},
}
(HERE / 'DELIVERY.json').write_text(json.dumps(delivery, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'files': len(delivery['files']), 'delivery_sha256': digest(HERE / 'DELIVERY.json'),
                  'patch_sha256': pins['patch_sha256'], 'status': delivery['status']}))
