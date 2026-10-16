"""复用已冻结底座；只增加显式标签与本轮真实公开头映射。"""
from contextlib import contextmanager
from pathlib import Path
import copy
import hashlib
import importlib.util
import re
import sys

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BASE = HERE.parent / 'm10a2-p6-followup-refresh'
BASE_PINS = {
    'followup.py': '9f6e7497d11a0edc5d5b3485c68e873191c13b22941ada3e1723443bed956e60',
    'driver.py': '0c3517e8717f09e15306842d2dc2d87f877aea289adad09fd5fda86c9dab0cf2',
    'PINS.json': '41fa8f71fc1ce4040dea231c2fc5efa25cc60e92422cf835636aa31e89565f8a',
    'READ-ONLY-CHECK.json': '52d99d5ab1e2455651223a291f6e7434dbbe72230a73273889e93c15ffb837cb',
}
for name, expected in BASE_PINS.items():
    if hashlib.sha256((BASE / name).read_bytes()).hexdigest() != expected:
        raise ValueError('已审旧wrapper的固定来源不同：' + name)
_SPEC = importlib.util.spec_from_file_location('frozen_followup_basis', BASE / 'followup.py')
_BASE = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_BASE)
CORE, HOST07, LAYOUT = _BASE.CORE, _BASE.HOST07, _BASE.LAYOUT
PREVIOUS, PREVIOUS_SHA, SDK, WITNESS = _BASE.PREVIOUS, _BASE.PREVIOUS_SHA, _BASE.SDK, _BASE.WITNESS
inside, read, sha = _BASE.inside, _BASE.read, _BASE.sha


def validated_labels(labels, known):
    if not labels:
        raise ValueError('必须显式重复--label给出本轮Root批准的完整标签集合')
    if any(not isinstance(label, str) or not re.fullmatch(r'[A-Z0-9_]+', label) for label in labels):
        raise ValueError('标签必须是实际CASES中的完整大写名称')
    if len(set(labels)) != len(labels):
        raise ValueError('重复--label不接受重复标签')
    if set(labels) & {'COMPILER', 'SCCC', 'SCCC1', 'SCLD', 'SCLD0'}:
        raise ValueError('本wrapper不刷新compiler或SCLD')
    unknown = set(labels) - set(known)
    if unknown:
        raise ValueError('标签不在实际当前CASES中：' + ','.join(sorted(unknown)))
    return list(labels)


def exact_changed_labels(labels, changed):
    if len(set(changed)) != len(changed) or set(changed) != set(labels):
        raise ValueError('显式标签与实际闭包变化必须精确相等：requested=' + repr(labels) + ', actual=' + repr(changed))


def prepared_header_map(prepared):
    mapping = prepared['public_header_sources']
    if not isinstance(mapping, dict) or not mapping:
        raise ValueError('本轮prepared缺真实公开头映射')
    for guest, relative in mapping.items():
        if (not isinstance(guest, str) or not guest.startswith('SYS/INC/')
                or '\\' in guest or any(part in ('', '.', '..') for part in guest.split('/'))
                or not isinstance(relative, str) or '\\' in relative
                or any(part in ('', '.', '..') for part in relative.split('/'))):
            raise ValueError('公开头映射不是规范源路径')
        path = inside(ROOT / relative)
        if path.relative_to(ROOT).as_posix() != relative:
            raise ValueError('公开头来源不是仓库规范相对路径')
        expected = prepared['source_input_sha256'].get(relative)
        if expected is None or sha(path) != expected:
            raise ValueError('公开头不属于本轮真实prepared源码快照：' + relative)
    return dict(mapping)


class Basis(_BASE.Basis):
    def __init__(self):
        super().__init__()
        for name, expected in BASE_PINS.items():
            self.pin(BASE / name, expected)

    @contextmanager
    def bind_generation(self, target, prepared):
        mapping = prepared_header_map(prepared)
        old_mapping = copy.deepcopy(self.old['preflight']['public_header_sources'])
        original = target.inherit

        def inherit(current, old, out):
            if current is not prepared or old is not self.old:
                raise ValueError('本轮公开头接线不接受其它prepared/previous')
            if prepared_header_map(current) != mapping:
                raise ValueError('本轮公开头映射在生成前发生变化')
            result = original(current, old, out)
            if result.get('source_snapshot_scope') != target.SCOPE:
                raise ValueError('新报告不是当前准备快照合同')
            # inherit的新preflight原本沿用旧map。这里单独绑定真实prepared，
            # 使新鲜SCO的SYS/INC来源在下一次复用时仍指向它实际编译的原文。
            result['preflight']['public_header_sources'] = dict(mapping)
            result['source_reuse']['current_public_header_sources'] = dict(
                origin='ACTUAL_CURRENT_PREPARED_MAP', entries=len(mapping),
                previous_map_retained_in_previous_report=True,
                runtime_observer_layout_origin='FROZEN_FINAL02_LAYOUT95_NOT_NEW_SCUI_HEADERS')
            if self.old['preflight']['public_header_sources'] != old_mapping:
                raise ValueError('禁止修改历史公开头映射')
            target.save(Path(out) / 'p6-generation.json', result)
            return result

        with super().bind_sidecars(target):
            target.inherit = inherit
            try:
                yield
            finally:
                target.inherit = original
                if self.old['preflight']['public_header_sources'] != old_mapping:
                    raise ValueError('历史公开头映射被修改')
