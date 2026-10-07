#!/usr/bin/env python3
"""真实双来源盘的Shell stdin回归；与网络传输用例分别记录。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import traceback

from m10_guest_boot import unique_symbols, wait_first_desktop
from m10_failure_snapshot import close_guest, failure_snapshot
from m10_verification_report import checkpoint
from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm


# 原始字节由管理串口上传，避免用待测Shell的转义生成它自己的输入。
# 子命令/read消费stdin的两项专门检查逐行解析不能提前读走后续数据。
CASES = (
    ('empty-EOF', b'', b'', 0),
    ('no-final-LF', b'printf M10-END', b'M10-END', 0),
    ('exit-before-EOF', b"printf 'M10-FIRST\\n'\nexit 7\necho BAD\n", b'M10-FIRST\n', 7),
    ('multiline-quote', b"printf '%s\\n' 'first\nsecond'\n", b'first\nsecond\n', 0),
    ('compound-for', b'for x in A B; do\nprintf "%s\\n" "$x"\ndone\n', b'A\nB\n', 0),
    ('heredoc', b'cat <<EOF\nM10 HERE\nEOF\n', b'M10 HERE\n', 0),
    ('CRLF', b'echo M10-CRLF\r\n', b'M10-CRLF\n', 0),
    ('tab-separators', b'\techo\tM10-TAB\n', b'M10-TAB\n', 0),
    ('read-keeps-next-line', b'read value\nM10 READ\nprintf "%s\\n" "$value"\n', b'M10 READ\n', 0),
    ('child-keeps-unread-input', b'cat\nM10-CHILD\n', b'M10-CHILD\n', 0),
    ('unfinished-quote-at-EOF', b"echo 'unfinished", b'', 2),
    ('NUL-rejected', b'echo BAD\x00\n', b'', 1),
    ('control-rejected', b'echo BAD\x01\n', b'', 1),
    ('exit-does-not-read-invalid-tail', b'exit 3\n\x00BAD\n', b'', 3),
)


def run_disk(boot, data, symbols, out):
    report = dict(status='RUNNING', scope='DECLARED_SHELL_STDIN_CASES_ONLY',
                  source=str(data.resolve()), source_sha256=sha(data), cases=[], screenshots=[])
    with QemuSession(boot, data, out, 'tcg', True, audio=False, network='none', memory=256) as vm:
        guest = Guest(vm)
        try:
            guest.wait(rb'CORE START SYS/CORE/CORE.SKM\r\n', timeout=60, debug=True)
            wait_first_desktop(guest, symbols, report)
            guest.connect()

            def case(name, script, expected, expected_code, body=None, interactive=False):
                if body is not None:
                    guest.put_bytes(body, '/TMP/SHINPUT', 'shell-input-'+name)
                code, output, transcript, wall, cpu = guest.run(script+' 2> /TMP/SHERR')
                error = guest.get_bytes('/TMP/SHERR', 'shell-stderr-'+name)
                okay = code == expected_code and (expected in output if interactive else output == expected)
                if interactive:
                    okay = okay and b'SandShell M9\n' in output
                elif expected_code in (1, 2):
                    okay = okay and b'sh: ' in error
                else:
                    okay = okay and error == b''
                item = dict(name=name, status='PASS' if okay else 'FAIL', script=script,
                            input_sha256=hashlib.sha256(body).hexdigest() if body is not None else None,
                            exit_code=code, expected_exit=expected_code, stdout_hex=output.hex(),
                            expected_hex=expected.hex(), stderr_hex=error.hex(), wall_seconds=wall,
                            qemu_cpu_seconds=cpu, transcript=transcript.decode('utf-8', 'replace'))
                report['cases'].append(item)
                checkpoint(vm.out/'shell.json', report)
                print(name+(': PASS' if okay else ': FAIL'), flush=True)
                if not okay:
                    raise AssertionError(name)

            for name, body, output, code in CASES:
                case('file-'+name, 'sh < /TMP/SHINPUT', output, code, body)
                case('pipe-'+name, 'cat /TMP/SHINPUT | sh', output, code, body)
            case('old-script-file', 'sh /TMP/SHINPUT', b'M10-OLD\n', 0, b'echo M10-OLD\n')
            case('old-command-c', "sh -c 'echo M10-C; exit 9'", b'M10-C\n', 9)
            case('explicit-interactive', 'sh -i < /TMP/SHINPUT', b'M10-I', 6,
                 b'echo M10-I\nexit 6\n', interactive=True)
            vm.hmp('sendkey esc')
            picture = vm.out/'shell-after-regressions.ppm'
            vm.hmp('screendump "'+picture.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(picture))
            report['status'] = 'DECLARED_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__,
                          message=str(error), traceback=traceback.format_exc()))
            failure_snapshot(vm, symbols, report)
            if vm.process.poll() is None:
                picture = vm.out/'shell-failure.ppm'
                try:
                    vm.hmp('screendump "'+picture.as_posix()+'"')
                    report['screenshots'].append(png_from_ppm(picture))
                except Exception as capture_error:
                    report['capture_error'] = str(capture_error)
            raise
        finally:
            try:
                close_guest(guest, report)
            finally:
                report['source_unchanged'] = sha(data) == report['source_sha256']
                checkpoint(vm.out/'shell.json', report)
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
    report = dict(status='RUNNING', scope='DECLARED_SHELL_STDIN_NOT_WHOLE_M10A1', disks=[])
    checkpoint(args.out/'shell-matrix.json', report)
    try:
        symbols = unique_symbols(args.symbols)
        for n, data in enumerate(args.data, 1):
            report['disks'].append(run_disk(args.boot, data, symbols, args.out/f'disk-{n}'))
            checkpoint(args.out/'shell-matrix.json', report)
        report['status'] = 'DECLARED_CASES_PASS'
    except BaseException as error:
        report.update(status='FAIL_OR_INTERRUPTED', error=dict(type=type(error).__name__, message=str(error)))
        raise
    finally:
        checkpoint(args.out/'shell-matrix.json', report)


if __name__ == '__main__':
    main()
