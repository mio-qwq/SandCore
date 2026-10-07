#!/usr/bin/env python3
"""原样Notes客体编译、中文显示与真实HMP编辑；其它GUI合同另验。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import time
import traceback

from m10_guest_boot import unique_symbols, wait_first_desktop, physical_word
from m10_failure_snapshot import close_guest, failure_snapshot
from m10_verification_report import checkpoint
from scserial import QemuSession, sha
from serial_protocol import ManagementResultError
from verify_m9 import Guest, png_from_ppm
from verify_m10_lifecycle import run_case

ROOT = Path(__file__).resolve().parents[1]


def run_disk(boot, data, symbols, out):
    report = dict(status='RUNNING', scope='REAL_NOTES_CHINESE_AND_DECLARED_EDIT_CASES_ONLY',
                  source_sha256=sha(data), cases=[], screenshots=[], source_inputs=[])
    with QemuSession(boot, data, out, 'tcg', True, audio=False, network='none', memory=256) as vm:
        guest = Guest(vm)
        session = original = 0
        try:
            guest.wait(rb'CORE START SYS/CORE/CORE.SKM\r\n', timeout=60, debug=True)
            wait_first_desktop(guest, symbols, report)
            guest.connect()
            for source, name in ((ROOT/'user/note.c', 'M10NOTE.C'),
                                 (ROOT/'tests/m10/M10GUI.C', 'M10GUI.C'),
                                 *((ROOT/'user'/name, name) for name in ('NUI.inc', 'SCMEM.inc', 'GLYPHS.inc'))):
                body = source.read_bytes()
                guest.put_bytes(body, '/TMP/'+name, 'notes-source-'+name)
                report['source_inputs'].append(dict(source=str(source), guest='/TMP/'+name,
                                                   bytes=len(body), sha256=hashlib.sha256(body).hexdigest()))
            run_case(guest, report, 'native-original-Notes', 's3c /TMP/M10NOTE.C /TMP/M10NOTE.SCX', timeout=360)
            run_case(guest, report, 'native-public-GUI-launcher', 's3c /TMP/M10GUI.C /TMP/M10GUI.SCX', timeout=240)
            initial = ('中文注释\nint value = 1;\n/* 隐藏会话不绘制，返回后重画。 */\n'
                       + '\n'.join('/* 中文源码第%d行：窗口与输入独立。 */'%n for n in range(1, 41))+'\n').encode()
            guest.put_bytes(initial, '/TMP/NOTEEDIT.C', 'notes-Chinese-document')
            began_frames = physical_word(vm, symbols['wm_frames'])
            launched = run_case(guest, report, 'normal-mio-Notes-launch', '/TMP/M10GUI.SCX /TMP/M10NOTE.SCX /TMP/NOTEEDIT.C')
            fields = {name.decode():int(value) for name, value in re.findall(rb'^(gui_[a-z]+)=([0-9]+)\r?$',
                                                                          bytes.fromhex(launched['stdout_hex']), re.M)}
            if set(fields) != {'gui_pid', 'gui_window', 'gui_session', 'gui_original'} or not all(fields.values()):
                raise AssertionError('没有取得真实Notes任务/窗口/桌面')
            report['Notes'] = fields
            session, original = fields['gui_session'], fields['gui_original']

            def picture(label):
                path = vm.out/(label+'.ppm')
                vm.hmp('screendump "'+path.as_posix()+'"')
                report['screenshots'].append(dict(label=label, **png_from_ppm(path)))
                checkpoint(vm.out/'notes.json', report)

            # 观察完成帧而不靠固定延时猜首屏。字形是否正确由原生截图
            # 人工审阅；本程序不把“产生了一张PNG”自动当中文画质PASS。
            deadline = time.monotonic()+30
            while physical_word(vm, symbols['wm_frames'])-began_frames < 2:
                if time.monotonic() >= deadline:
                    raise TimeoutError('Notes切换与实际客户帧未完成')
                guest.stop.wait(.05)
            picture('Notes-original-Chinese-source')

            def edit(label, keys, expected):
                for key in keys:
                    vm.hmp('sendkey '+key+' 20')
                deadline = time.monotonic()+30
                actual = b''
                while time.monotonic() < deadline:
                    try:
                        actual = guest.get_bytes('/TMP/NOTEEDIT.C', 'notes-saved-'+label)
                    except ManagementResultError as error:
                        # F2真实COW提交可发生在GET_BEGIN与GET_DATA之间。
                        # 内核-8拒绝混代正文是正确保护；仅此精确代数变化
                        # 重开完整GET，保留失败部分，绝不拼接两代字节。
                        # 原30秒期限与其它协议/权限错误都不放宽。
                        if error.kind!=8 or error.result!=-8:raise
                        report.setdefault('save_snapshot_restarts',[]).append(dict(edit=label,kind=error.kind,result=error.result,
                                                                                  monotonic=time.monotonic()))
                        guest.stop.wait(.05)
                        continue
                    if actual == expected:
                        break
                    guest.stop.wait(.05)
                okay = actual == expected
                report['cases'].append(dict(name=label, status='PASS' if okay else 'FAIL', hmp_keys=keys,
                                            bytes=len(actual), sha256=hashlib.sha256(actual).hexdigest(),
                                            expected_sha256=hashlib.sha256(expected).hexdigest()))
                checkpoint(vm.out/'notes.json', report)
                print(label+(': PASS' if okay else ': FAIL'), flush=True)
                if not okay:
                    raise AssertionError('Notes真实保存正文不符：'+label)
                picture(label)

            changed = initial.decode().replace('中文注释', '中x注释', 1).encode()
            edit('Chinese-whole-character-delete-and-insert', ['right', 'right', 'backspace', 'x', 'f2'], changed)
            # 覆盖路径由测试拥有、原副本保留；F3是Notes真实重载入口。
            mixed = 'Aé中Z\n123456789\n'.encode()
            guest.put_bytes(mixed, '/TMP/NOTEEDIT.C', 'notes-mixed-width-input')
            edit('mixed-half-and-wide-caret-delete', ['f3', 'right', 'right', 'right', 'backspace', 'x', 'f2'],
                 'AéxZ\n123456789\n'.encode())
            guest.put_bytes(mixed, '/TMP/NOTEEDIT.C', 'notes-up-down-input')
            edit('vertical-caret-keeps-font-cell-column', ['f3', 'right', 'right', 'right', 'down', 'backspace', 'f2'],
                 'Aé中Z\n12346789\n'.encode())
            edit('end-and-newline-index', ['f9', 'backspace', 'f2'], 'Aé中Z\n12346789'.encode())
            # 滚动后在实际第35行插入，只有按键被完整消费才会保存
            # 预期字节；不能用已存在的相同正文冒充F2已执行的证据。
            guest.put_bytes(initial, '/TMP/NOTEEDIT.C', 'notes-scroll-input')
            lines = initial.decode().splitlines(keepends=True)
            lines[34] = 'x'+lines[34]
            scrolled = ''.join(lines).encode()
            edit('vertical-scroll-and-exact-insertion-line', ['f3']+['down']*34+['x', 'f2'], scrolled)
            edit('Notes-return-to-document-start', ['f8', 'y', 'f2'], b'y'+scrolled)
            run_case(guest, report, 'Notes-session-logout', 'sessionctl logout '+str(session))
            session = 0
            run_case(guest, report, 'restore-original-mio', 'sessionctl show '+str(original))
            picture('Notes-cleanup-original-desktop')
            report['status'] = 'DECLARED_EDIT_CASES_PASS_SCREENSHOTS_REQUIRE_VISUAL_REVIEW'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                          message=str(error), traceback=traceback.format_exc()))
            failure_snapshot(vm, symbols, report)
            if vm.process.poll() is None:
                try:
                    path = vm.out/'Notes-failure.ppm'
                    vm.hmp('screendump "'+path.as_posix()+'"')
                    report['screenshots'].append(png_from_ppm(path))
                except Exception as capture_error:
                    report['capture_error'] = str(capture_error)
            raise
        finally:
            # 失败现场已取样。退出VM回收其测试副本；不在失败后继续
            # 注入GUI键或把额外清理命令覆盖最初的正文错误。
            try:
                close_guest(guest, report)
            finally:
                report['source_unchanged'] = sha(data) == report['source_sha256']
                checkpoint(vm.out/'notes.json', report)
    if not report['source_unchanged']:
        raise AssertionError('只读来源盘改变')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--boot', type=Path, required=True)
    parser.add_argument('--data', type=Path, action='append', required=True)
    parser.add_argument('--symbols', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if os.name != 'nt' or len(args.data) < 2 or len(set(p.resolve(strict=True) for p in args.data)) != len(args.data):
        parser.error('需要Windows Python与不同的双来源盘')
    args.out.mkdir(parents=True, exist_ok=False)
    report = dict(status='RUNNING', scope='DECLARED_NOTES_EDITS_NOT_WHOLE_FONT_OR_GUI', disks=[],
                  remaining=['visual Chinese source/caret/highlight/scroll review', 'horizontal clipping and pointer selection',
                             'other resolution/theme and bad-font fallback', 'hidden Notes frame and resource ownership'])
    checkpoint(args.out/'notes-matrix.json', report)
    try:
        symbols = unique_symbols(args.symbols)
        for n, data in enumerate(args.data, 1):
            report['disks'].append(run_disk(args.boot, data, symbols, args.out/f'disk-{n}'))
            checkpoint(args.out/'notes-matrix.json', report)
        report['status'] = 'DECLARED_EDIT_CASES_PASS_SCREENSHOTS_REQUIRE_VISUAL_REVIEW'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__, message=str(error)))
        raise
    finally:
        checkpoint(args.out/'notes-matrix.json', report)


if __name__ == '__main__':
    main()
