"""M9外部管理帧与停等客户端；此模块不接入guest的网络或本机UART。"""
import hashlib
from pathlib import Path
import os
import secrets
import struct
import threading
import time
import zlib


def frame(kind, sequence, payload=b''):
    if len(payload) > 512 or not 0 <= sequence <= 0xffffffff:
        raise ValueError('帧超出协议边界')
    body = struct.pack('<4sHHII', b'SC9\0', kind, 0, sequence, len(payload)) + payload
    body += struct.pack('<I', zlib.crc32(body))
    escaped = bytearray(b'\x7e')
    for value in body:
        if value in (0x7d, 0x7e):
            escaped.extend((0x7d, value ^ 0x20))
        else:
            escaped.append(value)
    escaped.append(0x7e)
    return bytes(escaped)


class Decoder:
    def __init__(self, consume):
        self.consume = consume
        self.buffer = bytearray()
        self.escape = self.discard = False

    def feed(self, data):
        for value in data:
            if value == 0x7e:
                if not self.discard and not self.escape:
                    self.finish()
                self.buffer.clear()
                self.escape = self.discard = False
            elif not self.discard:
                if value == 0x7d:
                    if self.escape:
                        self.discard = True
                    else:
                        self.escape = True
                    continue
                if self.escape:
                    value ^= 0x20
                    self.escape = False
                self.buffer.append(value)
                if len(self.buffer) > 532:
                    self.discard = True

    def finish(self):
        body = self.buffer
        if len(body) < 20:
            return
        magic, kind, flags, sequence, length = struct.unpack_from('<4sHHII', body)
        if magic != b'SC9\0' or flags or length > 512 or len(body) != length + 20:
            return
        if zlib.crc32(body[:-4]) != struct.unpack_from('<I', body, len(body)-4)[0]:
            return
        self.consume(kind, sequence, bytes(body[16:-4]))


class ManagementClient:
    def __init__(self, pipe, console, debug_paused=None):
        self.pipe = pipe
        self.console = console
        self.decoder = Decoder(self.consume)
        self.sequence = 0
        self.lock = threading.Lock()
        self.condition = threading.Condition()
        self.pending = None
        self.response = None
        self.failure = None
        self.debug_paused = debug_paused

    def consume(self, kind, sequence, payload):
        if kind == 0x100 and sequence == 0:
            self.console(payload)
            return
        with self.condition:
            if self.pending == (kind, sequence):
                self.response = payload
                self.condition.notify_all()

    def fail(self, error):
        with self.condition:
            self.failure = error
            self.condition.notify_all()

    def request(self, kind, payload=b'', timeout=3):
        with self.lock:
            if self.failure:
                raise RuntimeError('串口连接已中断') from self.failure
            if self.sequence == 0xffffffff:
                raise RuntimeError('序号耗尽，请重新连接')
            self.sequence += 1
            sequence = self.sequence
            encoded = frame(kind, sequence, payload)
            with self.condition:
                self.pending = (kind | 0x8000, sequence)
                self.response = None
            try:
                for attempt in range(3):
                    self.pipe.write(encoded)
                    remaining = timeout
                    checkpoint = time.monotonic()
                    was_paused = bool(self.debug_paused and self.debug_paused.is_set())
                    with self.condition:
                        while self.response is None and self.failure is None:
                            now = time.monotonic()
                            paused = bool(self.debug_paused and self.debug_paused.is_set())
                            # 真正的COM1 STOP会让整个内核停住，COM2 ACK要等
                            # cont才能发。仅外部调试暂停不计超时；继续后保留
                            # 剩余活动时间。失联/关机仍由fail唤醒，不无限重发。
                            if not was_paused and not paused:
                                remaining -= now-checkpoint
                            checkpoint, was_paused = now, paused
                            if remaining <= 0 and not paused:
                                break
                            self.condition.wait(.05 if paused else min(.05,remaining))
                        if self.failure:
                            raise RuntimeError('串口连接已中断') from self.failure
                        if self.response is not None:
                            if len(self.response) < 4:
                                raise RuntimeError('管理回复不完整')
                            result, = struct.unpack_from('<i', self.response)
                            return result, self.response[4:]
                raise TimeoutError(f'管理请求 {kind} 无有效回复')
            finally:
                with self.condition:
                    self.pending = None

    @staticmethod
    def path(value):
        path = value.encode('utf-8')
        if not path or len(path) > 63 or b'\0' in path:
            raise ValueError('客体路径必须为1..63个UTF-8字节')
        return path + b'\0'

    def checked(self, kind, payload=b''):
        result, body = self.request(kind, payload)
        if result < 0:
            raise RuntimeError(f'客体请求 {kind} 返回 {result}')
        return result, body

    def hello(self):
        _, body = self.checked(1, struct.pack('<I', 1))
        if body != struct.pack('<II', 1, 512):
            raise RuntimeError('管理版本或帧上限不兼容')

    def input(self, data):
        for offset in range(0, len(data), 512):
            block = data[offset:offset+512]
            for attempt in range(30):
                result, _ = self.request(2, block)
                if result == len(block):
                    break
                if result != -6:
                    raise RuntimeError(f'控制台输入返回 {result}')
                time.sleep(.05)
            else:
                raise TimeoutError('客体控制台输入持续满')

    def put(self, source, target, progress=None):
        source = Path(source)
        count = source.stat().st_size
        if count > 64 * 1024 * 1024:
            raise ValueError('文件超过SandFS容量上限')
        def advance(phase, done):
            if progress is not None:
                progress(phase, done, count)
        advance('hash', 0)
        digest = hashlib.sha256()
        hashed = 0
        with source.open('rb') as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
                hashed += len(block)
                advance('hash', hashed)
        self.checked(3, struct.pack('<I', count) + digest.digest() + self.path(target))
        try:
            offset = 0
            advance('transfer', offset)
            actual = hashlib.sha256()
            with source.open('rb') as stream:
                while block := stream.read(508):
                    result, body = self.checked(4, struct.pack('<I', offset) + block)
                    offset += len(block)
                    actual.update(block)
                    if result != len(block) or body != struct.pack('<I', offset):
                        raise RuntimeError('接收端偏移不一致')
                    # 只显示已校验ACK的正文，重发、帧头和散列预读不算传输量。
                    advance('transfer', offset)
            if offset != count or actual.digest() != digest.digest():
                raise RuntimeError('发送期间源文件被改动，放弃提交')
            advance('commit', offset)
            self.checked(5)
            advance('done', offset)
            return {'bytes': count, 'sha256': actual.hexdigest(), 'guest': target}
        except BaseException:
            try:
                self.request(6)
            except Exception:
                pass  # 清理失败不遮掉原始传输/校验错误，租约也会撤销事务。
            raise

    def get(self, source, destination, progress=None):
        destination = Path(destination)
        if destination.exists():
            raise FileExistsError(destination)
        if progress is not None:
            progress('prepare', 0, None)
        _, body = self.checked(7, self.path(source))
        if len(body) != 8:
            raise RuntimeError('读取快照不完整')
        count, generation = struct.unpack('<II', body)
        def advance(phase, done):
            if progress is not None:
                progress(phase, done, count)
        temporary = destination.with_name(destination.name + '.receiving-' + secrets.token_hex(6))
        digest = hashlib.sha256()
        try:
            with temporary.open('xb') as output:
                offset = 0
                advance('transfer', offset)
                while offset < count:
                    result, body = self.checked(8, struct.pack('<I', offset))
                    if result <= 0 or len(body) != result + 4 or struct.unpack_from('<I', body)[0] != offset:
                        raise RuntimeError('读取正文偏移或长度不一致')
                    output.write(body[4:])
                    digest.update(body[4:])
                    offset += result
                    if offset > count:
                        raise RuntimeError('读取越过快照结尾')
                    advance('transfer', offset)
                advance('verify', offset)
                _, expected = self.checked(9)
                if expected != digest.digest():
                    raise RuntimeError('取回文件SHA-256不一致')
                output.flush()
                os.fsync(output.fileno())
            # Windows rename在目标已存在时失败，避免竞态覆盖用户现有文件。
            temporary.rename(destination)
            advance('done', offset)
            return {'bytes': count, 'generation': generation, 'sha256': digest.hexdigest(), 'guest': source}
        except BaseException:
            try:
                self.request(6)
            except Exception:
                pass
            raise
        finally:
            if temporary.exists():
                temporary.unlink()
