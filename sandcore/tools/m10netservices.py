#!/usr/bin/env python3
"""M10网络行为的原创宿主对端，标准库调用，不移植进客体。

所有监听固定127.0.0.1、内核分配端口；不会启用系统服务、修改
防火墙或连接公网。正文、HTTP坏帧和TFTP重复/丢包可独立复核。
"""
import hashlib
import json
from pathlib import Path
import socket
import socketserver
import struct
import threading
import time


class OwnedHandlers:
    def process_request_thread(self, request, client_address):
        thread = threading.current_thread()
        with self.service.lock:
            self.service.handlers[thread] = request
        try:
            if not self.service.stop.is_set():
                super().process_request_thread(request, client_address)
            else:
                self.shutdown_request(request)
        finally:
            with self.service.lock:
                self.service.handlers.pop(thread, None)

    def handle_error(self, request, client_address):
        import sys
        self.service.error(sys.exc_info()[1])


class TcpServer(OwnedHandlers, socketserver.ThreadingTCPServer):
    daemon_threads = False
    allow_reuse_address = False


class UdpServer(OwnedHandlers, socketserver.ThreadingUDPServer):
    daemon_threads = False
    allow_reuse_address = False


class Echo(socketserver.BaseRequestHandler):
    def handle(self):
        service = self.server.service
        self.request.settimeout(15)
        count, digest = 0, hashlib.sha256()
        try:
            while data := self.request.recv(4096):
                count += len(data)
                if count > 64*1024*1024:
                    raise ValueError('echo超过测试预算')
                digest.update(data)
                self.request.sendall(data)
            self.request.shutdown(socket.SHUT_WR)
            service.event('tcp-echo', bytes=count, sha256=digest.hexdigest())
        except (ConnectionError, socket.timeout) as error:
            service.event('tcp-closed', reason=repr(error), bytes=count)


class UdpEcho(socketserver.BaseRequestHandler):
    def handle(self):
        data, handle = self.request
        self.server.service.event('udp-echo', bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
        handle.sendto(data, self.client_address)


class Http(socketserver.BaseRequestHandler):
    def handle(self):
        service = self.server.service
        self.request.settimeout(10)
        request = bytearray()
        try:
            while b'\r\n\r\n' not in request:
                data = self.request.recv(1024)
                if not data:
                    return
                request.extend(data)
                if len(request) > 16384:
                    raise ValueError('HTTP请求过长')
            line = bytes(request).split(b'\r\n', 1)[0].split(b' ')
            if len(line) != 3 or line[0] != b'GET':
                raise ValueError('HTTP测试只接受GET')
            path = line[1]
            service.event('http-request', path=path.decode('ascii', 'replace'), request_sha256=hashlib.sha256(request).hexdigest())
            body, head = service.payload, b'HTTP/1.1 200 OK\r\nConnection: close\r\n'
            if path == b'/redirect':
                self.request.sendall(b'HTTP/1.1 302 Found\r\nLocation: /chunked\r\nContent-Length: 0\r\nConnection: close\r\n\r\n')
            elif path == b'/chunked':
                self.request.sendall(head+b'Transfer-Encoding: chunked\r\n\r\n')
                for offset in range(0, len(body), 1003):
                    part = body[offset:offset+1003]
                    self.request.sendall(('%x;fixture=yes\r\n' % len(part)).encode()+part+b'\r\n')
                self.request.sendall(b'0\r\nX-Fixture: complete\r\n\r\n')
            elif path == b'/close':
                self.request.sendall(head+b'\r\n'+body)
            elif path == b'/truncated':
                self.request.sendall(head+f'Content-Length: {len(body)+100}\r\n\r\n'.encode()+body)
            elif path == b'/ambiguous':
                self.request.sendall(head+b'Content-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n')
            elif path == b'/badchunk':
                self.request.sendall(head+b'Transfer-Encoding: chunked\r\n\r\nXYZ\r\n')
            elif path == b'/oversize':
                self.request.sendall(head+b'Content-Length: 268435457\r\n\r\n')
            elif path == b'/length':
                self.request.sendall(head+f'Content-Length: {len(body)}\r\n\r\n'.encode()+body)
            else:
                self.request.sendall(b'HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\n\r\n')
        except (ConnectionError, socket.timeout) as error:
            service.event('http-closed', reason=repr(error))
        except BaseException as error:
            service.error(error)


class Tftp(socketserver.BaseRequestHandler):
    def handle(self):
        request, _ = self.request
        service = self.server.service
        if len(request) < 4:
            return
        opcode = struct.unpack_from('!H', request)[0]
        fields = request[2:].split(b'\0')
        if opcode not in (1, 2) or len(fields) != 3 or fields[1].lower() != b'octet' or fields[2]:
            return
        name = fields[0]
        # 每次请求有独立临时TID。迟到的重复RRQ可形成另一个TID，客户
        # 必须拒绝它；服务只把完成事务记为有效传输，不覆盖主正文。
        handle = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        handle.bind(('127.0.0.1', 0))
        handle.settimeout(.05)
        try:
            if name == b'missing':
                handle.sendto(b'\0\5\0\1Not found\0', self.client_address)
                return
            if opcode == 1:
                self.download(handle, name)
            else:
                self.upload(handle, name)
        except (ConnectionError, socket.timeout) as error:
            service.event('tftp-closed', name=name.decode('ascii', 'replace'), reason=repr(error))
        except BaseException as error:
            service.error(error)
        finally:
            handle.close()

    def packet(self, handle, deadline):
        service = self.server.service
        while not service.stop.is_set() and time.monotonic() < deadline:
            try:
                packet, source = handle.recvfrom(65535)
                if source != self.client_address:
                    handle.sendto(b'\0\5\0\5Unknown transfer ID\0', source)
                    service.event('tftp-foreign-tid')
                    continue
                return packet
            except socket.timeout:
                continue
        return None

    def download(self, handle, name):
        service = self.server.service
        body = b'' if name == b'empty' else service.payload[:16384] if name == b'multiple' else service.payload
        block, offset, dropped = 1, 0, False
        while not service.stop.is_set():
            part = body[offset:offset+512]
            data = struct.pack('!HH', 3, block & 65535)+part
            acknowledged = False
            for attempt in range(8):
                handle.sendto(data, self.client_address)
                if name == b'duplicate' and attempt == 0:
                    handle.sendto(data, self.client_address)
                    service.event('tftp-inject-duplicate', block=block)
                deadline = time.monotonic()+.6
                while time.monotonic() < deadline:
                    received = self.packet(handle, deadline)
                    if received is None:
                        break
                    if received[:2] == b'\0\5':
                        service.event('tftp-client-error', body_hex=received.hex())
                        return
                    if received == struct.pack('!HH', 4, block & 65535):
                        if name == b'lost-final-ack' and len(part) < 512 and not dropped:
                            dropped = True
                            service.event('tftp-inject-lost-final-ack', block=block)
                            break
                        acknowledged = True
                        break
                if acknowledged:
                    break
                if service.stop.is_set():
                    return
                service.event('tftp-retransmit', block=block, attempt=attempt+1)
            if not acknowledged:
                raise TimeoutError('TFTP下载ACK未到')
            if len(part) < 512:
                service.event('tftp-download', name=name.decode('ascii', 'replace'), bytes=len(body), sha256=hashlib.sha256(body).hexdigest())
                return
            offset += len(part)
            block += 1

    def upload(self, handle, name):
        service = self.server.service
        count, expected, digest, attempts, dropped = 0, 1, hashlib.sha256(), 0, False
        ack = struct.pack('!HH', 4, 0)
        handle.sendto(ack, self.client_address)
        deadline = time.monotonic()+.6
        while not service.stop.is_set():
            received = self.packet(handle, deadline)
            if received is None:
                attempts += 1
                if attempts > 8:
                    raise TimeoutError('TFTP上传DATA未到')
                handle.sendto(ack, self.client_address)
                deadline = time.monotonic()+.6
                continue
            if received[:2] == b'\0\5':
                service.event('tftp-client-error', body_hex=received.hex())
                return
            if len(received) < 4 or len(received) > 516 or received[:2] != b'\0\3':
                raise ValueError('TFTP上传DATA格式')
            number = struct.unpack_from('!H', received, 2)[0]
            if number != expected & 65535:
                if number == (expected-1) & 65535:
                    handle.sendto(ack, self.client_address)
                continue
            if name == b'lost-data' and expected == 1 and not dropped:
                dropped = True
                service.event('tftp-inject-lost-data', block=expected)
                continue
            part = received[4:]
            count += len(part)
            if count > 64*1024*1024:
                raise ValueError('TFTP上传超过预算')
            digest.update(part)
            ack = struct.pack('!HH', 4, number)
            handle.sendto(ack, self.client_address)
            attempts, expected, deadline = 0, expected+1, time.monotonic()+.6
            if len(part) < 512:
                service.event('tftp-upload', name=name.decode('ascii', 'replace'), bytes=count, sha256=digest.hexdigest())
                # 最后一份ACK丢失时，重发DATA只得到同一ACK，不重算正文。
                dally = time.monotonic()+2
                while not service.stop.is_set():
                    repeat = self.packet(handle, dally)
                    if repeat is None:
                        break
                    if repeat == received:
                        handle.sendto(ack, self.client_address)
                return


class Services:
    def __init__(self, out):
        self.out = Path(out)
        self.out.mkdir(parents=True, exist_ok=False)
        self.stop, self.lock = threading.Event(), threading.Lock()
        self.events, self.errors, self.servers, self.threads, self.handlers = [], [], {}, [], {}
        self.payload = bytes(range(256))*64+b'\0M10-network-end\xff\n'
        (self.out/'payload.bin').write_bytes(self.payload)
        self.digest = hashlib.sha256(self.payload).hexdigest()
        try:
            for name, server_type, handler in [('tcp', TcpServer, Echo), ('udp', UdpServer, UdpEcho), ('http', TcpServer, Http), ('tftp', UdpServer, Tftp)]:
                server = server_type(('127.0.0.1', 0), handler)
                server.service = self
                self.servers[name] = server
                thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval':.05}, name='M10-'+name)
                self.threads.append(thread)
                thread.start()
        except BaseException:
            self.close()
            raise

    @property
    def ports(self):
        return {name: server.server_address[1] for name, server in self.servers.items()}

    def event(self, event_name, **details):
        with self.lock:
            if len(self.events) >= 200000:
                raise RuntimeError('服务事件超过证据预算')
            self.events.append(dict(event=event_name, monotonic=time.monotonic(), **details))

    def error(self, error):
        with self.lock:
            self.errors.append(repr(error))

    def close(self):
        self.stop.set()
        for server in self.servers.values():
            server.shutdown()
        for thread in self.threads:
            thread.join(5)
            if thread.is_alive():
                self.errors.append('服务线程未停止: '+thread.name)
        # 只触碰本实例拥有的handler/连接，不枚举或终止宿主其它线程。
        # TCP recv由关闭本人连接解除；TFTP等待自身stop，每50ms检查。
        # server_close的标准库拥有者表还会join已派发但未开始的请求。
        with self.lock:
            handlers = list(self.handlers.items())
        for thread, request in handlers:
            if isinstance(request, socket.socket):
                try:
                    request.shutdown(socket.SHUT_RDWR)
                except OSError:
                    pass
        for server in self.servers.values():
            server.server_close()
        for thread, request in handlers:
            thread.join(3)
            if thread.is_alive():
                self.errors.append('请求线程未停止: '+thread.name)
        report = dict(scope='HOST_LOOPBACK_REFERENCE_SERVICES', payload_bytes=len(self.payload), payload_sha256=self.digest,
                      ports=self.ports, events=self.events, errors=self.errors)
        (self.out/'services.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        if self.errors:
            raise RuntimeError('宿主参考服务失败: '+repr(self.errors))

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()
