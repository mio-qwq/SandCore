#!/usr/bin/env python3
"""在真实串口Shell批量核对独立CLI行为，逐项保存字节与退出码。

一次传入整批脚本减少UART握手；随机边界只用于拆分原始输出，不用
名字或帮助页充当功能通过。每项隔离工作目录和环境，源盘始终只读。
"""
import argparse
import calendar
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import time

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm

ROOT = Path(__file__).resolve().parents[1]
CASES = []


def case(name, reference, script, expected=None, status=0, pattern=None):
    CASES.append(dict(name=name, reference=reference, script=script,
                      expected=expected, expected_exit=status, pattern=pattern))


def cases():
    # 文本的二进制、空行、UTF-8、排序与错误语义用确定正文逐字节核对。
    case('bracket-true', '[', '[ 3 -gt 2 ]', b'')
    case('bracket-false', '[', '[ 3 -lt 2 ]', b'', 1)
    case('echo-escapes', 'echo', r"echo -e 'a\tb\nc'", b'a\tb\nc\n')
    case('echo-no-newline', 'echo', 'echo -n a b', b'a b')
    case('wc-counts', 'wc', "printf 'a b\nc\n' | wc", b'2 3 6 -\n')
    case('nl-empty-lines', 'nl', "printf 'a\n\nb\n' | nl", b'1\ta\n\t\n2\tb\n')
    case('expand-tabstops', 'expand', "printf 'a\tb\n' | expand -t 4", b'a   b\n')
    case('unexpand-tabs', 'unexpand', "printf '    a    b\n' | unexpand -a -t 4", b'\ta\t b\n')
    case('dos2unix-stream', 'dos2unix', r"printf 'a\r\nb\r\n' | dos2unix", b'a\nb\n')
    case('unix2dos-stream', 'unix2dos', r"printf 'a\nb\n' | unix2dos", b'a\r\nb\r\n')
    case('egrep-alternation', 'egrep', "printf 'cat\ndog\nbird\n' | egrep 'cat|dog'", b'cat\ndog\n')
    case('fgrep-literal', 'fgrep', "printf 'a.b\naxb\n' | fgrep a.b", b'a.b\n')
    case('grep-no-match', 'grep', "printf 'a\n' | grep z", b'', 1)
    case('cmp-identical', 'cmp', 'cmp A.TXT A.TXT', b'')
    case('cmp-different-silent', 'cmp', 'cmp -s A.TXT B.TXT', b'', 1)
    case('comm-columns', 'comm', 'comm A.TXT C.TXT', b'\t\ta\nb\n\t\tc\n')
    case('comm-common-only', 'comm', 'comm -12 A.TXT C.TXT', b'a\nc\n')
    case('diff-same', 'diff', 'diff -s A.TXT A.TXT', b'Files are identical\n')
    case('diff-exit-and-change', 'diff', 'diff A.TXT B.TXT', b'2c2\n< b\n---\n> x\n', 1)
    case('patch-dry-run', 'patch', 'cp A.TXT PATCHED; patch --dry-run PATCHED CHANGE.PATCH; cat PATCHED', b'patch: applicable\na\nb\nc\n')
    case('patch-apply', 'patch', 'cp A.TXT PATCHED; patch PATCHED CHANGE.PATCH; cat PATCHED', b'patch: applied\na\nx\nc\n')
    case('patch-reverse', 'patch', 'cp B.TXT PATCHED; patch -R PATCHED CHANGE.PATCH; cat PATCHED', b'patch: applied\na\nb\nc\n')
    case('tee-binary', 'tee', r"printf 'A\x00\xff' | tee TEE.BIN; cat TEE.BIN", b'A\0\xffA\0\xff')
    case('strings-separation', 'strings', r"printf '\x00alpha\x00beta\n' | strings -n 4", b'alpha\nbeta\n')
    case('length-argument-bytes', 'length', "length 'a b'", b'3\n')
    case('od-byte-format', 'od', r"printf '\x00\x7f\xff' | od -An -tx1", b' 00 7f ff\n')
    case('catv-controls', 'catv', r"printf 'A\x01\x7f\n' | catv", b'A^A^?\n')
    case('split-byte-parts', 'split', "printf abcdefg | split -b 3 - SPLIT; cat SPLITaa SPLITab SPLITac", b'abcdefg')
    case('less-redirected', 'less', 'less A.TXT', b'a\nb\nc\n')
    case('more-redirected', 'more', 'more A.TXT', b'a\nb\nc\n')
    case('yes-pipe-close', 'yes', 'yes okay | head -n 3', b'okay\nokay\nokay\n')
    case('getopt-quoted-options', 'getopt', "getopt -o ab: -l long: -- -a -b 'a b' --long=x tail", b"'-a' '-b' 'a b' '--long' 'x' '--' 'tail'\n")
    case('getopt-invalid', 'getopt', 'getopt -q -o a -- -z', b'', 1)
    case('env-child-and-parent', 'env', "env M9QA='a b' printenv M9QA; printenv M9QA", b'a b\n', 1)
    case('printenv-missing', 'printenv', 'printenv M9_MISSING_8392', b'', 1)
    case('envdir-trim', 'envdir', "mkdir -p ENV; printf ' value  \nignored' > ENV/M9QA; envdir ENV printenv M9QA", b' value\n')
    case('pwd-working-directory', 'pwd', 'pwd', b'/TMP/CLIQA\n')
    case('shell-home-rooted', 'sh', "printf '%s\\n' ~; cd ~; pwd", b'/SYS\n/SYS\n')
    case('which-executable', 'which', 'which cat', pattern=rb'/BIN/[Cc][Aa][Tt]\.[Ss][Cc][Xx]\n')
    case('which-missing', 'which', 'which missing_9381', b'', 1)
    case('realpath-existing', 'realpath', 'realpath ./A.TXT', b'/TMP/CLIQA/A.TXT\n')
    case('realpath-missing', 'realpath', 'realpath missing_9381', b'', 1)
    case('readlink-canonical-missing', 'readlink', 'readlink -m ./sub/../missing', b'/TMP/CLIQA/missing\n')
    case('ls-sort-hidden', 'ls', 'mkdir -p LIST; touch LIST/z LIST/a LIST/.hidden; ls -1 LIST', b'a\nz\n')
    case('ls-all-excludes-dot-pair', 'ls', 'ls -A LIST', b'.hidden\na\nz\n')
    case('mkdir-parents', 'mkdir', 'mkdir -p MK/A/B; test -d MK/A/B', b'')
    case('mkdir-existing-error', 'mkdir', 'mkdir MK', b'', 1)
    case('touch-preserves-data', 'touch', 'cp A.TXT TOUCH; touch TOUCH EMPTY; cat TOUCH; test -f EMPTY', b'a\nb\nc\n')
    case('cp-binary', 'cp', 'cp BINARY.BIN COPY.BIN; cat COPY.BIN', b'\0A\xff\n')
    case('cp-recursive', 'cp', 'mkdir -p CP/A; cp A.TXT CP/A/F; cp -r CP CP2; cat CP2/A/F', b'a\nb\nc\n')
    case('cp-self-reject', 'cp', 'cp A.TXT A.TXT', b'', 1)
    case('mv-keeps-data', 'mv', 'cp A.TXT MOVE; mv MOVE MOVED; cat MOVED; test ! -e MOVE', b'a\nb\nc\n')
    case('rm-recursive', 'rm', 'mkdir -p RM/A; cp A.TXT RM/A/F; rm -r RM; test ! -e RM', b'')
    case('rm-missing-force', 'rm', 'rm -f missing_9381', b'')
    case('rmdir-empty', 'rmdir', 'mkdir EMPTYDIR; rmdir EMPTYDIR; test ! -e EMPTYDIR', b'')
    case('rmdir-nonempty-refuse', 'rmdir', 'rmdir MK', b'', 1)
    case('find-name-type', 'find', 'find CP2 -type f -name F', b'/TMP/CLIQA/CP2/A/F\n')
    case('du-summary', 'du', 'du -s CP2', b'1\tCP2\n')
    case('chmod-rw-model', 'chmod', 'cp A.TXT META; chmod 310 META; stat META', pattern=rb'TMP/CLIQA/META kind=1 bytes=6 uid=-1 gid=-1 rw=7 generation=[0-9]+\n')
    case('chown-numeric', 'chown', 'chown 1200:1201 META; stat META', pattern=rb'TMP/CLIQA/META kind=1 bytes=6 uid=1200 gid=1201 rw=7 generation=[0-9]+\n')
    case('chgrp-numeric', 'chgrp', 'chgrp 1202 META; stat META', pattern=rb'TMP/CLIQA/META kind=1 bytes=6 uid=1200 gid=1202 rw=7 generation=[0-9]+\n')
    case('install-copy-mode', 'install', 'install -m 300 A.TXT INSTALLED; cat INSTALLED; stat INSTALLED', pattern=rb'a\nb\nc\nTMP/CLIQA/INSTALLED kind=1 bytes=6 uid=-1 gid=-1 rw=3 generation=[0-9]+\n')
    case('stat-exact-object', 'stat', 'stat A.TXT', pattern=rb'TMP/CLIQA/A.TXT kind=1 bytes=6 uid=-1 gid=-1 rw=[0-9]+ generation=[0-9]+\n')
    case('dd-skip-count', 'dd', 'dd if=A.TXT bs=2 skip=1 count=1', b'b\n')
    case('dd-binary-write', 'dd', 'dd if=BINARY.BIN of=DD.BIN bs=3; cat DD.BIN', b'\0A\xff\n')
    case('dd-bad-block', 'dd', 'dd bs=0', b'', 2)
    case('cksum-POSIX-answer', 'cksum', 'printf abc | cksum', b'1219131554 3\n')
    case('sum-system-v', 'sum', 'printf abc | sum -s', b'294 1\n')
    case('sum-BSD', 'sum', 'printf abc | sum -r', b'16556 1\n')
    case('pipe-progress-binary', 'pipe_progress', 'cat BINARY.BIN | pipe_progress', b'\0A\xff\n')
    case('zcat-stream', 'zcat', 'printf abc | gzip -c | zcat', b'abc')
    case('gunzip-bad-format', 'gunzip', 'printf bad | gunzip -c', b'', 1)
    case('bzip2-truncated', 'bunzip2', 'printf bad | bunzip2 -c', b'', 1)
    case('unlzma-stdin', 'unlzma', 'unlzma -c /SYS/TEST/DATA.LZMA', b'contents')
    case('tar-create-list', 'tar', 'tar cf QA.TAR A.TXT BINARY.BIN; tar tf QA.TAR', b'TMP/CLIQA/A.TXT\nTMP/CLIQA/BINARY.BIN\n')
    case('tar-extract', 'tar', 'mkdir -p TAROUT; cd TAROUT; tar xf ../QA.TAR; cat TMP/CLIQA/BINARY.BIN', b'\0A\xff\n')
    case('run-parts-sorted-filter', 'run-parts', 'mkdir -p PARTS; cp /BIN/TRUE.SCX PARTS/a; cp /BIN/FALSE.SCX PARTS/z.skip; run-parts --test PARTS', b'/TMP/CLIQA/PARTS/a\n')
    case('run-parts-execute', 'run-parts', 'run-parts PARTS', b'')
    case('watch-bounded', 'watch', 'watch -t --count 2 -n 1 echo tick', b'tick\ntick\n')
    case('watch-child-status', 'watch', 'watch -t --count 1 false', b'', 1)
    case('time-child-output', 'time', 'time printf done', b'done')
    case('time-child-failure', 'time', 'time false', b'', 1)
    case('timeout-child-success', 'timeout', 'timeout 1 printf done', b'done')
    case('timeout-terminates', 'timeout', 'timeout 1 sleep 10', b'', 124)
    case('sleep-zero', 'sleep', 'sleep 0', b'')
    case('usleep-small', 'usleep', 'usleep 1', b'')
    case('uptime-shape', 'uptime', 'uptime', pattern=rb'up [0-9]+d [0-9]+h [0-9]+m [0-9]+s\n')
    case('free-accounting', 'free', 'free', pattern=rb'MEMORY TOTAL_KiB USED_KiB FREE_KiB\nPhysical [0-9]+ [0-9]+ [0-9]+\n')
    case('df-capacity', 'df', 'df', pattern=rb'SandFS TOTAL_KiB LIVE_KiB RESERVED_KiB FREE_KiB MAX_RUN_KiB ENTRIES\n/ 65536 [0-9]+ [0-9]+ [0-9]+ [0-9]+ [0-9]+/2048\n')
    case('mountpoint-root', 'mountpoint', 'mountpoint /', b'/ is a mountpoint\n')
    case('mountpoint-nonroot', 'mountpoint', 'mountpoint -q .', b'', 1)
    case('ps-visible-identity', 'ps', 'ps', pattern=rb'PID UID GID STATE NAME\n(?:[0-9]+ -?[0-9]+ -?[0-9]+ [0-9]+ [^\n]*\n)+')
    case('top-batch-once', 'top', 'top -b -n 1', pattern=rb'SandCore tasks   uptime [0-9]+s\nMemory KiB: used [0-9]+ / [0-9]+   free [0-9]+\nCPU PIT samples: waiting for interval\nPID  UID  GID  STATE  CPU%  SAMPLES  NAME\n(?:[^\n]+\n)*')
    case('uname-platform', 'uname', 'uname -a', b'SandCore M9 i386\n')
    case('id-management', 'id', 'id', pattern=rb'uid=-1 gid=-1 realm=[0-9]+\n')
    case('whoami-management', 'whoami', 'whoami', b'SYSTEM\n')
    case('logname-management', 'logname', 'logname', b'SYSTEM\n')
    case('tty-management', 'tty', 'tty', b'external-serial\n')
    case('date-format-literals', 'date', 'date +M9:%Y:%m:%d:%Z:%z', pattern=rb'M9:20[0-9]{2}:[0-9]{2}:[0-9]{2}:UTC:\+0000\n')
    case('date-refuse-setting', 'date', 'date -s 2020-01-01', b'', 2)
    month = calendar.TextCalendar(calendar.SUNDAY).formatmonth(2024,2)
    # cal的布局合同没有Python页眉居中；检查独立日历推导的闰日和首行位置。
    case('cal-leap-year', 'cal', 'cal 2 2024', pattern=rb'February 2024\nSu Mo Tu We Th Fr Sa\n             1  2  3\n 4  5  6  7  8  9 10\n11 12 13 14 15 16 17\n18 19 20 21 22 23 24\n25 26 27 28 29\n')
    case('hwclock-read', 'hwclock', 'hwclock', pattern=rb'20[0-9]{2}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} UTC\n')
    case('kill-job', 'kill', 'sleep 30 &\np=$!\nsleep 1\nkill "$p"; wait "$p"; test "$?" -ne 0', b'')
    case('pidof-live-process', 'pidof', 'sleep 30 &\np=$!\nsleep 1\npidof sleep.scx > PIDS; kill "$p"; wait "$p"; test -s PIDS', b'')
    case('pgrep-live-process', 'pgrep', 'sleep 30 &\np=$!\nsleep 1\npgrep -x -c sleep.scx; kill "$p"; wait "$p"; true', b'1\n')
    case('pkill-live-process', 'pkill', 'sleep 30 &\np=$!\nsleep 1\npkill -x sleep.scx; r=$?; wait "$p"; test "$r" -eq 0', b'')
    case('killall-live-process', 'killall', 'sleep 30 &\np=$!\nsleep 1\nkillall sleep.scx; r=$?; wait "$p"; test "$r" -eq 0', b'')
    case('logger-write-read', 'logger', "logger -t qa -p 13 'm9 text'; logread", pattern=rb'(?:[^\n]*\n)*\[[^\n]+ uid=-1 pid=[0-9]+ p=13\] qa: m9 text\n')
    case('logread-exact-last-line', 'logread', "logger -t qa 'm9 marker'; logread | tail -n 1 | sed 's/^.*qa: //'", b'm9 marker\n')
    case('crontab-publish-list', 'crontab', "printf '* * * * * echo QACRON\n' > CRON; crontab CRON; crontab -l", b'* * * * * echo QACRON\n')
    case('crontab-invalid-preserves', 'crontab', '''printf '99 * * * * echo bad\n' > CRONBAD; crontab CRONBAD; test "$?" -ne 0; crontab -l''', b'* * * * * echo QACRON\n')
    case('crontab-remove', 'crontab', 'crontab -r; crontab -l', b'', 1)
    case('script-persist-child-text', 'script', "script -c 'printf script-ok' SESSION; cat SESSION", b'script-ok')
    case('script-child-failure-persists', 'script', '''script -c 'printf failed; false' FAILED; r=$?; cat FAILED; test "$r" -eq 1''', b'failed')
    case('scriptreplay-content', 'scriptreplay', "printf '0.01 3\n0.01 3\n' > TIMING; printf abcdef > REPLAY; scriptreplay TIMING REPLAY 100", b'abcdef')
    case('scriptreplay-reject-extra', 'scriptreplay', "printf '0.01 3\n' > TIMING; scriptreplay TIMING REPLAY 100", b'abc', 1)
    case('ed-save-contents', 'ed', "printf 'a\nhello\nworld\n.\nw EDITED\nQ\n' | ed > EDLOG; cat EDITED", b'hello\nworld\n')
    case('adduser-create', 'adduser', 'adduser m9qa 1203 1203 /HOME/M9QA', b'Account created; set its password with passwd m9qa\n')
    case('chpasswd-set', 'chpasswd', "printf 'm9qa:Qam9_123456\n' | chpasswd", b'')
    case('deluser-remove', 'deluser', 'deluser m9qa', b'')
    case('deluser-protected-system', 'deluser', 'deluser SYSTEM', b'', 1)
    case('beep-effect', 'beep', 'beep 2', b'')
    case('fsync-existing', 'fsync', 'fsync A.TXT', b'')
    case('sync-disk', 'sync', 'sync', b'')
    return CASES


def run_disk(boot, data, out, plan):
    report = dict(status='RUNNING', scope='CLI_DECLARED_BEHAVIOR', cases=[], source_data=str(data), screenshots=[])
    with QemuSession(boot, data, out, 'tcg', True) as vm:
        g = Guest(vm)
        try:
            g.connect()
            vm.hmp('sendkey shift')
            result, _, _, _ = g.command('mkdir -p /TMP/CLIQA')
            if result:
                raise AssertionError('工作目录创建失败')
            for name, body in [('A.TXT',b'a\nb\nc\n'),('B.TXT',b'a\nx\nc\n'),('C.TXT',b'a\nc\n'),
                               ('BINARY.BIN',b'\0A\xff\n'),('CHANGE.PATCH',b'--- A.TXT\n+++ B.TXT\n@@ -1,3 +1,3 @@\n a\n-b\n+x\n c\n')]:
                g.put_bytes(body, '/TMP/CLIQA/'+name, name)
            token = '__CLI_'+secrets.token_hex(16)
            lines = []
            for i, c in enumerate(plan):
                lines += [f"printf '{token}:B:{i}\\n'", '(', 'cd /TMP/CLIQA', c['script'], ')',
                          '_qa_result=$?', f"printf '\\n{token}:E:{i}:%s\\n' \"$_qa_result\""]
            script = '\n'.join(lines)+'\n'
            (out/'cases.sh').write_text(script, encoding='utf-8')
            started = time.monotonic()
            # Shell明确有512 token/256 node上限；小批次既减少握手，
            # 又不把测试总长度误当成单个CLI的运行故障。
            body, transcripts, elapsed, cpu = b'', [], 0, 0
            for begin in range(0,len(plan),5):
                block='\n'.join(lines[begin*7:min(begin+5,len(plan))*7])+'\n'
                result, part, transcript, wall, used_cpu=g.run(block,120)
                body+=part;transcripts.append(transcript);elapsed+=wall;cpu+=used_cpu
                print(f'{out.name}: executed {min(begin+5,len(plan))}/{len(plan)}',flush=True)
                if result:
                    raise AssertionError('批脚本未完成: '+repr(transcript[-800:]))
            transcript=b'\n'.join(transcripts)
            (out/'batch-stdout.bin').write_bytes(body)
            report.update(batch_exit=result, wall_seconds=time.monotonic()-started, qemu_cpu_seconds=cpu,
                          stdout_sha256=hashlib.sha256(body).hexdigest(), transcript=transcript.decode('utf-8','replace'))
            failures = []
            for i, c in enumerate(plan):
                pattern = re.escape(f'{token}:B:{i}\n'.encode())+rb'(.*?)\n'+re.escape(f'{token}:E:{i}:'.encode())+rb'([0-9]+)\n'
                m = re.search(pattern, body, re.S)
                output, code = (m[1],int(m[2])) if m else (b'',-999)
                okay = code == c['expected_exit']
                if c['expected'] is not None:
                    okay = okay and output == c['expected']
                if c['pattern'] is not None:
                    okay = okay and re.fullmatch(c['pattern'],output) is not None
                item = dict(name=c['name'], reference=c['reference'], status='PASS' if okay else 'FAIL',
                            exit_code=code, expected_exit=c['expected_exit'], stdout_hex=output.hex(),
                            expected_hex=c['expected'].hex() if c['expected'] is not None else None,
                            pattern=c['pattern'].decode('ascii') if c['pattern'] else None,
                            script=c['script'])
                report['cases'].append(item)
                if not okay:
                    failures.append(c['name'])
                    print('FAIL',c['name'],code,repr(output[:180]),flush=True)
            # 真实终端专用工具在外部串口stdout执行，文件重定向不能假装TTY。
            for name, command, pattern in [('ttysize','ttysize',rb'[0-9]+ [0-9]+(?:\r?\n|$)'),
                                            ('resize','resize',rb'COLUMNS=[0-9]+; LINES=[0-9]+; export COLUMNS LINES;(?:\r?\n|$)'),
                                            ('clear',r"clear; printf '\n'",rb'\x1b\[2J\x1b\[H'),('reset',r"reset; printf '\n'",rb'\x1b\[2J\x1b\[H')]:
                code, output, _, _ = g.command(command)
                okay=code==0 and re.search(pattern,output) is not None
                report['cases'].append(dict(name='external-terminal-'+name, reference=name,status='PASS' if okay else 'FAIL',exit_code=code,stdout_hex=output.hex()))
                if not okay:
                    failures.append(name)
                    print('FAIL',name,code,repr(output[:180]),flush=True)
            screenshot=out/'cli.ppm'
            vm.hmp('screendump "'+screenshot.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(screenshot))
            report['status']='CLI_CASES_PASS' if not failures else 'FAIL'
            report['failures']=failures
        finally:
            (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            g.close()
    return report


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--boot',required=True,type=Path)
    parser.add_argument('--data',required=True,action='append',type=Path)
    parser.add_argument('--out',required=True,type=Path)
    args=parser.parse_args()
    if os.name!='nt' or len(set(p.resolve() for p in args.data))<2:
        parser.error('使用Windows Python与两个不同来源盘')
    args.out.mkdir(parents=True,exist_ok=False)
    plan=cases()
    reports=[]
    for i,data in enumerate(args.data,1):
        report=run_disk(args.boot,data,args.out/f'disk-{i}',plan)
        reports.append(report)
        matrix=dict(status='CLI_CASES_PASS' if all(r['status']=='CLI_CASES_PASS' for r in reports) else 'FAIL',
                    disks=reports, scope='DECLARED_SUBSETS_ONLY_NOT_80_PERCENT_CERTIFICATION')
        (args.out/'matrix.json').write_text(json.dumps(matrix,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(f'disk-{i}: {report["status"]} {len(report["cases"])} cases',flush=True)
    return 0 if all(r['status']=='CLI_CASES_PASS' for r in reports) else 1


if __name__=='__main__':
    raise SystemExit(main())
