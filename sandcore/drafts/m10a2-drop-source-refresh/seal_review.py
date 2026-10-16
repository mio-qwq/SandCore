"""仅静态审阅并封签本目录，不编译、不应用补丁、不改主树。"""
from pathlib import Path
import ast
import ctypes
import difflib
import hashlib
import json
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def lf(data):
    return data.decode('utf-8').replace('\r\n', '\n')


def save(name, value):
    (HERE / name).write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))


def rebuilt(section, original):
    lines = original.splitlines(True)
    output = []
    position = 0
    cursor = 2
    while cursor < len(section):
        header = re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@\n', section[cursor])
        assert header, section[cursor]
        start = int(header[1]) - (int(header[1]) != 0)
        assert start >= position
        output.extend(lines[position:start])
        position = start
        old_count = new_count = 0
        cursor += 1
        while cursor < len(section) and not section[cursor].startswith('@@ '):
            row = section[cursor]
            assert row[:1] in (' ', '+', '-'), row[:50]
            if row[:1] in (' ', '-'):
                assert position < len(lines) and lines[position] == row[1:], (position, row)
                position += 1
                old_count += 1
            if row[:1] in (' ', '+'):
                output.append(row[1:])
                new_count += 1
            cursor += 1
        assert old_count == int(header[2] or 1)
        assert new_count == int(header[4] or 1)
    output.extend(lines[position:])
    return ''.join(output)


def lexical(text, name):
    # 只作词法边界核查，不冒充C语法/类型/链接检查。
    token = re.compile(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
    plain = token.sub('', text)
    stack = []
    pairs = {')': '(', ']': '[', '}': '{'}
    for character in plain:
        if character in '([{':
            stack.append(character)
        elif character in ')]}':
            assert stack and stack.pop() == pairs[character], name
    assert not stack, name
    assert not re.search(r'\b(?:int|__asm|asm)\s+(?:0x80|volatile)', plain), name


def main():
    pins = json.loads((HERE / 'PINS.json').read_text(encoding='utf-8'))
    patch = (HERE / 'drop-source-refresh.patch').read_bytes()
    assert sha(patch) == pins['patch_sha256']
    sections = {}
    active = None
    for row in patch.decode('utf-8').splitlines(True):
        if row.startswith('--- '):
            active = [row]
        elif row.startswith('+++ '):
            assert active is not None
            active.append(row)
            name = row[6:].rstrip('\n')
            assert name not in sections
            sections[name] = active
        else:
            assert active is not None
            active.append(row)
    assert set(sections) == set(pins['modified']) | set(pins['added'])
    for name, record in {**pins['modified'], **pins['added']}.items():
        body = (HERE / 'overlay' / name).read_bytes()
        assert sha(body) == record['result_sha256'] and len(body) == record['result_bytes'], name
        original = ''
        if name in pins['modified']:
            source = (ROOT / name).read_bytes()
            assert sha(source) == record['before_sha256'], name
            original = lf(source)
        else:
            assert not (ROOT / name).exists(), name
        assert rebuilt(sections[name], original).encode('utf-8') == body, name
        if Path(name).suffix.lower() in ('.c', '.h', '.inc'):
            lexical(body.decode('utf-8'), name)
    for name in ('make_patch.py', 'seal_review.py'):
        ast.parse((HERE / name).read_text(encoding='utf-8'))
    library_counts = {}
    for name, count, appended in (
        ('user/scui/SCUI.json', 170, ['ScUiDropDefer', 'ScUiDropQuery', 'ScUiDropReply', 'ScUiDropRelease']),
        ('user/scshell/SCSHELL.json', 96, ['ScShellDragBeginEx', 'ScShellDropFilesDeferred', 'ScShellDropReply']),
    ):
        before = json.loads((ROOT / name).read_text(encoding='utf-8'))
        after = json.loads((HERE / 'overlay' / name).read_text(encoding='utf-8'))
        assert len(before['exports']) == count
        assert after['exports'][:count] == before['exports']
        assert [entry['name'] for entry in after['exports'][count:]] == appended
        assert len(after['imports']) == len({entry['name'] for entry in after['imports']})
        library_counts[after['name']] = len(after['exports'])
    for name in ('user/scui/SCUI.json', 'user/scshell/SCSHELL.json', 'user/files2/FILES2.json', 'user/scshell/desktop/SCSHELL.json'):
        manifest = json.loads((HERE / 'overlay' / name).read_text(encoding='utf-8'))
        assert len(manifest['imports']) == len({entry['name'] for entry in manifest['imports']})
    old_headers = ('user/SCUI.H', 'user/SCSHELL.H', 'user/SC32DROP.H', 'kernel/sc32_drop.c')
    stable = {}
    for name in old_headers:
        assert name not in pins['modified'] and name not in pins['added']
        stable[name] = {'sha256': sha((ROOT / name).read_bytes()), 'unchanged': True}
    # SC32指针字段明确按4字节计算，不借宿主64位指针宣称ABI尺寸。
    u32 = ctypes.c_uint32
    class Identity(ctypes.LittleEndianStructure):
        _fields_ = [('word', u32 * 12)]
    class Drag(ctypes.LittleEndianStructure):
        _fields_ = [('word', u32 * 7), ('data32', u32), ('sender', Identity)]
    class Result(ctypes.LittleEndianStructure):
        _fields_ = [('word', u32 * 14)]
    class Info(ctypes.LittleEndianStructure):
        _fields_ = [('header', u32 * 4), ('transfer', Drag), ('result', Result)]
    assert ctypes.sizeof(Identity) == 48 and ctypes.sizeof(Drag) == 80
    assert ctypes.sizeof(Result) == 56 and ctypes.sizeof(Info) == 152
    clip = 'user/scshell/shell_clipboard.c'
    old = lf((ROOT / clip).read_bytes())
    new = (HERE / 'overlay' / clip).read_text(encoding='utf-8')
    old_marker, new_marker = 'ScStatus ScShellDragBegin(', 'static ScStatus sh_drag_begin('
    start, finish = old.index(old_marker), new.index(new_marker)
    assert old[:start] == new[:finish]
    lower_patch = ''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True), fromfile='a/' + clip, tofile='b/' + clip, n=3)).encode('utf-8')
    (HERE / 'shell-clipboard-lower.patch').write_bytes(lower_patch)
    save('HUNK-PINS.json', {
        'path': clip, 'normalization': 'UTF8_LF',
        'before_marker': old_marker, 'result_marker': new_marker,
        'before_line': old[:start].count('\n') + 1,
        'untouched_prefix_sha256': sha(old[:start].encode('utf-8')),
        'before_lower_sha256': sha(old[start:].encode('utf-8')),
        'result_lower_sha256': sha(new[finish:].encode('utf-8')),
        'lower_patch_sha256': sha(lower_patch),
        'merge': 'Apply lower hunks only; preserve independent TEXT1 top changes; record combined real digest.'
    })
    evidence = {}
    for leaf in ('drop-journal-audit.json', 'drop-cancel-audit.json', 'desktop-drop-copy-journal.rec', 'desktop-drop-move-journal.rec',
                 'p6-desktop-after-move-refresh.png', 'p6-desktop-manual-refresh.png'):
        name = 'build/m10a2-p6-gui-run-02/' + leaf
        body = (ROOT / name).read_bytes()
        evidence[name] = {'sha256': sha(body), 'bytes': len(body), 'scope': 'PRE_FIX_ORIGINAL_EVIDENCE_ONLY'}
    save('SOURCE-PINS.json', {'unchanged': stable, 'evidence': evidence})
    save('STATIC-REVIEW.json', {
        'status': 'PASS_STATIC_ONLY', 'built': False, 'guest_verified': False,
        'checks': ['29 patch sections reconstruct exact overlays', '22 main preimages unchanged', '7 new paths absent from main',
                   'old 170/96 export prefixes preserved', 'new manifest imports unique', 'old public headers and kernel untouched',
                   '32-bit record size arithmetic 16/56/80/152', 'TEXT1 prefix unchanged and lower patch isolated',
                   'C lexical delimiter balance only', 'Python AST only'],
        'exports': library_counts,
        'limits': ['No C parser/type/link check', 'No SCCC/SCO/native manifest regeneration', 'No guest or ABI runtime validation']
    })
    inventory = {}
    for path in sorted(HERE.rglob('*')):
        if path.is_file() and path.name != 'DELIVERY.json':
            assert '__pycache__' not in path.parts
            body = path.read_bytes()
            inventory[path.relative_to(HERE).as_posix()] = {'sha256': sha(body), 'bytes': len(body)}
    delivery = {'status': pins['status'], 'modified': len(pins['modified']), 'added': len(pins['added']),
                'patch_sha256': pins['patch_sha256'], 'files': inventory}
    save('DELIVERY.json', delivery)
    print(json.dumps({'status': 'SEALED_SOURCE_ONLY', 'patch_sha256': pins['patch_sha256'],
                      'delivery_sha256': sha((HERE / 'DELIVERY.json').read_bytes()), 'exports': library_counts}))


if __name__ == '__main__':
    main()
