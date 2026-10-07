"""包内 LZSS 载荷冻结；不改变 SCX、SCB2 或任何平台格式。

SPLZ1MIO: 原长 u32 + 每八 token 一组标记（1 字面量）。回引用
两个小端字节，高12位=距离-1，低4位=长度-3；窗口4096、长度3..18。
每一项压缩后立即回解并逐字节比较，原 SCX/图标 CRC 另外保留。
"""
import struct
import zlib
import re
import sys
from collections import defaultdict, deque
from pathlib import Path


def compress(data):
    out=bytearray(b'SPLZ1MIO'+struct.pack('<I',len(data)))
    index=defaultdict(deque);pos=0
    while pos<len(data):
        flag_at=len(out);out.append(0);flags=0
        for bit in range(8):
            if pos>=len(data):break
            best_n=0;best_pos=0;key=data[pos:pos+3]
            if len(key)==3:
                candidates=index[key]
                while candidates and pos-candidates[0]>4096:candidates.popleft()
                for previous in reversed(list(candidates)[-96:]):
                    n=3
                    while n<18 and pos+n<len(data) and data[previous+n]==data[pos+n]:n+=1
                    if n>best_n:best_n,best_pos=n,previous
                    if n==18:break
            n=best_n if best_n>=3 else 1
            if n==1:flags|=1<<bit;out.append(data[pos])
            else:out.extend(struct.pack('<H',((pos-best_pos-1)<<4)|(n-3)))
            for at in range(pos,pos+n):
                if at+3<=len(data):
                    bucket=index[data[at:at+3]];bucket.append(at)
                    while len(bucket)>128:bucket.popleft()
            pos+=n
        out[flag_at]=flags
    return bytes(out)


def decompress(data):
    if len(data)<12 or data[:8]!=b'SPLZ1MIO':raise ValueError('bad magic')
    size=struct.unpack_from('<I',data,8)[0];out=bytearray();at=12
    while len(out)<size:
        flags=data[at];at+=1
        for bit in range(8):
            if len(out)>=size:break
            if flags&(1<<bit):out.append(data[at]);at+=1
            else:
                token=struct.unpack_from('<H',data,at)[0];at+=2
                distance=(token>>4)+1;n=(token&15)+3
                if distance>len(out) or n>size-len(out):raise ValueError('bad back reference')
                for _ in range(n):out.append(out[-distance])
    if at!=len(data):raise ValueError('trailing bytes')
    return bytes(out)


def array(data):
    return ',\n'.join(' '+','.join(f'0x{x:02x}' for x in data[i:i+24]) for i in range(0,len(data),24))


def main():
    manifest,hfile,cfile=sys.argv[1:];entries=[];body=['/* 包内压缩资源，勿手改。 */','#include "payload.h"']
    pattern=re.compile(r'^(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+"([^"]*)"\s+(\S+)\s+(\S+)\s*$')
    for line in Path(manifest).read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.lstrip().startswith('#'):continue
        match=pattern.fullmatch(line.strip())
        if not match:raise ValueError('bad manifest row')
        name,scx,icon,cfg,desc,label,cfgname=match.groups();parts=[]
        for kind,source,magic in [('SCX',scx,b'SCX1MIO\0'),('ICON',icon,b'SCB2MIO\0')]:
            raw=Path(source).read_bytes()
            if not raw.startswith(magic):raise ValueError('bad input magic')
            packed=compress(raw)
            if decompress(packed)!=raw:raise ValueError('roundtrip failed')
            symbol='PAYLOAD_'+name+'_'+kind
            body.append(f'static const u8 {symbol}[]={{\n{array(packed)}\n}};')
            parts.append(f'{symbol},{len(raw)},sizeof({symbol}),0x{zlib.crc32(raw):08x}u')
        def quote(s):return '"'+s.replace('\\','\\\\').replace('"','\\"').replace('\n','\\n')+'"'
        config=Path(cfg).read_text(encoding='utf-8')
        entries.append('{'+','.join([quote(name),quote(label),quote(cfgname),*parts,quote(config),quote(desc)])+'}')
    header='''/* 包内 SPLZ1MIO 资源合同；平台 SCX/SCB2 不变。 */
#ifndef PAYLOAD_H
#define PAYLOAD_H
#include "SCAPI.H"
typedef struct {
 const char *name,*label,*cfg_name;
 const u8 *scx;u32 scx_size,scx_packed_size,scx_crc;
 const u8 *icon;u32 icon_size,icon_packed_size,icon_crc;
 const char *cfg,*desc;
} exapp_entry;
extern const exapp_entry EXAPP[];
extern const int EXAPP_COUNT;
#endif
'''
    body.append('const exapp_entry EXAPP[]={\n'+',\n'.join(entries)+'\n};')
    body.append(f'const int EXAPP_COUNT={len(entries)};\n')
    Path(hfile).write_text(header,encoding='utf-8');Path(cfile).write_text('\n'.join(body),encoding='utf-8')
    print('Packed and verified',len(entries),'applications and icons')


if __name__=='__main__':main()
