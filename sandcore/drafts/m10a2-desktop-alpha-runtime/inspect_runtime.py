"""只读复用上一轮几何与SCB读取器；不写截图、资源、主源或客体。"""
from pathlib import Path
from datetime import datetime, timezone
import collections
import hashlib
import importlib.util
import json
import mmap
import struct
import sys

from PIL import Image

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
PRIOR = ROOT / 'drafts/m10a2-desktop-icon-alpha/inspect_pixels.py'
SPEC = importlib.util.spec_from_file_location('frozen_icon_reader', PRIOR)
READER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(READER)
NEW_SHOT = 'build/m10a2-p6-gui-run-02/p6-new-native-desktop.png'
OLD_SHOT = 'build/m10a2-p6-gui-run-01/p6-native-desktop-initial.png'
REPORT = 'build/m10a2-p6-gui-run-02/p6-gui.json'
UI_SHA = '7059348b04925a4ba5f29702abd0c419a570b05008ad854908f70dae66d640b7'
SHOT_SHA = '9a57a7504873b058462558c4fe17fb41740cdfc3f9f8eeb30bfee1e0cad9455e'
FILES_SHA = '2a6a31f0732886fadaf457b1afba18cb8e6beb4ab37282cd86e6a55772286ea1'


def sha(body):
    return hashlib.sha256(body).hexdigest()


def record_file(path):
    body = (ROOT / path).read_bytes()
    return {'path': path, 'bytes': len(body), 'sha256': sha(body)}


def rgb(value):
    return tuple((value >> shift) & 255 for shift in (16, 8, 0))


def read_wallpaper(path):
    # 客体仍由主代理使用，因此只复制所需文件的只读视图，不固定整盘快照身份。
    with path.open('rb') as stream:
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
            rows = READER.read_image(mapped, False)
            try:
                return bytes(rows['SYS/WALL/AURORA.SCB'].payload)
            finally:
                READER.release_views(rows)


def main():
    original_disk = ROOT / 'build/m10a1/sanddata.img'
    old_disk = ROOT / 'build/m10a2-p6-gui-run-01/sanddata.img'
    new_disk = ROOT / 'build/m10a2-p6-gui-run-02/sanddata.img'
    original = READER.images_from(original_disk)
    old = READER.images_from(old_disk)
    new = READER.images_from(new_disk)
    resources = {}
    for path, body in new.items():
        assert body == old[path] == original[path], path
        resources[path] = {'bytes': len(body), 'sha256': sha(body),
                           'identical_original_gui01_gui02': True}
    body = new['SYS/ICONS/AURORA/FILES.SCB']
    assert sha(body) == FILES_SHA
    assert body[:8] == b'SCB2MIO\0'
    assert struct.unpack_from('<6I', body, 8) == (128, 128, 2, 0, 65536, 0x004f494d)
    assert len(body) == 65568
    icon = struct.unpack_from('<16384I', body, 32)

    wall = read_wallpaper(new_disk)
    assert wall == read_wallpaper(old_disk) == read_wallpaper(original_disk)
    width, height, kind, flags, size, trailer = struct.unpack_from('<6I', wall, 8)
    assert wall[:8] == b'SCB2MIO\0' and kind == 2 and flags == 0
    assert size == width * height * 4 and trailer == 0x004f494d and len(wall) == size + 32
    wallpaper = struct.unpack_from('<%dI' % (width * height), wall, 32)
    assert all(value >> 24 == 255 for value in wallpaper)
    # 按真实desktop_wallpaper的整数cover裁剪，不从待验证截图反推背景颜色。
    crop_w, crop_h = width, height
    if width * 1080 > height * 1920:
        crop_w = height * 1920 // 1080
    else:
        crop_h = width * 1080 // 1920
    left, top = (width - crop_w) // 2, (height - crop_h) // 2

    shots = {}
    shot_records = {}
    for name, path in (('gui01', OLD_SHOT), ('gui02', NEW_SHOT)):
        shot_records[name] = record_file(path)
        image = Image.open(ROOT / path).convert('RGB')
        assert image.size == (1920, 1080)
        shots[name] = image
    assert shot_records['gui02']['sha256'] == SHOT_SHA
    counts = {key: collections.Counter() for key in shots}
    excluded = collections.Counter()
    examples = []
    mismatches = {key: [] for key in shots}
    for y in range(72):
        for x in range(72):
            screen_x, screen_y = 96 + x, 48 + y
            # 角标与鼠标是独立绘制者；两截图采用同一保守排除框，避免事后挑点。
            if x < 13 and y >= 59:
                excluded['shortcut_badge'] += 1
                continue
            if 160 <= screen_x < 176 and 100 <= screen_y < 120:
                excluded['mouse_box_intersection'] += 1
                continue
            source_x, source_y = x * 128 // 72, y * 128 // 72
            source = icon[source_y * 128 + source_x]
            alpha = source >> 24
            source_rgb = rgb(source)
            background = rgb(wallpaper[(top + screen_y * crop_h // 1080) * width
                                       + left + screen_x * crop_w // 1920])
            expected = tuple((a * alpha + b * (255 - alpha) + 127) // 255
                             for a, b in zip(source_rgb, background))
            old_black = tuple((a * alpha + 127) // 255 for a in source_rgb)
            category = 'opaque' if alpha == 255 else 'partial' if alpha else 'transparent'
            for key, image in shots.items():
                observed = image.getpixel((screen_x, screen_y))
                counts[key][category] += 1
                counts[key][category + '_exact_straight_over_wallpaper'] += observed == expected
                if category == 'partial':
                    counts[key]['partial_exact_wrong_black'] += observed == old_black
                if observed != expected and len(mismatches[key]) < 3:
                    mismatches[key].append({'screen': [screen_x, screen_y],
                                            'observed_rgb': observed, 'expected_rgb': expected})
            if (screen_x, screen_y) in ((105, 66), (107, 67), (125, 66)):
                examples.append({'screen': [screen_x, screen_y], 'source': [source_x, source_y],
                                 'source_argb': '%08x' % source, 'wallpaper_rgb': background,
                                 'expected_straight_over_rgb': expected,
                                 'gui01_rgb': shots['gui01'].getpixel((screen_x, screen_y)),
                                 'gui02_rgb': shots['gui02'].getpixel((screen_x, screen_y))})
    for key in shots:
        assert counts[key]['opaque'] == counts[key]['opaque_exact_straight_over_wallpaper'] == 1640
        assert counts[key]['transparent'] == counts[key]['transparent_exact_straight_over_wallpaper'] == 2522
        assert counts[key]['partial'] == 693
    assert counts['gui01']['partial_exact_wrong_black'] == 693
    assert counts['gui01']['partial_exact_straight_over_wallpaper'] == 0
    assert counts['gui02']['partial_exact_straight_over_wallpaper'] == 693
    assert counts['gui02']['partial_exact_wrong_black'] == 0
    assert not mismatches['gui02']

    # 报告由主代理继续追加；只固定本次读取的字节摘要及所用字段，不声称全程不变。
    report_bytes = (ROOT / REPORT).read_bytes()
    report = json.loads(report_bytes.decode('utf-8'))
    baseline = report['baseline']['coherent_snapshot']
    assert baseline['status'] == 'ACTUAL_COHERENT_READ_ONLY_SNAPSHOT'
    module = next(value for value in baseline['debug_modules'] if value['image_sha256'] == UI_SHA)
    assert module['source_bytes'] == 733256 and module['kind'] == 2 and module['phase'] == 2
    owner = next(value for value in baseline['owners'] if value['pid'] == module['owner'])
    owner = {key: owner[key] for key in ('pid', 'generation', 'session', 'name')}
    assert owner == {'pid': 1, 'generation': 1, 'session': 1, 'name': 'SCSHELL.SCX'}
    actions = report['p6_gui_runtime']['actions']
    ordinary = actions['initial-ordinary-bind']['result']
    assert ordinary['uid'] == 1000 and ordinary['session'] == 4 and ordinary['session_row']['active']
    screen_record = actions['desktop-start']['result']
    assert screen_record['sha256'] == SHOT_SHA
    native_report = report['native_report']
    native_path = Path(native_report)
    if not native_path.is_absolute():
        native_path = ROOT / native_path
    native_bytes = native_path.read_bytes()
    assert sha(native_bytes) == report['native_report_sha256']
    native_ui = json.loads(native_bytes.decode('utf-8'))['native_libraries']['SCUI.SCL']
    assert native_ui['sha256'] == UI_SHA and native_ui['bytes'] == 733256

    sources = ('user/scui/ui_paint.c', 'user/scui/ui_argb.c', 'user/scui/SCUIPRIVATE.H',
               'user/scshell/desktop/desktop_view.inc', 'kernel/desktop.c', 'kernel/gfx.c')
    result = {
        'schema': 1, 'status': 'PASS_SCOPED_EXISTING_GUI02_ALPHA_RUNTIME_PIXELS',
        'scope': 'ONE_EXISTING_FILES_ICON_GEOMETRY_NO_NEW_VM_BUILD_OR_ASSET_EDIT',
        'observed_utc': datetime.now(timezone.utc).isoformat(),
        'screenshots': shot_records, 'resources': resources,
        'wallpaper': {'path': 'SYS/WALL/AURORA.SCB', 'bytes': len(wall), 'sha256': sha(wall),
                      'identical_original_gui01_gui02': True, 'width': width, 'height': height,
                      'all_source_pixels_opaque': True, 'cover_crop': [left, top, crop_w, crop_h],
                      'screen_dimensions': [1920, 1080]},
        'geometry': {'screen': [96, 48, 72, 72], 'source': [128, 128],
                     'sampling': 'source_x=floor(x*128/72),source_y=floor(y*128/72)',
                     'shortcut_exclusion_screen': [96, 107, 13, 13],
                     'mouse_exclusion_screen': [160, 100, 16, 20], 'excluded': dict(excluded)},
        'expected_formula_per_rgb_channel': '(source*source_alpha+wallpaper*(255-source_alpha)+127)//255',
        'results': {key: dict(value) for key, value in counts.items()}, 'examples': examples,
        'gui02_mismatches': mismatches['gui02'],
        'runtime_binding': {
            'report_path': REPORT, 'report_read_time_sha256': sha(report_bytes),
            'report_is_live_not_claimed_unchanged': True,
            'baseline_field': 'baseline.coherent_snapshot.debug_modules[6]',
            'loaded_scui_module': module, 'baseline_module_owner': owner,
            'baseline_is_session1_not_ordinary_session4': True,
            'ordinary_screenshot_fields': ['p6_gui_runtime.actions.initial-ordinary-bind.result',
                                           'p6_gui_runtime.actions.desktop-start.result'],
            'ordinary_binding': ordinary, 'screen_record': screen_record,
            'native_report': str(native_path), 'native_report_sha256': sha(native_bytes)},
        'source_sha256': {path: sha((ROOT / path).read_bytes()) for path in sources},
        'reused_checker': record_file('drafts/m10a2-desktop-icon-alpha/inspect_pixels.py'),
        'limitations': ['Only this existing 128-to-72 Files icon is numerically accepted.',
                        'No scaling-quality or performance conclusion.',
                        'Coherent module observation is baseline session1; ordinary screenshot has separate UID1000 SID4 action binding.',
                        'This inspection did not start, pause, resume, or send input to any VM.'],
        'asset_edited': False, 'image_generated': False, 'main_source_edited': False,
        'build_started': False, 'vm_called': False, 'input_sent': False}
    output = HERE / 'PIXEL-EVIDENCE.json'
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': result['status'], 'results': result['results'],
                      'receipt': str(output), 'receipt_sha256': sha(output.read_bytes())}, indent=2))


if __name__ == '__main__':
    main()
