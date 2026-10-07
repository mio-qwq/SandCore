#!/usr/bin/env python3
"""独立60秒DHCP生命周期对端，真实选择性丢续期或全部租约应答。"""
import struct
from m10netpeer import FramePeer,SERVER,CLIENT,BROADCAST,dhcp_options,option


class LeasePeer(FramePeer):
    def __init__(self,out):
        self.policy='all'
        self.dhcp_destination=None
        super().__init__(out)

    def set_policy(self,policy):
        if policy not in ('all','drop-renew','blackout'):raise ValueError(policy)
        with self.lock:self.policy=policy
        self.event('lease-policy',policy=policy)

    def receive(self,frame):
        # 只记录本次已到达的IP目的地址；父类仍检查实际IP/UDP校验和、
        # 长度、端口后才调用dhcp。不能靠ciaddr猜单播还是广播重绑定。
        self.dhcp_destination=None
        if len(frame)>=34 and frame[12:14]==b'\x08\0':self.dhcp_destination=frame[30:34]
        return super().receive(frame)

    def dhcp(self,mac,source,request):
        try:fields=dhcp_options(request)
        except ValueError as error:
            self.event('invalid-dhcp',reason=str(error));return
        if request[:3]!=b'\1\1\6' or request[28:34]!=mac:return
        kind=fields.get(53,b'')
        destination=self.dhcp_destination
        ciaddr=request[12:16]
        phase='other'
        if kind==b'\1':phase='discover'
        elif kind==b'\3' and ciaddr==bytes(4):phase='select'
        elif kind==b'\3' and ciaddr==CLIENT:
            phase='renew' if destination==SERVER else 'rebind' if destination==b'\xff'*4 else 'other'
        elif kind==b'\7':phase='release'
        with self.lock:policy=self.policy
        self.event('lease-request',kind=kind.hex(),phase=phase,policy=policy,xid=request[4:8].hex(),
                   source=source.hex(),destination=destination.hex() if destination else None,
                   ciaddr=ciaddr.hex(),requested=fields.get(50,b'').hex(),server_id=fields.get(54,b'').hex())
        if policy=='blackout' or (policy=='drop-renew' and phase=='renew'):
            self.event('lease-reply-dropped',phase=phase,policy=policy);return
        if kind not in (b'\1',b'\3') or fields.get(54,SERVER)!=SERVER:return
        reply_type=2 if kind==b'\1' else 5
        if kind==b'\3' and fields.get(50,ciaddr) not in (CLIENT,bytes(4)):reply_type=6
        body=bytearray(240);body[:4]=b'\2\1\6\0'
        body[4:8],body[10:12],body[28:44]=request[4:8],request[10:12],request[28:44]
        body[16:20],body[20:24],body[236:240]=CLIENT if reply_type!=6 else bytes(4),SERVER,b'\x63\x82\x53\x63'
        body+=option(53,bytes((reply_type,)))+option(54,SERVER)+option(1,b'\xff\xff\xff\0')
        body+=option(3,SERVER)+option(6,SERVER)+option(15,b'm10.test')
        body+=option(51,struct.pack('!I',60))+option(58,struct.pack('!I',20))+option(59,struct.pack('!I',40))+b'\xff'
        body+=bytes(max(0,300-len(body)))
        unicast=source==CLIENT and not (struct.unpack_from('!H',request,10)[0]&32768) and reply_type!=6
        self.send_udp(mac if unicast else BROADCAST,SERVER,source if unicast else b'\xff'*4,67,68,body)
        self.event('lease-reply',phase=phase,kind=reply_type,lease_seconds=60,t1_seconds=20,t2_seconds=40)
