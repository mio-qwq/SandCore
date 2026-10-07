#!/usr/bin/env python3
"""原创有限TCP故障对端；只实现声明的三种固定会话，不是通用协议栈。"""
import hashlib
import json
import struct
import threading
import time

from m10netpeer import FramePeer, MAC, SERVER, CLIENT, checksum

SYN, ACK, FIN, RST, PSH = 2, 16, 1, 4, 8
UPSTREAM = bytes((i*73+(i>>7)) & 255 for i in range(16384))
DOWNSTREAM = bytes((i*29+(i>>5)+0x53) & 255 for i in range(4097))


def distance(sequence, base):
    return (sequence-base) & 0xffffffff


class TcpFaultPeer(FramePeer):
    def __init__(self, out):
        # 基类启动收包线程之前初始化子类状态，避免首个真实帧竞态。
        self.tcp_lock = threading.RLock()
        self.tcp = {}
        super().__init__(out)
        self.timer = threading.Thread(target=self.poll, name='M10-TCP-window-reopen')
        self.timer.start()

    def send_tcp(self, state, flags, body=b'', sequence=None, window=1460, options=b''):
        if len(options) % 4:
            raise ValueError('TCP options must be padded')
        seq = state['server_next'] if sequence is None else sequence
        header = bytearray(struct.pack('!HHIIBBHHH',state['port'],state['client_port'],
                         seq & 0xffffffff,state['client_next'],(20+len(options))//4 << 4,
                         flags,window,0,0)+options+body)
        struct.pack_into('!H',header,16,checksum(SERVER+CLIENT+struct.pack('!BBH',0,6,len(header))+header))
        self.send_ip(state['mac'],SERVER,CLIENT,6,bytes(header),reverse=False)
        self.event('tcp-send',port=state['port'],sequence=seq & 0xffffffff,
                   acknowledgment=state['client_next'],flags=flags,window=window,bytes=len(body))

    def poll(self):
        try:
            while not self.stop.wait(.01):
                with self.tcp_lock:
                    for state in self.tcp.values():
                        until=state.get('zero_until')
                        if until is not None and time.monotonic()>=until:
                            state['zero_until']=None;state['window_reopened']=True
                            self.send_tcp(state,ACK)
                            self.event('tcp-window-reopened',port=state['port'])
        except BaseException as error:
            if not self.stop.is_set():self.errors.append(repr(error))

    def receive(self, frame):
        if len(frame)<54 or frame[:6]!=MAC or frame[12:14]!=b'\x08\x00' or frame[23]!=6:
            return super().receive(frame)
        packet=frame[14:];head=(packet[0]&15)*4;length=struct.unpack_from('!H',packet,2)[0]
        if packet[0]>>4!=4 or head<20 or length<head+20 or length>len(packet):
            raise ValueError('invalid TCP IPv4 packet lengths')
        if checksum(packet[:head]) or struct.unpack_from('!H',packet,6)[0]&0x3fff:
            raise ValueError('bad IPv4 checksum or unexpected fragmented TCP fixture')
        source,destination=packet[12:16],packet[16:20]
        if source!=CLIENT or destination!=SERVER:
            return super().receive(frame)
        tcp=packet[head:length];tcp_head=(tcp[12]>>4)*4
        if tcp_head<20 or tcp_head>len(tcp) or checksum(source+destination+struct.pack('!BBH',0,6,len(tcp))+tcp):
            raise ValueError('invalid TCP header or checksum')
        client_port,port,seq,ack=struct.unpack_from('!HHII',tcp);flags=tcp[13];body=tcp[tcp_head:]
        if port not in (7010,7011,7012):
            return
        with self.tcp_lock:
            self.event('tcp-receive',port=port,sequence=seq,acknowledgment=ack,flags=flags,
                       window=struct.unpack_from('!H',tcp,14)[0],bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
            key=(client_port,port)
            state=self.tcp.get(key)
            if flags&SYN and not flags&ACK:
                if state is None:
                    state=dict(port=port,client_port=client_port,mac=frame[6:12],
                               client_isn=seq,client_next=(seq+1)&0xffffffff,server_isn=0xfffffc00,
                               server_next=0xfffffc01,syns=0,upstream=bytearray(),attempts={},
                               established=False,dropped_data=False,window_reopened=False,fin=False,final_ack=False)
                    self.tcp[key]=state
                if seq!=state['client_isn']:raise AssertionError('SYN retransmission changed sequence')
                state['syns']+=1
                if port==7011:
                    self.send_tcp(state,RST|ACK,sequence=0);state['refused']=True;return
                if port==7010 and state['syns']==1:
                    self.event('tcp-first-syn-dropped',port=port);return
                self.send_tcp(state,SYN|ACK,sequence=state['server_isn'],options=b'\x02\x04\x02\x18')
                return
            if state is None:raise AssertionError('TCP packet without declared SYN')
            if flags&RST:
                state['guest_reset']=True;self.event('tcp-unexpected-guest-reset',port=port);return
            if flags&ACK and not state['established']:
                if ack!=state['server_next']:raise AssertionError('handshake ACK is not exact')
                state['established']=True;self.event('tcp-established',port=port)
            if state['fin'] and flags&ACK and ack==state['server_next']:
                state['final_ack']=True
            if port==7012 and body:
                if body!=b'M':raise AssertionError('reset trigger bytes changed')
                state['client_next']=(seq+len(body))&0xffffffff
                self.send_tcp(state,RST|ACK);state['reset_sent']=True;return
            if port!=7010:return
            if body:
                signature=(seq,len(body),hashlib.sha256(body).hexdigest())
                state['attempts'][signature]=state['attempts'].get(signature,0)+1
                if not state['dropped_data']:
                    state['dropped_data']=True;state['dropped_signature']=signature
                    state['zero_until']=time.monotonic()+2
                    self.event('tcp-first-data-dropped',sequence=seq,bytes=len(body))
                    self.send_tcp(state,ACK,window=0);return
                if state.get('zero_until') is not None:
                    state['zero_window_packets']=state.get('zero_window_packets',0)+1
                    self.send_tcp(state,ACK,window=0);return
                expected=state['client_next']
                if seq!=expected:
                    # 已确认的重复段只回累计ACK；未顺序到达的正文不交付。
                    self.send_tcp(state,ACK);return
                offset=len(state['upstream'])
                if body!=UPSTREAM[offset:offset+len(body)]:raise AssertionError('upstream bytes differ on wire')
                state['upstream'].extend(body);state['client_next']=(expected+len(body))&0xffffffff
                self.send_tcp(state,ACK)
            if flags&FIN:
                fin_seq=(seq+len(body))&0xffffffff
                if fin_seq!=state['client_next']:
                    self.send_tcp(state,ACK);return
                if state['fin']:
                    self.send_tcp(state,ACK);return
                if bytes(state['upstream'])!=UPSTREAM:raise AssertionError('guest FIN before complete original stream')
                state['client_next']=(state['client_next']+1)&0xffffffff
                self.send_tcp(state,ACK)
                self.event('tcp-guest-half-close',bytes=len(state['upstream']))
                base=state['server_next'];parts=[DOWNSTREAM[n:n+512] for n in range(0,len(DOWNSTREAM),512)]
                order=[1,1,3,2,0]+list(range(len(parts)-1,3,-1))
                for index in order:
                    self.send_tcp(state,ACK|PSH,parts[index],sequence=(base+index*512)&0xffffffff)
                state['server_next']=(base+len(DOWNSTREAM))&0xffffffff
                self.send_tcp(state,FIN|ACK);state['server_next']=(state['server_next']+1)&0xffffffff
                state['fin']=True
                self.event('tcp-downstream-reorder-duplicate-fin',order=order,bytes=len(DOWNSTREAM),
                           sequence_wrap=True,sha256=hashlib.sha256(DOWNSTREAM).hexdigest())

    def summary(self):
        with self.tcp_lock:
            connections=[]
            for state in self.tcp.values():
                signature=state.get('dropped_signature')
                connections.append(dict(port=state['port'],client_port=state['client_port'],syns=state['syns'],
                    established=state['established'],upstream_bytes=len(state['upstream']),
                    upstream_sha256=hashlib.sha256(state['upstream']).hexdigest(),
                    dropped_data_repeated=signature is not None and state['attempts'].get(signature,0)>=2,
                    window_reopened=state['window_reopened'],zero_window_packets=state.get('zero_window_packets',0),
                    half_closed=state['fin'],final_ack=state['final_ack'],
                    refused=state.get('refused',False),reset_sent=state.get('reset_sent',False),
                    unexpected_guest_reset=state.get('guest_reset',False)))
            return dict(scope='DECLARED_THREE_RAW_TCP_FAULT_CONNECTIONS_ONLY',connections=connections)

    def close(self):
        self.stop.set();self.timer.join(5)
        if self.timer.is_alive():self.errors.append('TCP window timer did not stop')
        (self.out/'tcp-summary.json').write_text(json.dumps(self.summary(),ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        super().close()
