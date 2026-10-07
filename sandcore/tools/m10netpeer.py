#!/usr/bin/env python3
"""原创、隔离的以太网验收对端；仅在统一验证阶段显式实例化。

socket后端每帧为网络序u32长度加Ethernet正文。固定测试网不连接
宿主其它接口；DHCP/DNS/ARP/ICMP/UDP是线缆协议的独立参考实现，
没有借用客体lwIP，也没有复制QEMU实现。这里不提供TCP协议栈。
"""
import hashlib
import json
from pathlib import Path
import socket
import struct
import threading
import time

SERVER = socket.inet_aton('10.23.0.1')
CLIENT = socket.inet_aton('10.23.0.2')
REMOTE = socket.inet_aton('10.23.1.2')
CONFLICT = socket.inet_aton('10.23.0.99')
MAC = bytes.fromhex('525400abcdef')
BROADCAST = b'\xff' * 6


def checksum(data):
    if len(data) & 1:
        data += b'\0'
    total = sum(struct.unpack('!'+str(len(data)//2)+'H', data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def option(code, body):
    return bytes((code, len(body))) + body


def dhcp_options(body):
    if len(body) < 240 or body[236:240] != b'\x63\x82\x53\x63':
        raise ValueError('DHCP cookie/长度')
    values, offset = {}, 240
    while offset < len(body):
        code = body[offset]
        offset += 1
        if code == 255:
            return values
        if not code:
            continue
        if offset == len(body) or offset+1+body[offset] > len(body):
            raise ValueError('DHCP option越界')
        length = body[offset]
        if code in values:
            raise ValueError('DHCP重复option')
        values[code] = body[offset+1:offset+1+length]
        offset += length+1
    raise ValueError('DHCP缺结束标记')


class FramePeer:
    def __init__(self, out):
        self.out = Path(out)
        self.out.mkdir(parents=True, exist_ok=False)
        self.stop, self.connected = threading.Event(), threading.Event()
        self.disconnect_expected = threading.Event()
        self.lock, self.send_lock = threading.Lock(), threading.Lock()
        self.errors, self.events, self.fragments = [], [], {}
        self.connection, self.identification, self.wire_bytes = None, 0, 0
        self.reverse_fragments = True
        self.pcap = (self.out/'frames.pcap').open('xb')
        self.pcap.write(struct.pack('<IHHIIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1))
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.listener.bind(('127.0.0.1', 0))
            self.listener.listen(1)
            self.listener.settimeout(.1)
            self.port = self.listener.getsockname()[1]
            self.thread = threading.Thread(target=self.pump, name='M10-isolated-ethernet')
            self.thread.start()
        except BaseException:
            self.listener.close()
            self.pcap.close()
            raise

    def event(self, event_name, **details):
        with self.lock:
            if len(self.events) >= 200000:
                raise RuntimeError('对端事件超过本次证据预算')
            self.events.append(dict(event=event_name, monotonic=time.monotonic(), **details))

    def record(self, direction, frame):
        stamp = time.time()
        with self.lock:
            self.wire_bytes += len(frame)
            if self.wire_bytes > 64*1024*1024:
                raise RuntimeError('对端流量超过本次证据预算')
            seconds = int(stamp)
            self.pcap.write(struct.pack('<IIII', seconds, int((stamp-seconds)*1000000), len(frame), len(frame))+frame)
            self.pcap.flush()
        self.event('frame', direction=direction, bytes=len(frame), sha256=hashlib.sha256(frame).hexdigest())

    def pump(self):
        try:
            while not self.stop.is_set():
                try:
                    self.connection, address = self.listener.accept()
                    break
                except socket.timeout:
                    continue
            if self.stop.is_set():
                return
            if address[0] != '127.0.0.1':
                raise RuntimeError('对端连接越过loopback边界')
            self.connection.settimeout(.1)
            self.connected.set()
            pending = bytearray()
            while not self.stop.is_set():
                now = time.monotonic()
                for key in [key for key, entry in self.fragments.items() if now-entry['began'] > 10]:
                    del self.fragments[key]
                    self.event('fragment-timeout')
                try:
                    received = self.connection.recv(65536)
                except socket.timeout:
                    continue
                except ConnectionResetError:
                    if self.disconnect_expected.is_set():
                        self.event('expected-vm-disconnect', reason='reset')
                        break
                    raise
                if not received:
                    if not self.stop.is_set() and not self.disconnect_expected.is_set():
                        raise RuntimeError('隔离以太网在测试结束前断开')
                    break
                pending.extend(received)
                while len(pending) >= 4:
                    count = struct.unpack_from('!I', pending)[0]
                    if count < 14 or count > 65535:
                        raise ValueError('Ethernet流长度越界')
                    if len(pending) < count+4:
                        break
                    frame = bytes(pending[4:count+4])
                    del pending[:count+4]
                    self.record('guest-to-peer', frame)
                    self.receive(frame)
        except BaseException as error:
            if not self.stop.is_set():
                self.errors.append(repr(error))

    def send_frame(self, destination, kind, body):
        frame = destination+MAC+struct.pack('!H', kind)+body
        if len(frame) < 60:
            frame += bytes(60-len(frame))
        self.record('peer-to-guest', frame)
        with self.send_lock:
            if not self.connected.wait(5) or not self.connection:
                raise TimeoutError('隔离Ethernet未连接')
            self.connection.sendall(struct.pack('!I', len(frame))+frame)

    def send_ip(self, destination_mac, source, destination, protocol, body, reverse=None):
        self.identification = (self.identification+1) & 65535
        fragments = []
        for offset in range(0, max(1, len(body)), 1480):
            part = body[offset:offset+1480]
            flags = offset//8 | (8192 if offset+len(part) < len(body) else 0)
            header = bytearray(struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20+len(part), self.identification, flags, 64, protocol, 0, source, destination))
            struct.pack_into('!H', header, 10, checksum(header))
            fragments.append(bytes(header)+part)
        if (reverse if reverse is not None else self.reverse_fragments):
            fragments.reverse()
        for fragment in fragments:
            self.send_frame(destination_mac, 0x0800, fragment)

    def send_udp(self, mac, source, destination, source_port, target_port, body):
        packet = bytearray(struct.pack('!HHHH', source_port, target_port, 8+len(body), 0)+body)
        pseudo = source+destination+struct.pack('!BBH', 0, 17, len(packet))
        struct.pack_into('!H', packet, 6, checksum(pseudo+packet) or 65535)
        self.send_ip(mac, source, destination, 17, packet)

    def receive(self, frame):
        if frame[:6] not in (MAC, BROADCAST):
            return
        kind = struct.unpack_from('!H', frame, 12)[0]
        if kind == 0x0806:
            return self.arp(frame[6:12], frame[14:])
        if kind != 0x0800 or len(frame) < 34:
            return
        packet = frame[14:]
        header_bytes = (packet[0] & 15)*4
        count = struct.unpack_from('!H', packet, 2)[0]
        if packet[0] >> 4 != 4 or header_bytes < 20 or count < header_bytes or count > len(packet) or checksum(packet[:header_bytes]):
            self.event('invalid-ip-header')
            return
        packet = packet[:count]
        source, destination, protocol = packet[12:16], packet[16:20], packet[9]
        flags = struct.unpack_from('!H', packet, 6)[0]
        body = packet[header_bytes:]
        if flags & 0x3fff:
            offset = (flags & 8191)*8
            if offset+len(body) > 65515 or (flags & 8192 and (not body or len(body) % 8)):
                self.event('invalid-fragment')
                return
            key = (source, destination, protocol, packet[4:6])
            if key not in self.fragments:
                if len(self.fragments) >= 16:
                    self.event('fragment-backpressure')
                    return
                self.fragments[key] = dict(began=time.monotonic(), parts={}, end=None, header=None)
            entry = self.fragments[key]
            for at, previous in entry['parts'].items():
                if offset < at+len(previous) and at < offset+len(body):
                    if at == offset and previous == body:
                        return
                    del self.fragments[key]
                    self.event('fragment-overlap-rejected')
                    return
            entry['parts'][offset] = body
            if not offset:
                entry['header'] = packet[:header_bytes]
            if not flags & 8192:
                entry['end'] = offset+len(body)
            cursor, pieces = 0, []
            for at, part in sorted(entry['parts'].items()):
                if at != cursor:
                    return
                cursor += len(part)
                pieces.append(part)
            if cursor != entry['end'] or entry['header'] is None:
                return
            body, header = b''.join(pieces), entry['header']
            del self.fragments[key]
            self.event('fragment-reassembled', bytes=len(body), fragments=len(pieces))
        else:
            header = packet[:header_bytes]
        mac = frame[6:12]
        if destination not in (SERVER, REMOTE, b'\xff'*4):
            return
        if destination == REMOTE and header[8] <= 1:
            return self.icmp_error(mac, source, 11, 0, header+body[:8])
        if protocol == 1:
            if len(body) >= 8 and not checksum(body) and body[:2] == b'\x08\0':
                reply = bytearray(body)
                reply[0] = 0
                reply[2:4] = b'\0\0'
                struct.pack_into('!H', reply, 2, checksum(reply))
                self.send_ip(mac, destination, source, 1, reply)
        elif protocol == 17 and len(body) >= 8:
            source_port, target_port, length, check = struct.unpack_from('!HHHH', body)
            if length != len(body) or (check and checksum(source+destination+struct.pack('!BBH', 0, 17, length)+body)):
                self.event('invalid-udp')
                return
            payload = body[8:]
            if target_port == 67:
                self.dhcp(mac, source, payload)
            elif target_port == 53:
                self.dns(mac, source, source_port, payload)
            elif target_port == 7007:
                self.event('udp-echo', bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest())
                self.send_udp(mac, destination, source, target_port, source_port, payload)
            elif target_port != 7008:
                self.icmp_error(mac, source, 3, 3, header+body[:8])

    def icmp_error(self, mac, destination, kind, code, quote):
        body = bytearray(bytes((kind, code))+bytes(6)+quote)
        struct.pack_into('!H', body, 2, checksum(body))
        self.send_ip(mac, SERVER, destination, 1, body)

    def arp(self, source_mac, packet):
        if len(packet) < 28 or packet[:8] != b'\0\1\x08\0\6\4\0\1' or packet[8:14] != source_mac:
            return
        target = packet[24:28]
        if target not in (SERVER, REMOTE, CONFLICT):
            return
        self.event('arp-answer', address=socket.inet_ntoa(target), probe=packet[14:18] == bytes(4))
        reply = b'\0\1\x08\0\6\4\0\2'+MAC+target+source_mac+packet[14:18]
        self.send_frame(source_mac, 0x0806, reply)

    def dhcp(self, mac, source, request):
        try:
            fields = dhcp_options(request)
        except ValueError as error:
            self.event('invalid-dhcp', reason=str(error))
            return
        if request[:3] != b'\1\1\6' or request[28:34] != mac:
            return
        kind = fields.get(53, b'')
        self.event('dhcp-request', kind=kind.hex(), xid=request[4:8].hex())
        if kind not in (b'\1', b'\3') or fields.get(54, SERVER) != SERVER:
            return
        reply_type = 2 if kind == b'\1' else 5
        if kind == b'\3' and fields.get(50, request[12:16]) not in (CLIENT, bytes(4)):
            reply_type = 6
        body = bytearray(240)
        body[:4] = b'\2\1\6\0'
        body[4:8], body[10:12], body[28:44] = request[4:8], request[10:12], request[28:44]
        body[16:20], body[20:24], body[236:240] = CLIENT if reply_type != 6 else bytes(4), SERVER, b'\x63\x82\x53\x63'
        body += option(53, bytes((reply_type,)))+option(54, SERVER)+option(1, b'\xff\xff\xff\0')
        body += option(3, SERVER)+option(6, SERVER)+option(15, b'm10.test')
        body += option(51, struct.pack('!I', 60))+option(58, struct.pack('!I', 20))+option(59, struct.pack('!I', 40))+b'\xff'
        body += bytes(max(0, 300-len(body)))
        unicast = source == CLIENT and not (struct.unpack_from('!H', request, 10)[0] & 32768) and reply_type != 6
        self.send_udp(mac if unicast else BROADCAST, SERVER, source if unicast else b'\xff'*4, 67, 68, body)
        self.event('dhcp-reply', kind=reply_type)

    def dns(self, mac, source, port, request):
        if len(request) < 12 or struct.unpack_from('!HHHH', request, 4) != (1, 0, 0, 0):
            return
        cursor, labels = 12, []
        while cursor < len(request):
            length = request[cursor]
            cursor += 1
            if not length:
                break
            if length > 63 or cursor+length > len(request):
                return
            labels.append(request[cursor:cursor+length])
            cursor += length
        if cursor+4 != len(request) or request[cursor:cursor+4] != b'\0\1\0\1':
            return
        name = b'.'.join(labels).lower()
        self.event('dns-query', name=name.decode('ascii', 'replace'), transaction=request[:2].hex())
        if name == b'drop.m10.test':
            return
        success = name in (b'host.m10.test', b'large.m10.test')
        reply = request[:2]+struct.pack('!HHHHH', 0x8180 if success else 0x8183, 1, int(success), 0, 0)+request[12:]
        if success:
            reply += b'\xc0\x0c'+struct.pack('!HHIH', 1, 1, 5, 4)+SERVER
        self.send_udp(mac, SERVER, source, 53, port, reply)

    def expect_disconnect(self):
        # 仅由拥有VM的验证器在用例/诊断结束后、QMP quit之前调用。
        # 不能吞测试中途的reset；事件时间与原错误都保留在peer.json。
        self.disconnect_expected.set()
        self.event('expected-vm-shutdown')

    def close(self):
        self.stop.set()
        self.listener.close()
        if self.connection:
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.connection.close()
        self.thread.join(5)
        if self.thread.is_alive():
            self.errors.append('对端线程未停止')
        self.pcap.close()
        report = dict(scope='ISOLATED_UDP_ICMP_DHCP_DNS_REFERENCE_NOT_TCP', errors=self.errors,
                      events=self.events, wire_bytes=self.wire_bytes, port=self.port,
                      pcap_sha256=hashlib.sha256((self.out/'frames.pcap').read_bytes()).hexdigest())
        (self.out/'peer.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        if self.errors:
            raise RuntimeError('隔离对端失败: '+repr(self.errors))

    def __enter__(self):
        return self

    def __exit__(self, kind, error, trace):
        try:
            self.close()
        except Exception as cleanup_error:
            if error is None:
                raise
            error.add_note('隔离对端清理同时失败：' + str(cleanup_error))
