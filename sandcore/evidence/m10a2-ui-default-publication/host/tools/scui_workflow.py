"""独立Demo的真实有序输入工作流，使用F12公开查询值作判断。"""
import re
import time

from scui_input import screen_point


def record_of(snapshot, kind):
    values = [item for item in snapshot['records'] if item['kind'] == kind]
    if len(values) != 1:
        raise ValueError('缺少唯一真实查询记录：' + kind)
    return values[0]


def document_body(snapshot):
    node = snapshot['nodes']['edit']
    size, encoded = node.get('textBytes'), node.get('textHex')
    if (not isinstance(size, int) or size < 0 or node.get('textStatus') != 0
            or not isinstance(encoded, str)):
        raise ValueError('缺主编辑器的实际完整正文查询')
    if encoded == '-':
        encoded = ''
    if len(encoded) != size * 2 or not re.fullmatch(r'[0-9a-f]*', encoded):
        raise ValueError('主编辑器正文长度/十六进制不一致')
    body = bytes.fromhex(encoded)
    body.decode('utf-8', 'strict')
    if len(body) != record_of(snapshot, 'DOCUMENT')['bytes']:
        raise ValueError('主编辑器正文与Document公开字节数不一致')
    return body


def edit_selection(snapshot):
    values = [item for item in snapshot['records']
              if item['kind'] == 'EDIT_SELECTION' and item.get('role') == 'edit']
    if len(values) != 1:
        raise ValueError('缺主编辑器的唯一真实选区记录')
    result = values[0]
    size = record_of(snapshot, 'DOCUMENT')['bytes']
    if (any(not isinstance(result.get(key), int) for key in ('anchor', 'caret', 'revision'))
            or not 0 <= result['anchor'] <= size or not 0 <= result['caret'] <= size):
        raise ValueError('主编辑器选区不在当前Document范围')
    return result


def work(harness, inputs, snapshots, session):
    def snap(label):
        value = inputs.snapshot()
        value['label'] = label
        snapshots.append(value)
        harness.persist()
        return value

    def check(name, okay, **values):
        harness.append(dict(name=name, **values), bool(okay))

    def published(label, predicate, timeout=20):
        # Tab提交先发布可见状态，布局在ContextStep消息批结束后发布；
        # F12可发生在这两点之间，必须等待实际矩形而不能猜理想点位。
        deadline = time.monotonic() + timeout
        while True:
            value = snap(label)
            if predicate(value):
                return value
            if time.monotonic() >= deadline:
                raise TimeoutError('真实控件状态尚未发布：' + label)
            inputs.guest.stop.wait(.2)

    def arranged(label, role):
        return published(label, lambda value: value['nodes'][role]['state'] & 1
            and value['nodes'][role]['w'] > 0 and value['nodes'][role]['h'] > 0)

    def combo_ready(value):
        # SC32POPUP.H: POPUP1024/NO_ACTIVATE2048；SCUI.H: LIST14。
        # Kernel ref已存在并不等于SCUI已消费VIS与发布逻辑focus。
        for window in value['windows'].values():
            if (window['state'] & (1 | 1024 | 2048)) != (1 | 1024 | 2048):
                continue
            for node in value['nodes'].values():
                if (node['root'] == window['root'] and node.get('kind') == 14
                        and node['state'] & 7 == 7 and node['w'] > 0 and node['h'] > 0
                        and record_of(value, 'CONTEXT')['visible'] >= 3):
                    return True
        return False

    def focus_main(value):
        # 客户区顶端是菜单，点击真实标题栏才不会意外打开 File 弹出菜单。
        window = value['windows']['main']
        border = (window['w'] - window['cw']) // 2
        title = window['h'] - window['ch'] - border
        inputs.click(window['x'] + border + 60, window['y'] + max(2, title // 2))

    initial = snap('initial')
    context = record_of(initial, 'CONTEXT')
    check('native-demo-two-real-visible-roots', context['roots'] == 2 and context['visible'] == 2
        and all(window['session'] == session and window['state'] & 1 for window in initial['windows'].values()),
        context=context, windows=initial['windows'])
    harness.screen('scui-native-initial')

    focus_main(initial)
    inputs.key('f6')
    controls = snap('controls')
    check('three-state-initial-mixed', controls['nodes']['check']['state'] & 64, node=controls['nodes']['check'])
    inputs.click(*screen_point(controls, 'check', dx=10))
    clicked = snap('three-state-click')
    check('real-checkbox-click-cycles-mixed-to-clear', not clicked['nodes']['check']['state'] & (32 | 64), node=clicked['nodes']['check'])
    inputs.key('tab')
    inputs.key('shift-tab')
    inputs.click(*screen_point(clicked, 'combo'))
    published('combo-ready-real-VIS-layout-focus', combo_ready)
    harness.screen('scui-native-combo')
    inputs.key('down')
    published('combo-real-selected-label', lambda value: value['nodes']['combo']['textHex'] == '4175726f7261')
    inputs.key('ret')
    published('combo-closed-before-page-command', lambda value: not value['nodes']['combo']['state'] & 1024
        and record_of(value, 'CONTEXT')['roots'] == 2)

    inputs.key('f7')
    data = arranged('virtual-table', 'table')
    inputs.click(*screen_point(data, 'table', dx=40, dy=55))
    inputs.key('ctrl-a')
    selected = snap('virtual-select-all')
    selection = record_of(selected, 'SELECTION')
    check('virtual-table-selects-full-dataset', selection['count'] == 100000,
        selection=selection, nodes=record_of(selected, 'CONTEXT')['nodes'])
    inputs.wheel('wheel-down')
    harness.screen('scui-native-virtual-table')

    inputs.key('f8')
    editor = arranged('editor', 'edit')
    before = record_of(editor, 'DOCUMENT')
    before_body = document_body(editor)
    inputs.click(*screen_point(editor, 'edit', dx=60, dy=30))
    inputs.key('x')
    typed = snap('real-text')
    inserted = record_of(typed, 'DOCUMENT')
    typed_body = document_body(typed)
    check('real-key-and-text-insert-once', inserted['bytes'] == before['bytes'] + 1,
        before=before, after=inserted)
    inputs.key('ctrl-z')
    undo = snap('undo')
    check('undo-restores-document-bytes', record_of(undo, 'DOCUMENT')['bytes'] == before['bytes']
        and document_body(undo) == before_body,
        before=before, after=record_of(undo, 'DOCUMENT'),
        before_text_hex=before_body.hex(), after_text_hex=document_body(undo).hex())
    inputs.key('ctrl-y')
    redo = snap('redo')
    check('redo-restores-edited-document', record_of(redo, 'DOCUMENT')['bytes'] == inserted['bytes']
        and document_body(redo) == typed_body,
        before=inserted, after=record_of(redo, 'DOCUMENT'),
        before_text_hex=typed_body.hex(), after_text_hex=document_body(redo).hex())
    inputs.key('ctrl-a')
    # 同一步的公开观察证明全选，不让Copy/Paste同时无效仍因长度相等过关。
    copying = snap('clipboard-selected')
    copied_selection = edit_selection(copying)
    copied_document = record_of(copying, 'DOCUMENT')
    inputs.key('ctrl-c')
    inputs.key('ctrl-v')
    paste = snap('clipboard-paste')
    pasted_document, pasted_selection = record_of(paste, 'DOCUMENT'), edit_selection(paste)
    check('real-clipboard-paste-keeps-whole-selection-size',
        copied_selection['anchor'] == 0 and copied_selection['caret'] == copied_document['bytes']
        and document_body(copying) == typed_body
        and pasted_document['bytes'] == inserted['bytes']
        and pasted_document['revision'] == copied_document['revision'] + 1
        and pasted_selection['anchor'] == pasted_selection['caret'] == pasted_document['bytes']
        and pasted_selection['revision'] == pasted_document['revision']
        and document_body(paste) == typed_body,
        before=copied_document, after=pasted_document,
        selection_before=copied_selection, selection_after=pasted_selection,
        before_text_hex=document_body(copying).hex(), after_text_hex=document_body(paste).hex())
    harness.screen('scui-native-editor')

    # 同一SDK数据路径，捕获属于副窗实际Button，点到主窗真实Editor。
    inputs.move(*screen_point(paste, 'drag-source'))
    drag_start = len(inputs.guest.console)
    inputs.button('left', True)
    inputs.move(*screen_point(paste, 'edit', dx=100, dy=55))
    inputs.button('left', False)
    target_commit = inputs.guest.wait(
        rb'(?:\r?\n)SCUI DROP bytes=([0-9]+) sender=([0-9]+)/([0-9]+)\r?\n', drag_start, 20)
    source_complete = inputs.guest.wait(
        rb'(?:\r?\n)SCUI DRAG COMPLETE action=([0-9]+) sender=([0-9]+)/([0-9]+)\r?\n',
        target_commit.end() - 1, 20)
    dropped = snap('real-cross-window-drag')
    # 字面正文与冻结Demo的sizeof(text)合同一致；DROP.bytes包括末尾唯一NUL。
    drag_body = 'Dragged through the real broker / 拖放\n'.encode('utf-8')
    first, last = sorted((pasted_selection['anchor'], pasted_selection['caret']))
    expected_body = typed_body[:first] + drag_body + typed_body[last:]
    source, target = paste['windows']['second'], paste['windows']['main']
    source_identity = (source['pid'], source['pidgen'])
    target_identity = (target['pid'], target['pidgen'])
    committed_identity = (int(target_commit[2]), int(target_commit[3]))
    completed_identity = (int(source_complete[2]), int(source_complete[3]))
    dropped_document = record_of(dropped, 'DOCUMENT')
    # 日志只给PID/gen；SID来自同次公开WindowQuery，不编造日志未提供的字段。
    same_windows = all(
        tuple(dropped['windows'][role][key] for key in ('window', 'generation', 'pid', 'pidgen', 'session'))
        == tuple(paste['windows'][role][key] for key in ('window', 'generation', 'pid', 'pidgen', 'session'))
        for role in ('main', 'second'))
    check('real-drag-IPC-complete-reported',
        int(target_commit[1]) == len(drag_body) + 1 and int(source_complete[1]) == 1
        and committed_identity == source_identity and completed_identity == target_identity
        and source['session'] == target['session'] == session and same_windows
        and dropped_document['bytes'] == pasted_document['bytes'] - (last - first) + len(drag_body)
        and dropped_document['revision'] == pasted_document['revision'] + 1
        and document_body(dropped) == expected_body,
        document_before=pasted_document, document=dropped_document, selection_before=pasted_selection,
        expected_text_hex=expected_body.hex(), actual_text_hex=document_body(dropped).hex(),
        payload_bytes_including_NUL=int(target_commit[1]), action=int(source_complete[1]),
        source_identity=dict(pid=source['pid'], generation=source['pidgen'], session=source['session']),
        target_identity=dict(pid=target['pid'], generation=target['pidgen'], session=target['session']),
        actual_DROP_sender=list(committed_identity), actual_COMPLETE_sender=list(completed_identity),
        SID_observation='PUBLIC_WINDOW_QUERY_BOUND_TO_ACTUAL_LOG_PIDGEN',
        console_offset=drag_start, target_commit=target_commit.group().decode('utf-8'),
        source_complete=source_complete.group().decode('utf-8'))

    focus_main(dropped)
    inputs.key('alt-t')
    harness.screen('scui-native-menu')
    inputs.key('home')
    inputs.key('ret')
    inputs.guest.stop.wait(.3)
    harness.screen('scui-native-properties')
    inputs.key('esc')
    after_dialog = snap('property-cancel')
    check('modal-property-cancel-keeps-two-roots', record_of(after_dialog, 'CONTEXT')['roots'] == 2,
        context=record_of(after_dialog, 'CONTEXT'))
    # 随后资源/布局/主题/DPI与隐藏计量由当前主控按实际菜单和query接续。
    return after_dialog
