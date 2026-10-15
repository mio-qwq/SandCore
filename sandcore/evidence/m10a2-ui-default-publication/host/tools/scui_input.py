"""Windows QEMU真实PS/2输入及应用公开查询日志；不合成GUI消息。"""
import re
import time

from m10_guest_boot import physical_word


class ScuiInput:
    def __init__(self, vm, guest, symbols, record):
        self.vm, self.guest, self.symbols, self.record = vm, guest, symbols, record

    def key(self, value):
        self.vm.hmp('sendkey ' + value)
        self.record(dict(kind='HMP_SENDKEY', value=value, seconds=time.monotonic()))
        self.guest.stop.wait(.16)

    def move(self, x, y):
        if x < 0 or y < 0:
            raise ValueError('屏幕坐标不能为负')
        started = time.monotonic()
        for _ in range(48):
            current = (physical_word(self.vm, self.symbols['mx']), physical_word(self.vm, self.symbols['my']))
            if current == (x, y):
                self.record(dict(kind='PS2_MOVE', x=x, y=y, wall_seconds=time.monotonic() - started))
                return
            dx = max(-120, min(120, x - current[0]))
            dy = max(-120, min(120, y - current[1]))
            self.vm.qmp('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}]})
            self.guest.stop.wait(.06)
        raise TimeoutError('真实PS/2光标未到指定屏幕位置')

    def button(self, which, down):
        self.vm.qmp('input-send-event', {'events': [{'type': 'btn', 'data': {'button': which, 'down': down}}]})
        self.record(dict(kind='PS2_BUTTON', button=which, down=down, seconds=time.monotonic()))
        self.guest.stop.wait(.09)

    def click(self, x, y, which='left'):
        self.move(x, y)
        self.button(which, True)
        self.button(which, False)

    def wheel(self, direction='wheel-down'):
        self.button(direction, True)
        self.button(direction, False)

    def snapshot(self, timeout=15):
        start = len(self.guest.console)
        deadline = time.monotonic() + timeout
        # Popup可在同一消息批内建立，业务根观察者要在Step返回后才登记。
        # 仅诊断键有限重发以唤醒该登记点，功能操作始终只发送一次。
        for attempt in range(1, 4):
            self.record(dict(kind='SNAPSHOT_F12_REQUEST', attempt=attempt,
                seconds=time.monotonic(), deadline=deadline))
            self.key('f12')
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError('真实F12诊断超过总等待期限')
            try:
                end = self.guest.wait(rb'(?:\r?\n)SCUI SNAPSHOT END count=([0-9]+)\r?\n',
                    start, remaining / (4 - attempt))
            except TimeoutError:
                if attempt == 3:
                    raise
                continue
            blob = bytes(self.guest.console[start:end.end()])
            value = parse_snapshot(blob)
            self.record(dict(kind='SNAPSHOT_F12_COMPLETE', attempt=attempt,
                count=value['count'], seconds=time.monotonic()))
            return value


def parse_snapshot(blob):
    """只解释应用查询出的值；命令回显和未闭合截断记录不是snapshot。"""
    begin = re.search(rb'(?:^|\r?\n)SCUI SNAPSHOT BEGIN count=([0-9]+)\r?\n', blob)
    end = re.search(rb'(?:^|\r?\n)SCUI SNAPSHOT END count=([0-9]+)\r?\n', blob)
    if not begin or not end or begin[1] != end[1] or end.start() < begin.end():
        raise ValueError('真实snapshot边界不完整或编号不一致')
    result = dict(count=int(begin[1]), windows={}, nodes={}, records=[], raw=blob.decode('utf-8', 'replace'))
    for row in blob[begin.end():end.start()].decode('utf-8', 'strict').splitlines():
        if not row.startswith('SCUI '):
            continue
        fields = row.split()
        values = {}
        for item in fields[2:]:
            if '=' not in item:
                continue
            key, value = item.split('=', 1)
            # textHex是字节串编码；全数字也必须保留前导零和字符串类型。
            values[key] = int(value) if key != 'textHex' and re.fullmatch(r'-?[0-9]+', value) else value
        kind = fields[1]
        # NODE 的公开 kind 是控件枚举；记录类型另存，避免覆盖真实节点字段。
        record = dict(values)
        if 'kind' in record:
            record['node_kind'] = record.pop('kind')
        record['kind'] = kind
        result['records'].append(record)
        if kind in ('WINDOW', 'NODE'):
            role = values.get('role')
            if not isinstance(role, str) or role in result['windows' if kind == 'WINDOW' else 'nodes']:
                raise ValueError('窗口/节点角色缺失或重复')
            required = ('window', 'generation', 'pid', 'session', 'x', 'y', 'w', 'h', 'cw', 'ch', 'state') if kind == 'WINDOW' else ('id', 'root', 'x', 'y', 'w', 'h', 'state')
            if any(not isinstance(values.get(key), int) for key in required):
                raise ValueError('窗口/节点查询字段不完整')
            result['windows' if kind == 'WINDOW' else 'nodes'][role] = values
    if not {'main', 'second'} <= set(result['windows']):
        raise ValueError('缺两个真实根窗口')
    return result


def screen_point(snapshot, role, dx=None, dy=None):
    node = snapshot['nodes'][role]
    windows = [value for value in snapshot['windows'].values() if value.get('root') == node['root']]
    if len(windows) != 1:
        raise ValueError('节点没有唯一真实root窗口')
    window = windows[0]
    border = (window['w'] - window['cw']) // 2
    title = window['h'] - window['ch'] - border
    px = node['w'] // 2 if dx is None else dx
    py = node['h'] // 2 if dy is None else dy
    if not (0 <= px < node['w'] and 0 <= py < node['h']):
        raise ValueError('操作点不在实际节点矩形内')
    return window['x'] + border + node['x'] + px, window['y'] + title + node['y'] + py
