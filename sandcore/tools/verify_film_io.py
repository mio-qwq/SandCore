#!/usr/bin/env python3
"""mio：实际试帧与畸形PNG验证；保护耗时母版的提交/续渲边界。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from film_io import validate_png


def chunk(kind,body):
    return struct.pack('>I',len(body))+kind+body+struct.pack('>I',zlib.crc32(kind+body)&0xFFFFFFFF)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--frames',nargs='+',required=True)
    args=parser.parse_args();out=Path(args.out);out.mkdir(parents=True,exist_ok=False)
    header=b'\x89PNG\r\n\x1a\n';ihdr=chunk(b'IHDR',struct.pack('>IIBBBBB',2,3,8,2,0,0,0))
    end=chunk(b'IEND',b'');pixels=b''.join(bytes([y%5])+bytes(range(y*6,y*6+6)) for y in range(3))
    compressed=zlib.compress(pixels);valid=header+ihdr+chunk(b'IDAT',compressed)+end
    cases={
        'valid':(valid,True),'signature':(b'BADPNG00'+valid[8:],False),
        'truncated-chunk':(valid[:-15],False),'extra-tail':(valid+b'X',False),
        'idat-crc':(header+ihdr+chunk(b'IDAT',compressed)[:-1]+b'\0'+end,False),
        'truncated-zlib':(header+ihdr+chunk(b'IDAT',compressed[:-2])+end,False),
        'second-zlib':(header+ihdr+chunk(b'IDAT',compressed+compressed)+end,False),
        'long-rows':(header+ihdr+chunk(b'IDAT',zlib.compress(pixels+b'\0'))+end,False),
        'short-rows':(header+ihdr+chunk(b'IDAT',zlib.compress(pixels[:-1]))+end,False),
        'bad-filter':(header+ihdr+chunk(b'IDAT',zlib.compress(b'\5'+pixels[1:]))+end,False),
        'duplicate-ihdr':(header+ihdr+ihdr+chunk(b'IDAT',compressed)+end,False),
        'critical-unknown':(header+ihdr+chunk(b'FAIL',b'')+chunk(b'IDAT',compressed)+end,False),
        'split-idat':(header+ihdr+chunk(b'IDAT',compressed[:2])+chunk(b'tEXt',b'a\0b')+chunk(b'IDAT',compressed[2:])+end,False),
    }
    report=dict(author='mio',status='RUNNING',scope='HOST_FILM_FULL_PNG_VALIDATION',cases={},frames=[])
    for name,(data,expected) in cases.items():
        path=out/(name+'.png');path.write_bytes(data)
        try:validate_png(path,2,3,8);accepted=True
        except (AssertionError,zlib.error,struct.error):accepted=False
        assert accepted==expected,(name,accepted,expected)
        report['cases'][name]='ACCEPT' if accepted else 'REJECT'
    try:validate_png(out/'valid.png',3,3,8)
    except AssertionError:report['cases']['wrong-dimensions']='REJECT'
    else:raise AssertionError('错误尺寸竟通过')
    for directory in args.frames:
        for path in sorted(Path(directory).glob('frame-*.png')):
            proof=validate_png(path,1280,720,8)
            report['frames'].append(dict(path=str(path.resolve()),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),**proof))
    assert report['frames'],'没有实际帧可验'
    report.update(status='PASS',limitations='完整图像结构/压缩流，非影片视觉、连续运动或客体播放器验收')
    (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('FILM IO PASS',len(report['cases']),len(report['frames']),flush=True)


if __name__=='__main__':main()
