#!/usr/bin/env python3
"""原创客体验收资源，不执行构建；由M9第二阶段资源规则调用。

MP3夹具按固定MPEG1 LayerIII帧字段写两种极小谱：全零和一个
table1(1,1)正值对。它是已知结构夹具，不是通用音频编码器，也
不替代不同编码器/码率/VBR/联合立体声的后续兼容矩阵。
"""
import argparse
import bz2
import hashlib
import json
import lzma
from pathlib import Path
import struct
import zipfile


class Bits:
    def __init__(self):
        self.values=[]

    def put(self,value,width):
        if value<0 or value>=1<<width:
            raise ValueError('位字段溢出')
        self.values.extend((value>>bit)&1 for bit in range(width-1,-1,-1))

    def bytes(self):
        if len(self.values)%8:
            raise ValueError('字段未字节对齐')
        return bytes(sum(self.values[i+j]<<(7-j) for j in range(8)) for i in range(0,len(self.values),8))


def mp3(tone):
    result=bytearray()
    for index in range(80):
        bits=Bits()
        bits.put(0,9)  # main_data_begin：每帧独立，无reservoir/backpointer。
        bits.put(0,3)  # private bits。
        bits.put(0,8)  # 两声道scfsi。
        audible=tone and 4<=index<76
        gain=232+min(8,index-4,75-index) if audible else 0
        for granule_channel in range(4):
            bits.put(5 if audible else 0,12)
            bits.put(1 if audible else 0,9)
            bits.put(gain,8)
            bits.put(0,4)  # 无scalefactor正文。
            bits.put(0,1)  # long block，无window switching。
            bits.put(1 if audible else 0,5)
            bits.put(0,5)
            bits.put(0,5)
            bits.put(0,4)
            bits.put(0,3)
            bits.put(0,1)
            bits.put(0,1)
            bits.put(0,1)
        side=bits.bytes()
        if len(side)!=32:
            raise ValueError('MPEG1双声道side info容量异常')
        # 128kbps/44100Hz/stereo/no CRC/no padding，417B。table1
        # (1,1)的3bit码000与两个正号0正好5bit，四组共20bit。
        # 因码/符号都是0，main data与无用的ancillary正文均为零。
        result.extend(b'\xff\xfb\x90\x00'+side+bytes(417-4-32))
    return bytes(result)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    sources={'ZERO.MP3':mp3(False),'TONE.MP3':mp3(True),
             'BAD.WAV':b'RIFF'+struct.pack('<I',1000)+b'WAVEfmt '+bytes(20),
             'DATA.BZ2':bz2.compress(b'contents'),
             'DATA.LZMA':lzma.compress(b'contents',format=lzma.FORMAT_ALONE)}
    for name,body in sources.items():
        (args.out/name).write_bytes(body)
    with zipfile.ZipFile(args.out/'DATA.ZIP','w',compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('M9MEMBER',b'contents')
    report={name:dict(bytes=len(body),sha256=hashlib.sha256(body).hexdigest()) for name,body in sources.items()}
    report['mp3']=dict(rate=44100,channels=2,frames=80,samples_per_frame=1152,
                       scope='original fixed spectral fixtures; not full external-encoder interoperability')
    (args.out/'FIXTURES.JSON').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
