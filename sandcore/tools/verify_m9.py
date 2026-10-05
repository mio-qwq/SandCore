#!/usr/bin/env python3
"""M9 Windows双盘客体集成验收入口，源码已写，第二阶段才执行。

所有VM从源镜像生成副本，私有pipe与ACL由scserial复用。每个用例
记录真实返回码/字节/耗时；任何失败、未覆盖合同都不能输出M9_COMPLETE。
"""
import argparse
import bz2
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import struct
import threading
import time
import wave
import zlib

from scserial import QemuSession, sha
from serial_protocol import ManagementClient, frame

ROOT = Path(__file__).resolve().parents[1]


class Guest:
    def __init__(self, vm):
        self.vm = vm
        self.condition = threading.Condition()
        self.console, self.debug = bytearray(), bytearray()
        self.stop = threading.Event()
        self.errors = []
        self.client = ManagementClient(vm.pipes['management'], self.receive)
        self.rx = {role: (vm.out / (role + '-rx.bin')).open('xb')
                   for role in ('debug', 'management')}
        self.reader = threading.Thread(target=self.pump, name='M9-reader')
        self.reader.start()
        self.heart = None

    def receive(self, data):
        with self.condition:
            self.console.extend(data)
            if len(self.console) > 32 * 1024 * 1024:
                raise RuntimeError('客体输出超过本次验收容量')
            self.condition.notify_all()

    def pump(self):
        try:
            while not self.stop.is_set():
                for role in ('debug', 'management'):
                    data = self.vm.pipes[role].read()
                    if not data:
                        continue
                    self.rx[role].write(data)
                    self.rx[role].flush()
                    if role == 'management':
                        self.client.decoder.feed(data)
                    else:
                        with self.condition:
                            self.debug.extend(data)
                            self.condition.notify_all()
                self.stop.wait(.005)
        except BaseException as error:
            self.errors.append(error)
            self.client.fail(error)
            with self.condition:
                self.condition.notify_all()

    def wait(self, pattern, start=0, timeout=60, debug=False):
        expression = re.compile(pattern)
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                if self.errors:
                    raise RuntimeError('串口读取失败') from self.errors[0]
                data = bytes(self.debug if debug else self.console)
                match = expression.search(data, start)
                if match:
                    return match
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f'等待客体标记超时: {pattern!r}')
                if self.vm.process.poll() is not None:
                    raise RuntimeError('QEMU在验收期间退出')
                self.condition.wait(min(.25, remaining))

    def connect(self, heartbeat=True):
        self.client.hello()
        self.wait(rb'\[SYSTEM\] # ', timeout=90)
        if not heartbeat:
            return
        def heartbeat():
            try:
                while not self.stop.wait(5):
                    # 调试暂停时PIT不走；管理心跳必须暂缓且不注入COM1。
                    tail = bytes(self.debug)
                    if tail.rfind(b'STOP ') > tail.rfind(b'OK resume'):
                        continue
                    self.client.checked(10)
            except BaseException as error:
                if not self.stop.is_set():
                    self.errors.append(error)
                    self.client.fail(error)
        self.heart = threading.Thread(target=heartbeat, name='M9-heartbeat')
        self.heart.start()

    def command(self, text, timeout=90, secret=None):
        marker = ('__M9_' + secrets.token_hex(12)).encode()
        start = len(self.console)
        # 任意命令可输出无LF的二进制/正文；先保存其状态并单独分隔
        # 标记，避免把正常的printf/cat误判为Shell超时。
        command = text + '; _m9_status=$?; printf "\\n"; echo ' + marker.decode() + ':$_m9_status\n'
        if len(command.encode()) > 1023:
            raise ValueError('自动化命令超过交互行容量')
        begin, cpu = time.monotonic(), process_cpu(self.vm.process)
        self.client.input(command.encode('utf-8'))
        if secret is not None:
            self.wait(rb'M9 SECRET READY\r?\n', start, timeout)
            self.client.input(secret.encode('ascii') + b'\n')
        done = self.wait(rb'(?:\r?\n)' + marker + rb':([0-9]+)\r?\n', start, timeout)
        output = bytes(self.console[start:done.start()])
        self.wait(rb'\[SYSTEM\] # ', done.end(), timeout)
        return int(done[1]), output, time.monotonic()-begin, process_cpu(self.vm.process)-cpu

    def put_bytes(self, data, path, label='payload'):
        source = self.vm.out / ('input-' + label + '-' + secrets.token_hex(4))
        source.write_bytes(data)
        return self.client.put(source, path)

    def get_bytes(self, path, label='output'):
        output = self.vm.out / (label + '-' + secrets.token_hex(4))
        self.client.get(path, output)
        return output.read_bytes()

    def run(self, script, timeout=90):
        self.put_bytes(script.encode('utf-8') + b'\n', '/TMP/M9CASE.SH', 'script')
        result, transcript, elapsed, cpu = self.command('sh /TMP/M9CASE.SH > /TMP/M9OUT', timeout)
        return result, self.get_bytes('/TMP/M9OUT'), transcript, elapsed, cpu

    def dialog(self, command, ready):
        marker=('__M9_'+secrets.token_hex(12)).encode()
        start=len(self.console)
        self.client.input((command+'; echo '+marker.decode()+':$?\n').encode())
        self.wait(ready,start)
        return marker,start

    def finish_dialog(self,marker,start,timeout=90):
        done=self.wait(rb'(?:\r?\n)'+marker+rb':([0-9]+)\r?\n',start,timeout)
        self.wait(rb'\[SYSTEM\] # ',done.end(),timeout)
        return int(done[1]),bytes(self.console[start:done.start()])

    def close(self):
        self.stop.set()
        if self.heart:
            self.heart.join(timeout=12)
        self.reader.join(timeout=5)
        if self.reader.is_alive() or (self.heart and self.heart.is_alive()):
            raise RuntimeError('串口读取/心跳线程未停止')
        for stream in self.rx.values():
            stream.close()
        (self.vm.out / 'console.txt').write_bytes(bytes(self.console))
        if self.errors:
            raise RuntimeError('串口会话错误') from self.errors[0]


def process_cpu(process):
    class FileTime(ctypes.Structure):
        _fields_ = [('low', ctypes.c_ulong), ('high', ctypes.c_ulong)]
    values = [FileTime() for _ in range(4)]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetProcessTimes.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(FileTime)] * 4
    kernel.GetProcessTimes.restype = ctypes.c_int
    if not kernel.GetProcessTimes(int(process._handle), *(ctypes.byref(x) for x in values)):
        raise ctypes.WinError(ctypes.get_last_error())
    return sum(((x.high << 32) | x.low) for x in values[2:]) / 10000000


def png_from_ppm(path):
    data = path.read_bytes()
    match = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', data)
    if not match:
        raise ValueError('HMP未产出完整RGB PPM')
    width, height = int(match[1]), int(match[2])
    pixels = data[match.end():]
    if len(pixels) != width*height*3:
        raise ValueError('HMP截图像素容量不一致')
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind+payload))
    lines = b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
    result = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', width,height,8,2,0,0,0))
    result += chunk(b'IDAT', zlib.compress(lines)) + chunk(b'IEND', b'')
    output = path.with_suffix('.png')
    output.write_bytes(result)
    return {'path': str(output), 'width': width, 'height': height, 'sha256': sha(output)}


class Suite:
    def __init__(self, guest, report, symbols):
        self.guest, self.report = guest, report
        self.symbols = symbols

    def case(self, name, script, expected=None, status=0, contains=None, timeout=90):
        result, output, transcript, elapsed, cpu = self.guest.run(script, timeout)
        okay = result == status and (expected is None or output == expected)
        if contains is not None:
            okay = okay and contains in output and b'FAIL ' not in output
        item = dict(name=name, status='PASS' if okay else 'FAIL', exit_code=result,
                    expected_exit=status, bytes=len(output), sha256=hashlib.sha256(output).hexdigest(),
                    wall_seconds=elapsed, qemu_cpu_seconds=cpu,
                    stdout_hex=output[:4096].hex(), transcript=transcript.decode('utf-8','replace'))
        self.report['cases'].append(item)
        self.persist()
        if not okay:
            raise AssertionError(f'{name}: exit={result}, output={output[:256]!r}')
        return output

    def persist(self):
        (self.guest.vm.out / 'verification.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')

    def capture_bytes(self,path,label):
        # 原生BGRA没有降采样；先在真实客体用独立gzip压缩，再经同一
        # 512B管理协议传回。静态客户区高度冗余，避免把百万个重复字节
        # 都交给物理UART；宿主解压校验CRC后仍逐像素核对完整BMP。
        compressed=path+'.GZ'
        self.case(label+'-guest-lossless-gzip',f'gzip -c {path} > {compressed}')
        begin=time.monotonic()
        wire=self.guest.get_bytes(compressed,label+'.bmp.gz')
        elapsed=time.monotonic()-begin
        decoder=zlib.decompressobj(31)
        maximum=1920*1080*4+122
        body=decoder.decompress(wire,maximum+1)
        if len(body)>maximum or not decoder.eof or decoder.unconsumed_tail or decoder.unused_data:
            raise AssertionError('客体gzip快照未完整结束或带尾部数据')
        output=self.guest.vm.out/(label+'-'+secrets.token_hex(4)+'.bmp')
        output.write_bytes(body)
        self.report['cases'].append(dict(name=label+'-lossless-serial-transfer',status='PASS',
                                         raw_bytes=len(body),wire_bytes=len(wire),wall_seconds=elapsed,
                                         raw_sha256=hashlib.sha256(body).hexdigest(),wire_sha256=hashlib.sha256(wire).hexdigest(),output=str(output)))
        self.persist()
        return body

    def native(self):
        for name in ('M9CHECK', 'IODENY', 'IDENTITY', 'DEBUG'):
            self.case('native-compile-'+name,
                      f's3c /SYS/TEST/{name}.C /TMP/{name}.SCX', timeout=240)
        for name in ('abi','streams','simd','audio'):
            self.case('guest-'+name, '/TMP/M9CHECK.SCX '+name, contains=b'PASS ', timeout=120)
        self.case('simd-preemption', '/SYS/TEST/SIMDSTATE.SCX 7 > /TMP/M9S1 &\n'
                  '/SYS/TEST/SIMDSTATE.SCX 13 > /TMP/M9S2 &\nwait\ncat /TMP/M9S1 /TMP/M9S2',
                  contains=b'PASS XMM7 MXCSR x87', timeout=120)
        self.case('simd-slot-reuse', '/SYS/TEST/SIMDSTATE.SCX 21', contains=b'PASS XMM7 MXCSR x87')
        self.case('streaming-MP3-seek-replay', '/SYS/TEST/AUDIOCHECK.SCX', contains=b'PASS MP3 tone decode frames replay seek')
        self.case('MP3-actual-player', 'soundplay /SYS/TEST/TONE.MP3', b'',timeout=30)
        self.case('WAV-actual-player', 'soundplay /SYS/SOUND/NOTICE.WAV', b'',timeout=30)

    def themes(self):
        self.case('native-compile-THEME','s3c /SYS/TEST/THEME.C /TMP/THEME.SCX',timeout=240)
        for theme in ('classic','aurora'):
            self.case('theme-'+theme,'/TMP/THEME.SCX '+theme,('PASS '+theme.capitalize()+'\n').encode())
            screenshot=self.guest.vm.out/('desktop-'+theme+'.ppm')
            self.guest.vm.hmp('screendump "'+str(screenshot).replace('\\','/')+'"')
            self.report['screenshots'].append(png_from_ppm(screenshot))
        self.persist()

    def nano(self):
        guest=self.guest
        original=b'one\r\ntwo\nlast'
        prefix='新 '.encode('utf-8')
        guest.put_bytes(original,'/TMP/M9NANO','nano-original')
        marker,start=guest.dialog('nano /TMP/M9NANO',rb'SandNano')
        guest.client.input(prefix+b'\x1bu\x1be\x0f')
        prompt=guest.wait(rb'Write file:',start)
        guest.client.input(b'\x15/SYS/CORE/NANO-DENIED\n')
        failed=guest.wait(rb'Write failed; buffer preserved',prompt.end())
        guest.client.input(b'\x0f')
        prompt=guest.wait(rb'Write file:',failed.end())
        guest.client.input(b'\x15/TMP/M9NANO\n')
        saved=guest.wait(rb'Saved',prompt.end())
        guest.client.input(b'\x17')
        found=guest.wait(rb'Find literal:',saved.end())
        guest.client.input(b'one\n')
        guest.wait(rb'Found',found.end())
        guest.client.input(b'\x18')
        code,output=guest.finish_dialog(marker,start)
        body=guest.get_bytes('/TMP/M9NANO','nano-saved')
        if code or body!=prefix+original:
            raise AssertionError('nano UTF8/CRLF/末行/undo-redo/失败后另存未保留正文')
        marker,start=guest.dialog('nano -v /TMP/M9NANO',rb'\[RO\]')
        guest.client.input(b'X')
        guest.wait(rb'Read only',start)
        guest.client.input(b'\x18')
        code,_=guest.finish_dialog(marker,start)
        if code or guest.get_bytes('/TMP/M9NANO')!=body:
            raise AssertionError('nano只读模式修改文件')
        self.report['cases'].append(dict(name='nano-interactive-UTF8-undo-redo-save-failure-save-search-readonly',status='PASS',sha256=hashlib.sha256(body).hexdigest()))
        self.persist()

    def scdbg(self):
        guest=self.guest
        marker,start=guest.dialog('scdbg /TMP/DEBUG.SCX',rb'scdbg> ')
        initial=guest.wait(rb'EIP=([0-9a-fA-F]{8}).*? VECTOR=([0-9]+)',start)
        address=initial[1]
        position=len(guest.console)
        guest.client.input(b'break '+address+b'\n')
        guest.wait(rb'scdbg> ',position)
        position=len(guest.console)
        guest.client.input(b'cont\n')
        guest.wait(rb'Paused EIP='+address,position)
        position=len(guest.console)
        guest.client.input(b'regs\n')
        guest.wait(rb'VECTOR=3 ',position)
        position=len(guest.console)
        guest.client.input(b'clear '+address+b'\nstep\n')
        guest.wait(rb'VECTOR=1 ',position)
        position=len(guest.console)
        guest.client.input(b'mem '+address+b' 16\nstack 1\ncode '+address+b' 16\nrestart\n')
        guest.wait(rb'Paused EIP=',position)
        position=len(guest.console)
        guest.client.input(b'cont\n')
        guest.wait(rb'Target exited 0',position)
        guest.client.input(b'quit\n')
        code,output=guest.finish_dialog(marker,start)
        if code or b'scdbg:' in output or b'????????' in output:
            raise AssertionError('三环调试现场/断点/步进/重启/退出失败')
        self.report['cases'].append(dict(name='scdbg-real-INT3-step-memory-stack-code-restart-exit',status='PASS',entry=address.decode()))
        self.persist()

    def identity(self):
        self.guest.command('rm -f /TMP/M9WIN; /TMP/M9CHECK.SCX gui &')
        deadline=time.monotonic()+15
        while True:
            try:
                handle=int(self.guest.get_bytes('/TMP/M9WIN'))
                break
            except RuntimeError:
                if time.monotonic()>=deadline:
                    raise
                time.sleep(.1)
        self.case('SYSTEM-window-present-before-identity',f'capture {handle} /TMP/M9PRE.BMP',b'')
        status, output, elapsed, cpu = self.guest.command('/TMP/IDENTITY.SCX', timeout=240, secret=secrets.token_hex(24))
        required = (b'PASS parent identity unchanged', b'PASS root ordinary rw bypass',
                    b'PASS other UID read denied', b'PASS UART GP', b'PASS SYSTEM window capture denied')
        okay = status == 0 and b'FAIL ' not in output and all(text in output for text in required)
        self.report['cases'].append(dict(name='kernel-identities-root-user-UART', status='PASS' if okay else 'FAIL',
                                         wall_seconds=elapsed,qemu_cpu_seconds=cpu,output=output.decode('utf-8','replace')))
        self.persist()
        if not okay:
            raise AssertionError('身份/UART客体验证失败')
        self.case('SYSTEM-window-present-after-identity',f'capture {handle} /TMP/M9POST.BMP',b'')
        self.guest.vm.hmp('sendkey esc')
        self.case('capture-closed-SYSTEM-handle',f'sleep 1; capture {handle} /TMP/M9STALE.BMP',status=1)

    def transfer(self):
        body = bytes(range(256))*17 + b'\x7d\x7e\0serial\xff'
        self.guest.put_bytes(body, '/TMP/M9WIRE', 'binary')
        retrieved = self.guest.get_bytes('/TMP/M9WIRE', 'binary-return')
        if body != retrieved:
            raise AssertionError('串口二进制往返不一致')
        client = self.guest.client
        original = b'old committed\n'
        self.guest.put_bytes(original, '/TMP/M9BAD', 'old')
        client.checked(3, struct.pack('<I', 3)+b'\0'*32+client.path('/TMP/M9BAD'))
        client.checked(4, struct.pack('<I', 0)+b'bad')
        code, _ = client.request(5)
        if code >= 0 or self.guest.get_bytes('/TMP/M9BAD') != original:
            raise AssertionError('坏SHA提交或损坏旧文件')
        client.checked(3, struct.pack('<I',3)+hashlib.sha256(b'new').digest()+client.path('/TMP/M9BAD'))
        client.checked(4, struct.pack('<I',0)+b'n')
        client.checked(6)
        if self.guest.get_bytes('/TMP/M9BAD') != original:
            raise AssertionError('取消传输损坏已发布文件')
        # 损坏CRC的输入帧不得变成Shell输入；下一有效ping仍正常。
        corrupt = bytearray(frame(2, client.sequence+100, b'echo BADCRC\n'))
        corrupt[-2] ^= 1
        position = len(self.guest.console)
        self.guest.vm.pipes['management'].write(bytes(corrupt))
        client.checked(10)
        if b'BADCRC' in self.guest.console[position:]:
            raise AssertionError('坏CRC被交给Shell')
        self.report['cases'].append(dict(name='serial-binary-integrity-cancel-bad-SHA-CRC',status='PASS',bytes=len(body)))
        self.persist()

    def cli(self):
        cases = [
            ('shell-quote', "printf '<%s>\\n' 'a b' ''", b'<a b>\n<>\n',0),
            ('shell-function', 'f(){ echo "$1"; }; f yes', b'yes\n',0),
            ('shell-loop', 'for x in a b c; do printf "%s" "$x"; done', b'abc',0),
            ('shell-condition', 'if false; then echo no; elif true; then echo yes; fi', b'yes\n',0),
            ('shell-case', 'case abc in a*) echo yes;; *) echo no;; esac', b'yes\n',0),
            ('shell-parameters', 'v=abcabc; printf "%s %s %s\\n" "${v%bc}" "${v##a*}" "${#v}"', b'abca  6\n',0),
            ('shell-substitute', 'v=$(printf "a\\n\\n"); printf "<%s>\\n" "$v"', b'<a>\n',0),
            ('shell-arithmetic', 'echo $((3+4*5))', b'23\n',0),
            ('shell-here', 'v=expanded\ncat <<EOF\n$v\nEOF', b'expanded\n',0),
            ('shell-here-quoted', "v=expanded\ncat <<'EOF'\n$v\nEOF", b'$v\n',0),
            ('shell-here-tabs', 'cat <<-EOF\n\ttext\n\tEOF', b'text\n',0),
            ('shell-short-circuit', 'false && echo no; true || echo no; echo yes', b'yes\n',0),
            ('shell-background-status', 'false &\np=$!\nsleep 1\njobs > /TMP/M9JOBS\nwait "$p"\necho $?\nwait "$p"\necho $?', b'1\n127\n',0),
            ('cat-binary', "printf '\\x00A\\xff' | cat", b'\0A\xff',0),
            ('grep-pipeline', "printf 'alpha\\nbeta\\n' | grep '^b'", b'beta\n',0),
            ('sed-substitute', "printf 'abc\\n' | sed 's/b/BB/'", b'aBBc\n',0),
            ('awk-sum', "printf 'a 3\\nb 7\\n' | awk '{s+=$2; print NR,$1} END{print s}'", b'1 a\n2 b\n10\n',0),
            ('awk-array', "awk 'BEGIN{a[2]=5; print (2 in a),a[2]; delete a[2]; print (2 in a)}'", b'1 5\n0\n',0),
            ('awk-gsub', "printf 'abc\\n' | awk '{gsub(/b/,\"&X\"); print $0}'", b'abXc\n',0),
            ('awk-short-circuit', "awk 'BEGIN{x=0; print (x&&1/0); printf(\"%04d %s\\n\",12,\"ok\")}'", b'0\n0012 ok\n',0),
            ('awk-reject-ignored-update', "awk 'BEGIN{RS=\"x\"}'", b'',2),
            ('sort-unique', "printf 'z\\na\\nz\\n' | sort | uniq", b'a\nz\n',0),
            ('cut', "printf 'a:b:c\\n' | cut -d : -f 2", b'b\n',0),
            ('tr', "printf 'abc\\n' | tr a-z A-Z", b'ABC\n',0),
            ('head-tail', "printf '1\\n2\\n3\\n' | tail -n 2 | head -n 1", b'2\n',0),
            ('rev', "printf 'abc\\n' | rev", b'cba\n',0),
            ('tac', "printf '1\\n2\\n' | tac", b'2\n1\n',0),
            ('paste', "printf 'a\\nb\\n' | paste -s -d :", b'a:b\n',0),
            ('fold', "printf 'abcdef\\n' | fold -w 3", b'abc\ndef\n',0),
            ('expr', r'expr 3 + 4 \* 5', b'23\n',0),
            ('dc', "echo '3 4 + p' | dc", b'7\n',0),
            ('seq', 'seq 2 2 6', b'2\n4\n6\n',0),
            ('basename', 'basename /a/b.c .c', b'b\n',0),
            ('dirname', 'dirname /a/b.c', b'/a\n',0),
            ('test', 'test 4 -gt 3 && echo yes', b'yes\n',0),
            ('false', 'false', b'',1),
            ('true', 'true', b'',0),
            ('xargs', "printf 'a b\\n' | xargs echo", b'a b\n',0),
            ('gzip-roundtrip', "printf 'binary\\x00data' | gzip -c | gunzip -c", b'binary\0data',0),
            ('bzip2-roundtrip', "printf 'binary\\x00data' | bzip2 -c | bunzip2 -c", b'binary\0data',0),
            ('uu-roundtrip', "printf 'data\\x00x' | uuencode file | uudecode -o /TMP/M9UU; cat /TMP/M9UU", b'data\0x',0),
            ('readlink', 'readlink -m /TMP/a/../b', b'/TMP/b\n',0),
            # CLI阶段可独立选择，不能借transfer阶段偶然留下的文件。
            ('storage-flush', 'printf flush > /TMP/M9FLUSH; sync; fsync /TMP/M9FLUSH', b'',0),
            ('mktemp', 'v=$(mktemp /TMP/test.XXXXXXXX); test -f "$v"; r=$?; rm "$v"; exit "$r"', b'',0),
        ]
        for name,script,expected,status in cases:
            self.case(name, script, expected, status)
        for algorithm in ('md5','sha1','sha256','sha512'):
            expected = hashlib.new(algorithm,b'abc').hexdigest().encode()+b'  -\n'
            self.case(algorithm+'-known-answer', 'printf abc | '+algorithm+'sum', expected)
        self.case('cpio-roundtrip', "printf contents > /TMP/M9MEMBER\ncd /TMP\nprintf 'M9MEMBER\\n' | cpio -o -H newc -F M9ARCH\nrm M9MEMBER\ncpio -i -F M9ARCH\ncat M9MEMBER", b'contents')
        self.case('ar-roundtrip', 'cd /TMP; ar qc M9AR M9MEMBER; ar p M9AR M9MEMBER', b'contents')
        self.case('unzip-deflate', 'unzip -p /SYS/TEST/DATA.ZIP M9MEMBER', b'contents')
        self.case('manual-cli-page', 'man -w nano', b'/SYS/MAN/nano.TXT\n')
        self.case('cron-matching-shell', "mkdir -p /TMP/M9CRON\nprintf '* * * * * printf scheduled\\n' > /TMP/M9CRON/CRON.TAB\ncrond -c /TMP/M9CRON --once", b'scheduled')
        self.case('cron-invalid-table', "printf '99 * * * * echo invalid\\n' > /TMP/M9CRON/CRON.TAB\ncrond -c /TMP/M9CRON --once", b'',status=1)
        self.case('cron-child-failure', "printf '* * * * * false\\n' > /TMP/M9CRON/CRON.TAB\ncrond -c /TMP/M9CRON --once", b'',status=1)
        self.case('bzip2-external-fixture', 'bzcat /SYS/TEST/DATA.BZ2', b'contents')
        self.case('lzma-external-fixture', 'lzmacat /SYS/TEST/DATA.LZMA', b'contents')
        original=bytes(range(256))*17+b'\0bzip2 external oracle\xff'
        self.guest.put_bytes(original,'/TMP/M9BZIPIN','bzip-original')
        self.case('bzip2-compress-oracle', 'bzip2 -c /TMP/M9BZIPIN > /TMP/M9BZIPOUT', b'')
        compressed=self.guest.get_bytes('/TMP/M9BZIPOUT','bzip-oracle.bz2')
        if bz2.decompress(compressed)!=original:
            raise AssertionError('客体bzip2与独立宿主解码结果不同')
        self.report['cases'].append(dict(name='bzip2-independent-decoder',status='PASS',bytes=len(compressed)))
        for name,path in [('BZIP','/SYS/TEST/DATA.BZ2'),('LZMA','/SYS/TEST/DATA.LZMA')]:
            payload=self.guest.get_bytes(path,'truncated-source-'+name)
            target='/TMP/M9BAD'+name
            suffix='.bz2' if name=='BZIP' else '.lzma'
            self.guest.put_bytes(payload[:-3],target+suffix,'truncated-'+name)
            self.guest.put_bytes(b'original',target,'keep-'+name)
            command='bunzip2' if name=='BZIP' else 'unlzma'
            self.case('truncated-'+name+'-preserves-target', command+' -f -k '+target+suffix, status=1)
            if self.guest.get_bytes(target,'preserved-'+name)!=b'original':
                raise AssertionError('坏压缩文件改变已提交目标: '+name)
        self.persist()

    def ring0(self):
        self.case('native-SKM', 'sccc /SYS/TEST/ONCE.C /SYS/MOD/M9ONCE.SKM --skm', timeout=240)
        self.case('Ring0-once', 'skmrun /SYS/MOD/M9ONCE.SKM; cat /TMP/M9RING0', b'1\n')
        self.case('Ring0-once-new-BSS', 'skmrun /SYS/MOD/M9ONCE.SKM; cat /TMP/M9RING0', b'1\n')
        self.case('Ring0-resident', 'skmrun /SYS/MOD/M9ONCE.SKM --resident; cat /TMP/M9RING0', b'1\n')
        self.case('Ring0-list-resident', 'lsmod | grep M9ONCE.SKM', contains=b'SYS/MOD/M9ONCE.SKM')
        self.case('Ring0-no-hot-reload', 'skmrun /SYS/MOD/M9ONCE.SKM --resident', status=1)
        guest = self.guest
        start = len(guest.debug)
        guest.vm.pipes['debug'].write(b'hello\nhalt\n')
        stop = guest.wait(rb'STOP vector=([0-9a-f]+) eip=([0-9a-f]+)\r\n', start, debug=True)
        # 外部IRQ可打断三环；其EIP是当前用户虚拟地址，不能当作
        # 恒等映射物理RAM。用同批内核符号验证RAM查看，不放宽范围。
        kernel_target=f'{self.symbols["rtc_snapshot"]:08x}'.encode()
        guest.vm.pipes['debug'].write(b'regs\nmem '+kernel_target+b' 10\n')
        guest.wait(rb'DATA [0-9a-f]{32}\r\n', stop.end(), debug=True)
        position = len(guest.debug)
        guest.vm.pipes['debug'].write(b'step\n')
        guest.wait(rb'STOP vector=00000001 ', position, debug=True)
        position = len(guest.debug)
        guest.vm.pipes['debug'].write(b'cont\n')
        guest.wait(rb'OK resume\r\n', position, debug=True)
        guest.client.checked(10)
        self.report['cases'].append(dict(name='COM1-pause-regs-RAM-step-resume',status='PASS'))
        address=self.symbols['rtc_snapshot']
        target=f'{address:08x}'.encode()
        # 内存查看契约要求真正暂停；不能在运行态期待DATA，再为了测试
        # 放宽调试器状态检查。第二次停住后核对原指令和补丁，再继续命令。
        start=len(guest.debug)
        guest.vm.pipes['debug'].write(b'halt\n')
        guest.wait(rb'STOP vector=([0-9a-f]+) eip=([0-9a-f]+)\r\n',start,debug=True)
        start=len(guest.debug)
        guest.vm.pipes['debug'].write(b'mem '+target+b' 1\n')
        original=guest.wait(rb'DATA ([0-9a-f]{2})\r\n',start,debug=True)[1]
        start=len(guest.debug)
        guest.vm.pipes['debug'].write(b'break '+target+b'\nmem '+target+b' 1\n')
        guest.wait(rb'DATA cc\r\n',start,debug=True)
        start=len(guest.debug)
        guest.vm.pipes['debug'].write(b'cont\n')
        guest.wait(rb'OK resume\r\n',start,debug=True)
        marker=('__M9_break_'+secrets.token_hex(10)).encode()
        console_at=len(guest.console)
        debug_at=len(guest.debug)
        # 管理帧接收期间就可能进入内核断点，ACK要等cont以后才能发送。
        # COM2事务在独立线程等待，主线程继续操作COM1；不跳过ACK或
        # 放宽协议重试。这样实际验证的是两条外部硬件通道协作。
        input_errors=[]
        def issue_input():
            try:
                guest.client.input(b'date; echo '+marker+b':$?\n')
            except BaseException as error:
                input_errors.append(error)
        sender=threading.Thread(target=issue_input,name='M9-break-trigger-input')
        sender.start()
        try:
            stop=guest.wait(rb'STOP vector=00000003 eip='+target+rb'\r\n',debug_at,debug=True)
            debug_at=len(guest.debug)
            guest.vm.pipes['debug'].write(b'clear '+target+b'\nmem '+target+b' 1\n')
            restored=guest.wait(rb'DATA ([0-9a-f]{2})\r\n',debug_at,debug=True)[1]
            if restored!=original:
                raise AssertionError('COM1 clear未恢复原内核指令')
            guest.vm.pipes['debug'].write(b'cont\n')
            sender.join(timeout=15)
            if sender.is_alive():
                raise TimeoutError('COM1继续以后COM2输入仍未完成')
            if input_errors:
                raise input_errors[0]
        finally:
            if sender.is_alive():
                guest.client.fail(RuntimeError('COM1/COM2断点协调用例结束'))
                sender.join(timeout=3)
        done=guest.wait(rb'\r?\n'+marker+rb':0\r?\n',console_at)
        guest.wait(rb'\[SYSTEM\] # ',done.end())
        self.report['cases'].append(dict(name='COM1-real-INT3-patch-hit-clear',status='PASS',
                                         symbol='rtc_snapshot',address=address,original_byte=original.decode()))
        self.persist()

    def gui(self):
        guest = self.guest
        guest.command('rm -f /TMP/M9WIN /TMP/M9KEY; /TMP/M9CHECK.SCX gui &')
        deadline = time.monotonic()+15
        while True:
            try:
                handle = int(guest.get_bytes('/TMP/M9WIN'))
                break
            except RuntimeError:
                if time.monotonic()>=deadline:
                    raise
                time.sleep(.1)
        guest.vm.hmp('sendkey f1')
        self.case('GUI-real-key', 'sleep 1; cat /TMP/M9KEY', b'F1\n')
        screenshot = guest.vm.out / 'gui.ppm'
        guest.vm.hmp('screendump "'+str(screenshot).replace('\\','/')+'"')
        self.report['screenshots'].append(png_from_ppm(screenshot))
        self.case('capture-tool', f'capture {handle} /TMP/M9CAP.BMP')
        body = self.capture_bytes('/TMP/M9CAP.BMP','window')
        if len(body)<122 or body[:2]!=b'BM' or struct.unpack_from('<I',body,2)[0]!=len(body):
            raise AssertionError('指定窗口BMP并非完整文件')
        width,height = struct.unpack_from('<ii',body,18)
        if width<=0 or height>=0 or len(body)!=122+width*(-height)*4:
            raise AssertionError('窗口BMP尺寸/原生像素边界不一致')
        expected_row=struct.pack('<I',0xFFFBFCFE)*(width//2)+struct.pack('<I',0xFF173247)*(width-width//2)
        if body[122:]!=expected_row*(-height):
            raise AssertionError('串口返回的客户区BGRA像素/alpha与实际提交夹具不一致')
        self.report['cases'].append(dict(name='window-BGRA-capture-serial-download',status='PASS',width=width,height=-height,sha256=hashlib.sha256(body).hexdigest()))
        guest.vm.hmp('sendkey esc')
        self.persist()

    def legacy(self):
        guest=self.guest
        # 原版M7探针自带真实UD2恢复、旧88B调试现场、字体/指针边界
        # 检查和PASS文件。直接运行安装的旧SCX，不重编或修改其载荷。
        for mode,marker in (('handler','HANDLED'),('debug','DEBUG'),('font','FONT')):
            path='/HOME/'+marker+'.OK'
            guest.command('rm -f '+path+'; /LEGACY/APPS/PROBE.SCX '+mode+' &')
            deadline=time.monotonic()+45
            while True:
                try:
                    payload=guest.get_bytes(path,'legacy-'+mode)
                    if payload!=b'PASS':
                        raise AssertionError('原版M7 '+mode+'探针没有实际PASS')
                    break
                except RuntimeError:
                    if time.monotonic()>=deadline:
                        raise
                    time.sleep(.1)
            self.case('legacy-M7-'+mode+'-original-SCX','cat '+path,b'PASS')
            result,windows,transcript,elapsed,cpu=guest.run('capture')
            candidates=[row.split(b' ',4) for row in windows.splitlines() if b'M7 Probe' in row]
            if result or len(candidates)!=1:
                raise AssertionError('原版M7窗口没有唯一实际句柄')
            handle=int(candidates[0][0])
            self.case('legacy-M7-'+mode+'-capture',f'capture {handle} /TMP/M9LEGACY.BMP')
            body=self.capture_bytes('/TMP/M9LEGACY.BMP','legacy-'+mode)
            if body[:2]!=b'BM' or len(body)<122 or not any(body[122:]):
                raise AssertionError('原版M7客户区没有实际像素')
            screenshot=guest.vm.out/('legacy-'+mode+'.ppm')
            guest.vm.hmp('screendump "'+screenshot.as_posix()+'"')
            self.report['screenshots'].append(png_from_ppm(screenshot))
            guest.vm.hmp('sendkey esc')
            deadline=time.monotonic()+15
            while True:
                result,windows,_,_,_=guest.run('capture')
                if result==0 and not any(row.startswith(str(handle).encode()+b' ') for row in windows.splitlines()):
                    break
                if time.monotonic()>=deadline:
                    raise AssertionError('原版M7 ESC退出未回收窗口')
                time.sleep(.1)
            self.report['cases'].append(dict(name='legacy-M7-'+mode+'-ESC-window-reclaim',status='PASS',handle=handle,pixels_sha256=hashlib.sha256(body).hexdigest()))
            self.persist()

    def bootstrap(self):
        guest=self.guest
        self.case('native-SCCC-CLI-G2','sccc /SYS/SRC/sccc.c /TMP/M9S3C-G2.SCX',timeout=600)
        self.case('native-SCCC-CLI-G3','/TMP/M9S3C-G2.SCX /SYS/SRC/sccc.c /TMP/M9S3C-G3.SCX',timeout=600)
        g2=guest.get_bytes('/TMP/M9S3C-G2.SCX','compiler-G2.scx')
        g3=guest.get_bytes('/TMP/M9S3C-G3.SCX','compiler-G3.scx')
        if g2!=g3:
            raise AssertionError('命令行编译器G2/G3不相等')
        self.report['cases'].append(dict(name='SCCC-CLI-G2-G3-fixed-point',status='PASS',bytes=len(g2),sha256=hashlib.sha256(g2).hexdigest()))
        self.persist()
        # 固定点相等并不能独自证明编译正确：新一代还须真正编译并
        # 执行独立的旧ABI护栏、二进制流和整数SSE2探针及零环模块。
        self.case('G2-compile-independent-M9CHECK','/TMP/M9S3C-G2.SCX /SYS/TEST/M9CHECK.C /TMP/M9G2-CHECK.SCX',timeout=240)
        for mode in ('abi','streams','simd'):
            self.case('G2-actual-'+mode,'/TMP/M9G2-CHECK.SCX '+mode,contains=b'PASS ',timeout=120)
        self.case('G2-SKM-native-package','/TMP/M9S3C-G2.SCX /SYS/TEST/ONCE.C /SYS/MOD/G2ONCE.SKM --skm',timeout=240)
        self.case('G2-SKM-real-Ring0','skmrun /SYS/MOD/G2ONCE.SKM; cat /TMP/M9RING0',b'1\n')


STAGES=('transfer','themes','cli','native','nano','scdbg','identity','ring0','gui','legacy','bootstrap')
NATIVE_DEPENDENCIES={'scdbg':('DEBUG',),'identity':('M9CHECK','IODENY','IDENTITY'),'gui':('M9CHECK',)}


def verify_disk(boot, data, out, accel, symbol_path, symbols, stages):
    report = dict(scope='M9_INTEGRATION',status='RUNNING',cases=[],screenshots=[],
                  not_yet_covered=['all 310 CLI behavior rows','historical SCX/LEGACY corpus',
                                   'MP3 external encoders/VBR/joint-stereo and GUI controls',
                                   'sound shutdown wait and DMA recovery','filesystem crash/corrupt bank matrix',
                                   'screenshot token teardown/allocation/slot reuse matrix',
                                   'whole-system performance budgets and before-after comparisons'])
    with QemuSession(boot,data,out,accel,True) as vm:
        guest = Guest(vm)
        report['selected_stages']=list(stages)
        report['symbols']=dict(path=str(symbol_path.resolve()),sha256=sha(symbol_path))
        suite = Suite(guest, report, symbols)
        try:
            guest.connect()
            screenshot = vm.out / 'boot.ppm'
            vm.hmp('sendkey shift')
            vm.hmp('screendump "'+str(screenshot).replace('\\','/')+'"')
            report['screenshots'].append(png_from_ppm(screenshot))
            if 'native' not in stages:
                needed={name for stage in stages for name in NATIVE_DEPENDENCIES.get(stage,())}
                for name in ('M9CHECK','IODENY','IDENTITY','DEBUG'):
                    if name in needed:
                        suite.case('native-compile-'+name,f's3c /SYS/TEST/{name}.C /TMP/{name}.SCX',timeout=240)
            for stage in stages:
                getattr(suite,stage)()
            report['status']='INTEGRATION_CASES_PASS_REMAINDER_PENDING'
        except BaseException as error:
            report['status']='FAIL'
            report['failure']=repr(error)
            try:
                failure=vm.out/'failure.ppm'
                vm.hmp('screendump "'+failure.as_posix()+'"')
                report['screenshots'].append(png_from_ppm(failure))
            except (OSError,RuntimeError,TimeoutError) as screenshot_error:
                report['failure_screenshot_error']=repr(screenshot_error)
            raise
        finally:
            suite.persist()
            guest.close()
    try:
        audio = out/'audio.wav'
        with wave.open(str(audio),'rb') as stream:
            if stream.getsampwidth()!=2 or stream.getnchannels()!=2 or stream.getframerate()!=48000:
                raise AssertionError('真实QEMU音频格式异常')
            count=stream.getnframes()
            nonzero=False
            while block:=stream.readframes(48000):
                if any(block):
                    nonzero=True
            if not count or not nonzero:
                raise AssertionError('QEMU音频没有实际PCM')
        report['audio']=dict(path=str(audio),frames=count,sha256=sha(audio),nonzero=True,
                             scope='presence only; not every effect/MP3 acceptance')
    except BaseException as error:
        report['status']='FAIL'
        report['failure']=repr(error)
        raise
    finally:
        (out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return report


def main():
    parser=argparse.ArgumentParser(description='M9独立镜像副本的双盘Windows QEMU集成验收')
    parser.add_argument('--boot',type=Path,required=True)
    parser.add_argument('--symbols',type=Path,required=True,help='同批新内核kernel.sym，供真实COM1断点定位')
    parser.add_argument('--data',type=Path,required=True,action='append',help='至少两个分别生成的v5测试盘')
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    parser.add_argument('--stage',choices=STAGES,action='append',help='只重测指定阶段；报告明确范围，默认完整阶段')
    args=parser.parse_args()
    if os.name!='nt' or len(args.data)<2:
        parser.error('使用Windows Python，传入至少两个不同数据盘')
    if len(set(path.resolve() for path in args.data))!=len(args.data):
        parser.error('不能重复同一路径冒充双盘矩阵')
    if args.stage and len(set(args.stage))!=len(args.stage):
        parser.error('不能重复指定同一阶段')
    stages=tuple(stage for stage in STAGES if args.stage is None or stage in args.stage)
    symbols={}
    for line in args.symbols.read_text(encoding='ascii').splitlines():
        match=re.fullmatch(r'([0-9a-fA-F]+)\s+[tT]\s+(\w+)',line)
        if match:
            symbols[match[2]]=int(match[1],16)
    if not 0x10000<=symbols.get('rtc_snapshot',0)<0x100000:
        parser.error('新kernel.sym缺rtc_snapshot真实文本地址')
    args.out.mkdir(parents=True,exist_ok=False)
    result=dict(status='RUNNING',disks=[])
    destination=args.out/'matrix.json'
    try:
        for index,path in enumerate(args.data):
            result['disks'].append(verify_disk(args.boot,path,args.out/f'disk-{index+1}',args.accel,args.symbols,symbols,stages))
        result['status']='INTEGRATION_CASES_PASS_REMAINDER_PENDING'
    except BaseException as error:
        result.update(status='FAIL',failure=repr(error))
        raise
    finally:
        destination.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
