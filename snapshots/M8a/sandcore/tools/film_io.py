#!/usr/bin/env python3
"""mio：母版帧的完整PNG结构/CRC/压缩流校验；仅宿主渲染工具使用。

只看PNG签名和IEND不能证明中间的IDAT完整。提交/续渲都必须验证
真实尺寸、位深、全部块CRC、单一完整DEFLATE流和每行过滤码。
严格限定RGB、非交错输出，是本片明确的母版合同，不是通用图片解码器。
"""
from pathlib import Path
import struct
import zlib


def validate_png(path,width,height,bits):
    assert 1<=width<=8192 and 1<=height<=8192 and bits in (8,16),'无效母版规格'
    path=Path(path);size=path.stat().st_size
    assert 45<=size<=128*1024*1024,'PNG大小超出母版帧边界'
    data=path.read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n','坏PNG签名'
    at=8;header=False;ended=False;idat=bytearray();idat_ended=False
    while at<len(data):
        assert at+12<=len(data),'截断PNG块头'
        length=struct.unpack_from('>I',data,at)[0];kind=data[at+4:at+8];end=at+12+length
        assert end<=len(data),'截断PNG块内容'
        body=data[at+8:at+8+length]
        actual=zlib.crc32(kind);actual=zlib.crc32(body,actual)&0xFFFFFFFF
        assert actual==struct.unpack_from('>I',data,end-4)[0],'PNG块CRC错误'
        if not header:
            assert kind==b'IHDR' and length==13,'IHDR不是唯一首块'
            assert struct.unpack('>IIBBBBB',body)==(width,height,bits,2,0,0,0),'母版格式/尺寸不符'
            header=True
        elif kind==b'IHDR':raise AssertionError('重复IHDR')
        elif kind==b'IDAT':
            assert not idat_ended,'非连续IDAT'
            idat.extend(body)
        elif kind==b'IEND':
            assert length==0 and end==len(data) and idat,'无效IEND/末尾多余内容'
            ended=True
        else:
            assert kind[0]&32 or kind==b'PLTE','未知关键PNG块'
            if idat:idat_ended=True
        at=end
    assert header and ended,'未完整结束PNG'
    row=1+width*3*(bits//8);expected=row*height
    # 解压上限由明确尺寸计算，损坏压缩流不能无限扩大内存；不使用
    # 无界flush。eof及两个尾部条件排除缺失校验码、第二压缩流/垃圾。
    stream=zlib.decompressobj();decoded=stream.decompress(idat,expected+1)
    assert stream.eof and not stream.unconsumed_tail and not stream.unused_data,'截断/多余DEFLATE流'
    assert len(decoded)==expected,'解压后行数据长度不符'
    assert all(decoded[y*row]<=4 for y in range(height)),'无效逐行PNG过滤码'
    return dict(width=width,height=height,bits=bits,bytes=size,decoded_bytes=len(decoded),validation='FULL_CRC_DEFLATE_ROWS')
