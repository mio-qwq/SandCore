#!/usr/bin/env python3
"""SandCore 双机串口连接工具：宿主充当外部调试机，QEMU 模拟目标机。

双通道、SYSTEM管理帧/完整文件传输与COM1已实际运行；完整M9仍待验收。
管理端口不使用 TCP；VM 暂停到本机通道连接/
ACL 核对完成以后再启动。每次使用新副本，不挂入源盘。
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import getpass
import json
import msvcrt
import os
from pathlib import Path
import re
import secrets
import shlex
import shutil
import struct
import subprocess
import sys
import threading
import time
from serial_protocol import ManagementClient
from serial_progress import TransferProgress

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        while block := stream.read(1024 * 1024):
            h.update(block)
    return h.hexdigest()


class WindowsPipe:
    """只打开本机随机命名管道，拒绝网络身份，核对实际服务端进程。

    使用 overlapped IO，使客体暂停/关闭时的发送可超时取消，不能让
    主工具堵在同步 WriteFile。缓冲/OVERLAPPED 保留到取消完成。
    """
    class Overlapped(C.Structure):
        _fields_ = [('internal', C.c_size_t), ('internal_high', C.c_size_t),
                    ('offset', W.DWORD), ('offset_high', W.DWORD),
                    ('event', W.HANDLE)]

    def __init__(self, name, server_pid, timeout=15):
        if os.name != 'nt':
            raise RuntimeError('本机管道后端需要 Windows Python')
        if not re.fullmatch(r'sandcore-[0-9a-f]{32}-(debug|management|qmp)', name):
            raise ValueError('只允许本机 SandCore 随机会话管道名')
        self.k = C.WinDLL('kernel32', use_last_error=True)
        self.a = C.WinDLL('advapi32', use_last_error=True)
        self._bind()
        self.handle = None
        endpoint = '\\\\.\\pipe\\' + name
        deadline = time.monotonic() + timeout
        while True:
            handle = self.k.CreateFileW(endpoint, 0xC0060000, 0, None, 3,
                                        0x40000000, None)
            if handle != C.c_void_p(-1).value:
                self.handle = handle
                break
            error = C.get_last_error()
            if error not in (2, 231) or time.monotonic() >= deadline:
                raise OSError(error, f'连接本机管道失败：{name}')
            time.sleep(.01)
        try:
            pid = W.ULONG()
            self._check(self.k.GetNamedPipeServerProcessId(self.handle, C.byref(pid)))
            if pid.value != server_pid:
                raise RuntimeError(f'管道服务端不是本次 QEMU：{pid.value}')
            self.server_pid = pid.value
            self.acl = self._harden()
        except BaseException:
            self.close()
            raise

    def _bind(self):
        # 明确指针宽度，避免64位Python把HANDLE截断成默认int。
        signatures = {
            'CreateFileW': ([W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p,
                             W.DWORD, W.DWORD, W.HANDLE], W.HANDLE),
            'CloseHandle': ([W.HANDLE], W.BOOL),
            'CreateEventW': ([C.c_void_p, W.BOOL, W.BOOL, W.LPCWSTR], W.HANDLE),
            'ReadFile': ([W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD),
                          C.POINTER(self.Overlapped)], W.BOOL),
            'WriteFile': ([W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD),
                           C.POINTER(self.Overlapped)], W.BOOL),
            'GetOverlappedResult': ([W.HANDLE, C.POINTER(self.Overlapped),
                                     C.POINTER(W.DWORD), W.BOOL], W.BOOL),
            'WaitForSingleObject': ([W.HANDLE, W.DWORD], W.DWORD),
            'CancelIoEx': ([W.HANDLE, C.POINTER(self.Overlapped)], W.BOOL),
            'PeekNamedPipe': ([W.HANDLE, C.c_void_p, W.DWORD, C.c_void_p,
                               C.POINTER(W.DWORD), C.c_void_p], W.BOOL),
            'GetNamedPipeServerProcessId': ([W.HANDLE, C.POINTER(W.ULONG)], W.BOOL),
            'GetCurrentProcess': ([], W.HANDLE),
            'LocalFree': ([C.c_void_p], C.c_void_p),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.k, name)
            fn.argtypes, fn.restype = args, result
        signatures = {
            'OpenProcessToken': ([W.HANDLE, W.DWORD, C.POINTER(W.HANDLE)], W.BOOL),
            'GetTokenInformation': ([W.HANDLE, C.c_int, C.c_void_p, W.DWORD,
                                     C.POINTER(W.DWORD)], W.BOOL),
            'ConvertSidToStringSidW': ([C.c_void_p, C.POINTER(W.LPWSTR)], W.BOOL),
            'ConvertStringSecurityDescriptorToSecurityDescriptorW':
                ([W.LPCWSTR, W.DWORD, C.POINTER(C.c_void_p), C.c_void_p], W.BOOL),
            'GetSecurityDescriptorDacl': ([C.c_void_p, C.POINTER(W.BOOL),
                                           C.POINTER(C.c_void_p), C.POINTER(W.BOOL)], W.BOOL),
            'GetSecurityDescriptorControl': ([C.c_void_p, C.POINTER(W.WORD),
                                              C.POINTER(W.DWORD)], W.BOOL),
            'IsValidAcl': ([C.c_void_p], W.BOOL),
            'GetAce': ([C.c_void_p, W.DWORD, C.POINTER(C.c_void_p)], W.BOOL),
            'SetSecurityInfo': ([W.HANDLE, C.c_int, W.DWORD, C.c_void_p,
                                 C.c_void_p, C.c_void_p, C.c_void_p], W.DWORD),
            'GetSecurityInfo': ([W.HANDLE, C.c_int, W.DWORD, C.c_void_p,
                                 C.c_void_p, C.c_void_p, C.c_void_p,
                                 C.POINTER(C.c_void_p)], W.DWORD),
            'ConvertSecurityDescriptorToStringSecurityDescriptorW':
                ([C.c_void_p, W.DWORD, W.DWORD, C.POINTER(W.LPWSTR), C.c_void_p], W.BOOL),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.a, name)
            fn.argtypes, fn.restype = args, result

    @staticmethod
    def _check(ok):
        if not ok:
            raise C.WinError(C.get_last_error())

    def _current_sid(self):
        token = W.HANDLE()
        self._check(self.a.OpenProcessToken(self.k.GetCurrentProcess(), 8, C.byref(token)))
        try:
            size = W.DWORD()
            self.a.GetTokenInformation(token, 1, None, 0, C.byref(size))
            if not size.value:
                raise C.WinError(C.get_last_error())
            data = C.create_string_buffer(size.value)
            self._check(self.a.GetTokenInformation(token, 1, data, size, C.byref(size)))
            sid = C.cast(data, C.POINTER(C.c_void_p))[0]
            result = W.LPWSTR()
            self._check(self.a.ConvertSidToStringSidW(sid, C.byref(result)))
            try:
                return result.value
            finally:
                self.k.LocalFree(C.cast(result, C.c_void_p))
        finally:
            self.k.CloseHandle(token)

    def _harden(self):
        # NU是NETWORK身份。显式拒绝它，即使远程连接者是相同账户；
        # 其余仅当前宿主账户与宿主SYSTEM。客体在此成功前始终暂停。
        sid = self._current_sid()
        desired = f'D:P(D;;GA;;;NU)(A;;GA;;;{sid})(A;;GA;;;SY)'
        descriptor = C.c_void_p()
        self._check(self.a.ConvertStringSecurityDescriptorToSecurityDescriptorW(
            desired, 1, C.byref(descriptor), None))
        try:
            present, defaulted, acl = W.BOOL(), W.BOOL(), C.c_void_p()
            self._check(self.a.GetSecurityDescriptorDacl(descriptor, C.byref(present),
                                                         C.byref(acl), C.byref(defaulted)))
            if not present.value:
                raise RuntimeError('拒绝空DACL')
            error = self.a.SetSecurityInfo(self.handle, 6, 0x80000004,
                                           None, None, acl, None)
            if error:
                raise C.WinError(error)
        finally:
            self.k.LocalFree(descriptor)
        # 回读对象实际ACL，而不是把期望字符串当成已经生效的证据。
        actual, text = C.c_void_p(), W.LPWSTR()
        error = self.a.GetSecurityInfo(self.handle, 6, 4, None, None, None,
                                       None, C.byref(actual))
        if error:
            raise C.WinError(error)
        try:
            self._verify_acl(actual, sid)
            self._check(self.a.ConvertSecurityDescriptorToStringSecurityDescriptorW(
                actual, 1, 4, C.byref(text), None))
            try:
                result = text.value
            finally:
                self.k.LocalFree(C.cast(text, C.c_void_p))
        finally:
            self.k.LocalFree(actual)
        return result

    def _verify_acl(self, descriptor, current_sid):
        # Windows会把GA展开为FA，把管理员SID打印为LA；SDDL文本不是
        # 稳定的权限表示。逐ACE核对实际SID/掩码/顺序，不能因别名放宽ACL。
        control, revision = W.WORD(), W.DWORD()
        self._check(self.a.GetSecurityDescriptorControl(descriptor, C.byref(control),
                                                        C.byref(revision)))
        present, defaulted, acl = W.BOOL(), W.BOOL(), C.c_void_p()
        self._check(self.a.GetSecurityDescriptorDacl(descriptor, C.byref(present),
                                                     C.byref(acl), C.byref(defaulted)))
        if not control.value & 0x1000 or not present.value or not acl.value:
            raise RuntimeError('本机管道DACL未保护、缺失或为空')
        self._check(self.a.IsValidAcl(acl))
        ace_count = struct.unpack_from('<H', C.string_at(acl, 8), 4)[0]
        expected = [(1, 'S-1-5-2'), (0, current_sid), (0, 'S-1-5-18')]
        if ace_count != len(expected):
            raise RuntimeError('本机管道含缺失或额外的权限条目')
        for index, (kind, identity) in enumerate(expected):
            ace = C.c_void_p()
            self._check(self.a.GetAce(acl, index, C.byref(ace)))
            actual_kind, flags, size, mask = struct.unpack('<BBHI', C.string_at(ace, 8))
            if actual_kind != kind or flags or size < 20 or mask not in (0x10000000, 0x1F01FF):
                raise RuntimeError('本机管道ACE类型、权限或继承状态不符')
            text = W.LPWSTR()
            self._check(self.a.ConvertSidToStringSidW(C.c_void_p(ace.value+8), C.byref(text)))
            try:
                if text.value != identity:
                    raise RuntimeError('本机管道ACE身份不符')
            finally:
                self.k.LocalFree(C.cast(text, C.c_void_p))

    def _io(self, operation, data, size, timeout=3):
        event = self.k.CreateEventW(None, True, False, None)
        self._check(event)
        ov, count = self.Overlapped(), W.DWORD()
        ov.event = event
        try:
            ok = operation(self.handle, data, size, C.byref(count), C.byref(ov))
            if not ok:
                error = C.get_last_error()
                if error != 997:
                    raise C.WinError(error)
                wait = self.k.WaitForSingleObject(event, int(timeout * 1000))
                if wait != 0:
                    self.k.CancelIoEx(self.handle, C.byref(ov))
                    # Windows取消是异步的，释放buffer/事件前必须等取消完成。
                    self.k.GetOverlappedResult(self.handle, C.byref(ov), C.byref(count), True)
                    raise TimeoutError('本机串口IO超时，已取消')
                self._check(self.k.GetOverlappedResult(self.handle, C.byref(ov),
                                                       C.byref(count), False))
            return count.value
        finally:
            self.k.CloseHandle(event)

    def read(self):
        available = W.DWORD()
        self._check(self.k.PeekNamedPipe(self.handle, None, 0, None,
                                         C.byref(available), None))
        if not available.value:
            return b''
        buffer = C.create_string_buffer(min(65536, available.value))
        count = self._io(self.k.ReadFile, buffer, len(buffer))
        return buffer.raw[:count]

    def write(self, data):
        if not data:
            return
        buffer = C.create_string_buffer(data)
        sent = 0
        while sent < len(data):
            count = self._io(self.k.WriteFile, C.byref(buffer, sent), len(data) - sent)
            if not count:
                raise EOFError('本机串口连接已关闭')
            sent += count

    def close(self):
        if self.handle is not None:
            self.k.CloseHandle(self.handle)
            self.handle = None


class StdioQmp:
    """父子进程专用匿名QMP管道，没有可连接名称或网络监听端点。"""
    def __init__(self, process):
        self.process = process
        self.kernel = C.WinDLL('kernel32', use_last_error=True)
        self.kernel.PeekNamedPipe.argtypes = [W.HANDLE, C.c_void_p, W.DWORD,
                                              C.c_void_p, C.POINTER(W.DWORD), C.c_void_p]
        self.kernel.PeekNamedPipe.restype = W.BOOL

    def read(self):
        count = W.DWORD()
        handle = msvcrt.get_osfhandle(self.process.stdout.fileno())
        if not self.kernel.PeekNamedPipe(handle, None, 0, None, C.byref(count), None):
            raise C.WinError(C.get_last_error())
        return self.process.stdout.read(min(count.value, 65536)) if count.value else b''

    def write(self, data):
        # stop-and-wait每次只有一条控制请求，限制到单个匿名管道缓冲以内，
        # 不因异常大HMP文本在未启动的子进程上同步堵住。UART仍是overlapped。
        if len(data) > 4096:
            raise ValueError('QMP请求超过4096B控制容量')
        while data:
            sent = self.process.stdin.write(data)
            if not sent:
                raise EOFError('匿名QMP管道已关闭')
            data = data[sent:]

    def close(self):
        self.process.stdin.close()
        self.process.stdout.close()


class QemuSession:
    """拥有自己启动的VM及副本；错误时只关闭这一个进程。"""
    def __init__(self, boot, data, out, accel='tcg', headless=False, audio=True, cpu='qemu32', no_shutdown=False):
        profiles=('qemu32','qemu32,-sse2','qemu32,-fxsr,-sse,-sse2','qemu32,-fpu,-fxsr,-sse,-sse2')
        if cpu not in profiles:
            raise ValueError('CPU只允许固定兼容回退测试配置')
        self.out = Path(out).resolve()
        self.out.mkdir(parents=True, exist_ok=False)
        self.pipes, self.process, self.log = {}, None, None
        self.audio_capture_id = None
        self.qmp_buffer, self.events, self.sequence = b'', [], 0
        self.report = dict(scope='M9_EXTERNAL_SERIAL_TRANSPORT', sources={}, acl={}, qmp_trace=[])
        try:
            for label, source in [('sandcore', boot), ('sanddata', data)]:
                source = Path(source).resolve(strict=True)
                destination = self.out / (label + '.img')
                digest = sha(source)
                shutil.copy2(source, destination)
                if sha(destination) != digest:
                    raise RuntimeError('镜像副本校验失败')
                self.report['sources'][label] = dict(path=str(source), sha256=digest,
                                                       copy=str(destination))
            import qemu_config
            command, selected = qemu_config.prefix(accel)
            if command.count('-cpu')!=1 or command[command.index('-cpu')+1]!='qemu32':
                raise RuntimeError('QEMU默认CPU参数布局变化')
            command[command.index('-cpu')+1]=cpu
            self.report['cpu_profile']=cpu
            name = 'sandcore-' + secrets.token_hex(16)
            # 每次启动的新强熵只经客体外部fw_cfg交给内核，不写入SandFS，
            # 不记录种子/令牌到报告。普通客体进程没有fw_cfg端口权限。
            entropy = self.out / 'entropy.bin'
            entropy.write_bytes(secrets.token_bytes(32))
            names = {role: name + '-' + role for role in ('debug', 'management')}
            command += ['-m', '128', '-vga', 'std', '-boot', 'a', '-S',
                        '-monitor', 'none', '-parallel', 'none', '-nic', 'none', '-rtc', 'base=utc',
                        '-fw_cfg', f'name=opt/sandcore/entropy,file={entropy}',
                        '-drive', f'format=raw,if=floppy,file={self.out / "sandcore.img"}',
                        '-drive', f'format=raw,if=ide,file={self.out / "sanddata.img"}']
            for role in ('debug', 'management'):
                command += ['-chardev', f'pipe,id={role},path={names[role]}',
                            '-serial', f'chardev:{role}']
            # 当前Windows QEMU的named-pipe QMP在cont停滞，已由无盘VM复现。
            # stdio匿名管道五步握手实际通过；只继承到本次子进程，无TCP/名称。
            command += ['-qmp', 'stdio']
            # 电源验收保留真实SHUTDOWN事件和停机现场，之后显式结束
            # 音频捕获；缺省交互会话仍按原来的客体关机自动退出。
            if no_shutdown:
                command.append('-no-shutdown')
            self.report['no_shutdown']=bool(no_shutdown)
            # 首个音频目标明确AC97；无头验收保存真实DMA输出WAV，
            # 有窗会话使用Windows DirectSound，不将空后端声称可听。
            if audio:
                command += ['-device', 'AC97,audiodev=sand-audio']
                command += ['-audiodev', 'none,id=sand-audio,out.frequency=48000,out.channels=2,out.format=s16' if headless
                            else 'dsound,id=sand-audio']
            self.report['audio_enabled'] = audio
            if headless:
                command += ['-display', 'none']
            self.report.update(command=command, accelerator=selected, pipe_names=names)
            self.log = (self.out / 'qemu.log').open('wb')
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                             stderr=self.log, bufsize=0, close_fds=True,
                                             creationflags=subprocess.CREATE_NO_WINDOW)
            self.report['pid'] = self.process.pid
            # QEMU Windows pipe初始化逐个等待客户端，必须按创建顺序连接。
            for role in ('debug', 'management'):
                self.pipes[role] = WindowsPipe(names[role], self.process.pid)
                self.report['acl'][role] = self.pipes[role].acl
            self.pipes['qmp'] = StdioQmp(self.process)
            self.report['qmp_transport'] = 'ANONYMOUS_PARENT_CHILD_ONLY'
            greeting = self._qmp_message()
            if 'QMP' not in greeting:
                raise RuntimeError('缺少真实QMP greeting')
            self.report['qmp_greeting'] = greeting
            self.qmp('qmp_capabilities')
            status = self.qmp('query-status')
            if status['running']:
                raise RuntimeError('ACL核对前客体不应已经运行')
            self.report['before_cont'] = status
            if audio and headless:
                # 当前Windows版本wav后端退出0仍留下长度为0的RIFF头。
                # 使用QEMU真实混音捕获并显式结束；不改录制字节来伪造WAV。
                path=(self.out/'audio.wav').as_posix()
                result=self.audio_hmp(f'wavcapture "{path}" sand-audio 48000 16 2')
                if result.strip():
                    raise RuntimeError('QEMU启动音频捕获失败: '+result)
                capture=self.audio_hmp('info capture')
                match=re.fullmatch(r'\[([0-9]+)\]: Capturing audio\(48000,16,2\) to '+re.escape(path)+r': [0-9]+ bytes',capture.strip())
                if not match:
                    raise RuntimeError('QEMU音频捕获状态不符: '+capture)
                self.audio_capture_id=int(match[1])
                self.report['audio_capture']=dict(transport='QEMU_MIXER_WAVCAPTURE',index=self.audio_capture_id,
                                                   info=capture,host_audible=False)
            self.qmp('cont')
            self.report['status'] = 'TRANSPORT_CONNECTED'
            self.persist()
        except BaseException:
            self.report['status'] = 'START_FAILED'
            # 初始化失败时客体还没有上层读取器。保留真实UART启动字节，
            # 避免把QMP/后端问题误认成内核启动完成或没有输出。
            for role in ('debug', 'management'):
                if role in self.pipes:
                    try:
                        (self.out / (role+'-startup.bin')).write_bytes(self.pipes[role].read())
                    except OSError:
                        pass
            self.close()
            raise

    def _qmp_message(self, timeout=15):
        deadline = time.monotonic() + timeout
        while b'\n' not in self.qmp_buffer:
            self.qmp_buffer += self.pipes['qmp'].read()
            if len(self.qmp_buffer) > 4 * 1024 * 1024:
                raise RuntimeError('QMP消息超出上限')
            if time.monotonic() >= deadline:
                raise TimeoutError('等待QMP响应超时')
            if self.process.poll() is not None:
                raise RuntimeError('QEMU已退出')
            time.sleep(.005)
        line, self.qmp_buffer = self.qmp_buffer.split(b'\n', 1)
        message = json.loads(line)
        self.report['qmp_trace'].append({'receive': message})
        return message

    def qmp(self, execute, arguments=None):
        self.sequence += 1
        request = dict(execute=execute, id=self.sequence)
        if arguments is not None:
            request['arguments'] = arguments
        self.report['qmp_trace'].append({'send': request})
        self.pipes['qmp'].write((json.dumps(request) + '\n').encode('utf-8'))
        while True:
            reply = self._qmp_message()
            if reply.get('id') == self.sequence:
                if 'error' in reply:
                    raise RuntimeError(reply['error'])
                return reply.get('return')
            self.events.append(reply)

    def hmp(self, command):
        return self.qmp('human-monitor-command', {'command-line': command})

    def audio_hmp(self, command):
        # 此安装版本仍支持捕获命令，但会打印明确的弃用提示；完整原文
        # 保留在QMP trace。只滤此已知提示，错误正文与实际状态仍严格检查。
        result=self.hmp(command)
        return '\n'.join(line for line in result.splitlines()
                         if not re.fullmatch(r"warning: '(wavcapture|stopcapture|info capture)' is deprecated since v[0-9.]+, to be removed",line))

    def persist(self):
        (self.out / 'session.json').write_text(
            json.dumps(self.report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

    def close(self):
        if self.process is not None and self.process.poll() is None:
            if self.audio_capture_id is not None:
                try:
                    result=self.audio_hmp('stopcapture '+str(self.audio_capture_id))
                    remaining=self.audio_hmp('info capture')
                    if result.strip() or 'Capturing audio(' in remaining:
                        raise RuntimeError('QEMU音频捕获未正确结束: '+result+remaining)
                    self.report['audio_capture']['finalized']=True
                    self.audio_capture_id=None
                except (OSError,RuntimeError,TimeoutError,EOFError) as error:
                    self.report['audio_capture_error']=repr(error)
            try:
                if 'qmp' in self.pipes:
                    self.qmp('quit')
            except (OSError, RuntimeError, TimeoutError, EOFError):
                pass
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=3)
        for pipe in self.pipes.values():
            pipe.close()
        self.pipes.clear()
        if self.log is not None:
            self.log.close()
            self.log = None
        if self.process is not None:
            self.report['exit_code'] = self.process.poll()
        self.report['source_unchanged'] = {
            label: sha(item['path']) == item['sha256']
            for label, item in self.report['sources'].items()}
        self.persist()

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()


def raw_console(client, debug_paused, stop, sender=None):
    """现有外部管理连接的逐键模式，Ctrl-]返回宿主命令菜单。

    不借TCP或新建客体端串口。直接读取Windows控制台键事件，禁用
    本机行缓冲/回显/处理Ctrl-C，避免nano快捷键被宿主吞掉。
    """
    if os.name != 'nt' or not sys.stdin.isatty():
        raise RuntimeError(':raw需要Windows交互控制台')
    send=sender or client.input
    class Key(C.Structure):
        _fields_ = [('down', W.BOOL), ('repeat', W.WORD),
                    ('virtual', W.WORD), ('scan', W.WORD),
                    ('character', W.WCHAR), ('control', W.DWORD)]
    class EventData(C.Union):
        _fields_ = [('key', Key), ('padding', W.DWORD * 4)]
    class Event(C.Structure):
        _fields_ = [('kind', W.WORD), ('data', EventData)]
    kernel = C.WinDLL('kernel32', use_last_error=True)
    kernel.GetStdHandle.argtypes = [W.DWORD]
    kernel.GetStdHandle.restype = W.HANDLE
    kernel.GetConsoleMode.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
    kernel.GetConsoleMode.restype = W.BOOL
    kernel.SetConsoleMode.argtypes = [W.HANDLE, W.DWORD]
    kernel.SetConsoleMode.restype = W.BOOL
    kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
    kernel.WaitForSingleObject.restype = W.DWORD
    kernel.ReadConsoleInputW.argtypes = [W.HANDLE, C.POINTER(Event), W.DWORD, C.POINTER(W.DWORD)]
    kernel.ReadConsoleInputW.restype = W.BOOL
    source = kernel.GetStdHandle(W.DWORD(-10).value)
    target = kernel.GetStdHandle(W.DWORD(-11).value)
    original, output_mode = W.DWORD(), W.DWORD()
    if not kernel.GetConsoleMode(source, C.byref(original)):
        raise C.WinError(C.get_last_error())
    have_output = bool(kernel.GetConsoleMode(target, C.byref(output_mode)))
    if have_output and not kernel.SetConsoleMode(target, output_mode.value | 4):
        raise C.WinError(C.get_last_error())
    # QuickEdit暂停控制台时会阻断调试；扩展位启用后明确关闭它。
    if not kernel.SetConsoleMode(source, (original.value | 0x80) & ~(1 | 2 | 4 | 0x40)):
        if have_output:
            kernel.SetConsoleMode(target, output_mode.value)
        raise C.WinError(C.get_last_error())
    specials = {0x25: b'\x1b[D', 0x26: b'\x1b[A', 0x27: b'\x1b[C', 0x28: b'\x1b[B',
                0x24: b'\x1b[H', 0x23: b'\x1b[F', 0x21: b'\x1b[5~', 0x22: b'\x1b[6~',
                0x2e: b'\x1b[3~', 0x70: b'\x07', 0x71: b'\x0f', 0x72: b'\x17',
                0x73: b'\x18', 0x75: b'\x0b', 0x76: b'\x15', 0x77: b'\x07',
                0x78: b'\x14', 0x79: b'\x18'}
    lead = None
    print('[逐键串口：Ctrl-]返回菜单；nano ^O保存/^X退出]', flush=True)
    try:
        while not stop.is_set():
            wait = kernel.WaitForSingleObject(source, 20)
            if wait == 258:
                continue
            if wait != 0:
                raise C.WinError(C.get_last_error())
            events, count = (Event * 16)(), W.DWORD()
            if not kernel.ReadConsoleInputW(source, events, 16, C.byref(count)):
                raise C.WinError(C.get_last_error())
            pending = bytearray()
            for event in events[:count.value]:
                if event.kind != 1 or not event.data.key.down:
                    continue
                key = event.data.key
                scalar = ord(key.character) if key.character else 0
                if scalar == 29 or (key.virtual == 0xdd and key.control & 0xc):
                    if pending:
                        send(bytes(pending))
                    return
                if debug_paused.is_set():
                    print('\nCOM1已暂停客体；Ctrl-]返回并切:debug后cont。', flush=True)
                    return
                if 0xd800 <= scalar <= 0xdbff:
                    lead = scalar
                    continue
                if 0xdc00 <= scalar <= 0xdfff:
                    scalar = 0x10000 + ((lead - 0xd800) << 10) + scalar - 0xdc00 if lead is not None else 0xfffd
                    lead = None
                elif lead is not None:
                    pending.extend('\ufffd'.encode('utf-8'))
                    lead = None
                data = specials.get(key.virtual)
                if data is None and scalar:
                    if scalar == 13:
                        scalar = 10
                    data = chr(scalar).encode('utf-8')
                    # 左Alt为Meta快捷键；AltGr的右Alt+Ctrl不添加Esc。
                    if key.control & 2 and not key.control & 1:
                        data = b'\x1b' + data
                if data:
                    pending.extend(data * min(key.repeat, 64))
                    # 重复键/粘贴成批传输，避免每字符一次ACK往返。
                    while len(pending) >= 512:
                        if send(bytes(pending[:512])) is False:
                            return
                        del pending[:512]
            if pending:
                if send(bytes(pending)) is False:
                    return
    finally:
        kernel.SetConsoleMode(source, original.value)
        if have_output:
            kernel.SetConsoleMode(target, output_mode.value)


def main():
    parser = argparse.ArgumentParser(description='SandCore 外部双串口连接工具（Windows QEMU）')
    parser.add_argument('--boot', required=True, help='启动镜像，运行时使用新副本')
    parser.add_argument('--data', required=True, help='数据镜像，运行时使用新副本')
    parser.add_argument('--out', required=True, help='本次新会话目录，不可已存在')
    parser.add_argument('--accel', choices=['tcg', 'whpx'], default='tcg')
    parser.add_argument('--headless', action='store_true', help='无头验收；缺省显示QEMU窗口')
    parser.add_argument('--debug-only', action='store_true', help='只用COM1调试；无需数据盘Shell能启动')
    args = parser.parse_args()
    selected, stop, errors = ['management'], threading.Event(), []
    if args.debug_only:
        selected[0]='debug'
    connected=not args.debug_only
    with QemuSession(args.boot, args.data, args.out, args.accel, args.headless) as vm:
        logs = {role: (vm.out / (role + '.bin')).open('wb')
                for role in ('debug', 'management')}
        output_lock = threading.RLock()
        progress_display = [None]
        def display(data):
            with output_lock:
                if progress_display[0] is not None:
                    progress_display[0].external(data)
                else:
                    sys.stdout.buffer.write(data)
                    sys.stdout.buffer.flush()
        def console(data):
            if selected[0] == 'management':
                display(data)
        debug_paused = threading.Event()
        debug_ready = threading.Event()
        client = ManagementClient(vm.pipes['management'], console, debug_paused)
        debug_tail = bytearray()
        pending = [None]

        def management(operation):
            # 平常保持逐条命令顺序；若执行期间命中COM1断点，就把
            # 等ACK的事务留给工作线程，前台立即恢复宿主菜单，可cont。
            # 同时只保留一项，避免粘贴/大传输积压和重复提交。
            previous=pending[0]
            if previous is not None:
                if previous['thread'].is_alive():
                    print('上一项管理请求尚未完成；可切:debug继续客体。',flush=True)
                    return False
                pending[0]=None
            state=dict(errors=[])
            def execute():
                try:
                    operation()
                except (OSError,ValueError,RuntimeError,TimeoutError) as error:
                    state['errors'].append(error)
                    print(f'管理操作失败: {error}',flush=True)
            state['thread']=threading.Thread(target=execute,name='SandCore-management-command')
            pending[0]=state
            state['thread'].start()
            while state['thread'].is_alive() and not stop.is_set():
                if debug_paused.is_set():
                    print('COM1断点已暂停；管理ACK等待cont，输入:debug继续调试。',flush=True)
                    return False
                state['thread'].join(timeout=.05)
            pending[0]=None
            return not state['errors']

        def pump():
            try:
                while not stop.is_set():
                    for role, log in logs.items():
                        data = vm.pipes[role].read()
                        if data:
                            log.write(data)
                            log.flush()
                            if role == 'management':
                                client.decoder.feed(data)
                            else:
                                debug_tail.extend(data)
                                # STOP后完整现场常超过256B，不能裁掉行首后
                                # 再搜索，否则恰好一次读完整现场就漏掉暂停。
                                # 逐完整行更新状态，未完行跨read保留。
                                while b'\n' in debug_tail:
                                    line,_,rest=debug_tail.partition(b'\n')
                                    debug_tail[:]=rest
                                    if line.rstrip(b'\r')==b'SandCore M9 | debug transport ready':
                                        debug_ready.set()
                                    if line.startswith(b'STOP '):
                                        debug_paused.set()
                                        if selected[0]=='management':
                                            display(line+b'\nCOM1 paused; :debug / cont to resume transfer.\n')
                                    elif line.startswith(b'OK resume'):
                                        debug_paused.clear()
                                if len(debug_tail)>2048:
                                    raise RuntimeError('COM1未结束调试行超过2048B')
                                if role == selected[0]:
                                    display(data)
                    stop.wait(.01)
            except BaseException as error:
                errors.append(error)
                client.fail(error)
                stop.set()

        reader = threading.Thread(target=pump, name='SandCore-serial-reader')
        reader.start()
        def heartbeat():
            try:
                while not stop.wait(5):
                    if debug_paused.is_set():
                        continue
                    try:
                        client.checked(10)
                    except TimeoutError:
                        if debug_paused.is_set():
                            continue
                        raise
            except BaseException as error:
                if not stop.is_set():
                    errors.append(error)
                    client.fail(error)
                    stop.set()
        heart = None
        try:
            if connected:
                client.hello()
                heart = threading.Thread(target=heartbeat, name='SandCore-serial-heartbeat')
                heart.start()
            else:
                # QEMU cont返回不等于客体UART已初始化；过早发送的首行
                # 会被BIOS/serial_init清掉，必须等真实客体就绪标记。
                if not debug_ready.wait(60):
                    if errors:
                        raise errors[0]
                    raise TimeoutError('客体COM1未在60秒内报告传输就绪')
                print('COM1独立调试模式；没有创建COM2 SYSTEM Shell/管理会话。',flush=True)
            print('SandCore 外部串口 | :put / :get / :raw / :debug / :management / :interrupt / :eof / :secret / :hmp / :quit', flush=True)
            for line in sys.stdin:
                if stop.is_set():
                    break
                command = line.rstrip('\r\n')
                if command == ':quit':
                    break
                if command in (':debug', ':management'):
                    if command==':management' and not connected:
                        print('独立调试模式不连接COM2；修好后按默认模式重新启动。',flush=True)
                        continue
                    selected[0] = command[1:]
                    print(f'[{selected[0]}]', flush=True)
                elif command.startswith(':hmp '):
                    print(vm.hmp(command[5:]), flush=True)
                elif selected[0] == 'management' and debug_paused.is_set():
                    print('COM1已暂停客体；先切:debug并cont，管理传输需要客体继续执行。', flush=True)
                elif command == ':raw':
                    if selected[0] != 'management':
                        print(':raw用于management终端；COM1保持命令模式。', flush=True)
                    else:
                        # 控制台读取仍由前台独占；只把输入ACK交给线程。
                        # 遇STOP先退出raw并恢复ConsoleMode，再读宿主命令，
                        # 不能让ReadConsoleInput和stdin行读取争抢同一控制台。
                        try:
                            raw_console(client,debug_paused,stop,
                                        lambda data:management(lambda data=data:client.input(data)))
                        except (OSError,RuntimeError,TimeoutError) as error:
                            print(f'逐键串口失败: {error}',flush=True)
                        print('\n[宿主命令菜单]', flush=True)
                elif command in (':interrupt', ':eof', ':secret'):
                    if selected[0] != 'management':
                        print('终端控制命令仅用于management通道。', flush=True)
                    elif command == ':secret':
                        password = getpass.getpass('输入将交给客体密码提示: ')
                        secret_data=(password+'\n').encode('utf-8')
                        management(lambda data=secret_data:client.input(data))
                        del password
                        del secret_data
                    else:
                        control_data=b'\x03' if command==':interrupt' else b'\x04'
                        management(lambda data=control_data:client.input(data))
                elif command.startswith((':put ', ':get ')):
                    if not connected:
                        print('未建立COM2管理会话；独立COM1模式不传文件。',flush=True)
                        continue
                    # POSIX引号用于两端参数；Windows路径可使用正斜杠或引号。
                    try:
                        words = shlex.split(command)
                        if len(words) != 3:
                            raise ValueError(':put 本机文件 客体路径；:get 客体路径 本机新文件')
                        operation, source, target = words
                        def transfer(operation=operation,source=source,target=target):
                            progress = TransferProgress(operation[1:].upper(),source,lock=output_lock)
                            progress_display[0] = progress
                            try:
                                result=client.put(source,target,progress) if operation==':put' else client.get(source,target,progress)
                            except BaseException:
                                progress.fail()
                                raise
                            finally:
                                progress_display[0] = None
                            print(json.dumps(result,ensure_ascii=False),flush=True)
                        management(transfer)
                    except (ValueError, OSError, RuntimeError) as error:
                        print(f'文件传输失败: {error}', flush=True)
                else:
                    data = (command + '\n').encode('utf-8')
                    if selected[0] == 'management':
                        management(lambda data=data:client.input(data))
                    else:
                        vm.pipes['debug'].write(data)
        except KeyboardInterrupt:
            pass
        finally:
            stop.set()
            client.fail(RuntimeError('本次宿主会话结束'))
            if pending[0] is not None:
                pending[0]['thread'].join(timeout=5)
            if heart is not None:
                heart.join(timeout=5)
            reader.join(timeout=5)
            for log in logs.values():
                log.close()
        if errors:
            raise errors[0]


if __name__ == '__main__':
    main()
