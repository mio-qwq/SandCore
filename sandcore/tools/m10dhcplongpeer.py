#!/usr/bin/env python3
"""原创长/无限DHCP对端；只调整真实线缆选项，不接触客体内存。"""
import struct
from m10dhcppeer import LeasePeer
from m10netpeer import SERVER,CLIENT,BROADCAST,dhcp_options,option


class LongLeasePeer(LeasePeer):
    def __init__(self,out):
        self.lease_values=(86400,43200,75600)
        super().__init__(out)

    def set_lease(self,seconds,t1=None,t2=None):
        for value in (seconds,t1,t2):
            if value is not None and not 0<=value<=0xFFFFFFFF:raise ValueError(value)
        with self.lock:self.lease_values=(seconds,t1,t2)
        self.event('lease-parameters',seconds=seconds,t1=t1,t2=t2)

    def dhcp(self,mac,source,request):
        try:fields=dhcp_options(request)
        except ValueError as error:
            self.event('invalid-dhcp',reason=str(error));return
        if request[:3]!=b'\1\1\6' or request[28:34]!=mac:return
        kind=fields.get(53,b'');destination=self.dhcp_destination;ciaddr=request[12:16]
        phase='other'
        if kind==b'\1':phase='discover'
        elif kind==b'\3' and ciaddr==bytes(4):phase='select'
        elif kind==b'\3' and ciaddr==CLIENT:
            phase='renew' if destination==SERVER else 'rebind' if destination==b'\xff'*4 else 'other'
        elif kind==b'\7':phase='release'
        with self.lock:seconds,t1,t2=self.lease_values
        self.event('long-lease-request',kind=kind.hex(),phase=phase,xid=request[4:8].hex(),
                   source=source.hex(),destination=destination.hex() if destination else None,
                   ciaddr=ciaddr.hex(),requested=fields.get(50,b'').hex(),server_id=fields.get(54,b'').hex())
        if kind not in (b'\1',b'\3') or fields.get(54,SERVER)!=SERVER:return
        reply_type=2 if kind==b'\1' else 5
        if kind==b'\3' and fields.get(50,ciaddr) not in (CLIENT,bytes(4)):reply_type=6
        body=bytearray(240);body[:4]=b'\2\1\6\0'
        body[4:8],body[10:12],body[28:44]=request[4:8],request[10:12],request[28:44]
        body[16:20],body[20:24],body[236:240]=CLIENT if reply_type!=6 else bytes(4),SERVER,b'\x63\x82\x53\x63'
        body+=option(53,bytes((reply_type,)))+option(54,SERVER)+option(1,b'\xff\xff\xff\0')
        body+=option(3,SERVER)+option(6,SERVER)+option(15,b'm10.test')+option(51,struct.pack('!I',seconds))
        if t1 is not None:body+=option(58,struct.pack('!I',t1))
        if t2 is not None:body+=option(59,struct.pack('!I',t2))
        body+=b'\xff';body+=bytes(max(0,300-len(body)))
        unicast=source==CLIENT and not (struct.unpack_from('!H',request,10)[0]&32768) and reply_type!=6
        self.send_udp(mac if unicast else BROADCAST,SERVER,source if unicast else b'\xff'*4,67,68,body)
        self.event('long-lease-reply',phase=phase,kind=reply_type,seconds=seconds,t1=t1,t2=t2)
