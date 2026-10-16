"""只读SCUI前像，生成共享按钮栏的最窄局部补丁；不应用、不构建、不运行。"""
from pathlib import Path
import ast
import difflib
import hashlib
import json

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def save(name, value):
    (HERE / name).write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + '\n').encode('utf-8'))


def main():
    names = ('user/scui/ui_dialog.c', 'docs/SCUI.md')
    raw = {name: (ROOT / name).read_bytes() for name in names}
    before = {name: value.decode('utf-8').replace('\r\n', '\n') for name, value in raw.items()}
    result = dict(before)
    anchor = '''    ui_dialog_layout(d,bar,SC_UI_HBOX,SC_UI_AUTO,30,0,0);
    UiNode *host=ui_node(d->context,bar);
    host->layout.align_h=SC_UI_END_ALIGN;'''
    replacement = '''    /* 固定单行会裁掉较长的后续按钮。AUTO将真实换行高度传回VBox，
     * 单行仍留原30逻辑像素，不靠缩字号或压缩按钮掩盖宽度不足。 */
    ui_dialog_layout(d,bar,SC_UI_FLOW,SC_UI_AUTO,SC_UI_AUTO,0,0);
    UiNode *host=ui_node(d->context,bar);
    host->layout.min_height=30;
    host->layout.align_h=SC_UI_END_ALIGN;'''
    assert result[names[0]].count(anchor) == 1
    result[names[0]] = result[names[0]].replace(anchor, replacement, 1)
    # 按钮构造及其命令校验后半段完整相同，不改default/cancel/字体/行为。
    tail = '    ScUiMenuItem fallback[2];'
    assert before[names[0]][before[names[0]].index(tail):] == result[names[0]][result[names[0]].index(tail):]
    result[names[1]] += '''
2026-10-16：共享Dialog按钮栏使用现有FLOW与AUTO高度，保留30逻辑像素单行下限；自然宽度、28高按钮、gap6、command/default/cancel、字体和事件不变。宽度足够的2/3按钮仍同排，后续按钮不足宽时自然换行，真实行高在两遍候选布局中传回父VBox，原flex正文让出相应空间；不缩字号或修改应用标签。MessageBox420×170的本次四按钮场景可由原父尺寸容纳。本组隔离源码未构建、未客体验证。
'''
    old_pins = HERE / 'PINS.json'
    if old_pins.exists():
        for name, record in json.loads(old_pins.read_text(encoding='utf-8'))['modified'].items():
            assert sha(raw[name]) == record['before_sha256'], name
    pins = {'status': 'SOURCE_ONLY_NOT_BUILT_NOT_GUEST_VERIFIED', 'modified': {}}
    patch = []
    for name in names:
        body = result[name].encode('utf-8')
        path = HERE / 'overlay' / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(body)
        pins['modified'][name] = {'before_sha256': sha(raw[name]), 'result_sha256': sha(body),
                                  'before_bytes': len(raw[name]), 'result_bytes': len(body)}
        patch.extend(difflib.unified_diff(before[name].splitlines(True), result[name].splitlines(True),
                                          fromfile='a/' + name, tofile='b/' + name, n=3))
    patch = ''.join(patch).encode('utf-8')
    (HERE / 'dialog-button-flow.patch').write_bytes(patch)
    pins['patch_sha256'] = sha(patch)
    save('PINS.json', pins)
    references = {}
    for name in ('user/scui/ui_layout.c', 'user/scui/scui_layout_helpers.inc', 'user/scui/ui_controls.c',
                 'user/scui/ui_tree.c', 'user/SCUI.H', 'user/scui/SCUI.json',
                 'build/m10a2-p6-gui-run-03/p6-gui3-desktop-drop-choice.png'):
        body = (ROOT / name).read_bytes()
        references[name] = {'sha256': sha(body), 'bytes': len(body), 'unchanged': True}
    save('SOURCE-PINS.json', references)
    assert all((ROOT / name).read_bytes() == body for name, body in raw.items())
    ast.parse(Path(__file__).read_text(encoding='utf-8'))
    inventory = {}
    for path in sorted(HERE.rglob('*')):
        if path.is_file() and path.name != 'DELIVERY.json':
            data = path.read_bytes()
            inventory[path.relative_to(HERE).as_posix()] = {'sha256': sha(data), 'bytes': len(data)}
    save('DELIVERY.json', {'status': pins['status'], 'files': inventory})
    print(json.dumps({'patch_sha256': sha(patch), 'delivery_sha256': sha((HERE / 'DELIVERY.json').read_bytes()),
                      'result_source_sha256': pins['modified'][names[0]]['result_sha256'], 'status': pins['status']}))


if __name__ == '__main__':
    main()
