#!/usr/bin/env python3
"""独立宿主FFmpeg编码FLAC；原创PCM/封面及精确S16参考，不发布宿主工具。"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]

def fnv(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte)*16777619) & 0xffffffff
    return value

def run(command, out):
    result = subprocess.run(list(map(str, command)), capture_output=True, check=True)
    with (out/'encoder.log').open('ab') as log:
        log.write((' '.join(map(str, command))+'\n').encode())
        log.write(result.stdout+result.stderr)

def metadata(flac, picture):
    data = flac.read_bytes()
    at, blocks = 4, []
    while True:
        h = data[at:at+4]; size = int.from_bytes(h[1:4], 'big')
        blocks.append((h[0]&127, data[at+4:at+4+size])); at += 4+size
        if h[0]&128:
            break
    # 自己写metadata封装，不改独立编码器的FLAC音频帧。
    comments = [b'title=Evening Lines', b'artist=mio / SandCore']
    vendor = b'SandCore original verification fixture'
    tag = struct.pack('<I', len(vendor))+vendor+struct.pack('<I', len(comments))
    tag += b''.join(struct.pack('<I', len(x))+x for x in comments)
    pic = struct.pack('>II', 3, 9)+b'image/png'+struct.pack('>I', 0)
    pic += struct.pack('>IIIII', 600,600,24,0,len(picture))+picture
    blocks = [(k,v) for k,v in blocks if k not in (4,6)]+[(4,tag),(6,pic)]
    result = b'fLaC'+b''.join(bytes([kind|(128 if i==len(blocks)-1 else 0)])+len(body).to_bytes(3,'big')+body
                            for i,(kind,body) in enumerate(blocks))+data[at:]
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ffmpeg', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args(); out = args.out.resolve(); out.mkdir(parents=True, exist_ok=False)
    palette = json.loads((ROOT/'assets/design/tokens.json').read_text())['aurora']
    colors = {k:tuple(bytes.fromhex(v[1:])) for k,v in palette.items()}
    image = Image.new('RGB', (600,600)); pixels = image.load()
    for y in range(600):
        for x in range(600):
            t = (x+y)/1198; pixels[x,y]=tuple(int(a+(b-a)*t) for a,b in zip(colors['ink'],colors['accent_deep']))
    draw = ImageDraw.Draw(image)
    # 封面是原创测试资源，颜色沿用主题token；真实图像内容不塞进系统调色板。
    draw.ellipse((76,54,510,488),fill=colors['sand'])
    for i in range(8):
        x=90+i*38; draw.polygon([(x,280),(x+23,252),(x+115,518),(x+82,518)], fill=colors['gold'] if i&1 else colors['cyan'])
    font = Path('C:/Windows/Fonts/segoeui.ttf')
    draw.text((42,36),'SANDCORE  /  09',font=ImageFont.truetype(str(font),18),fill=colors['paper'])
    draw.rectangle((0,462,600,600),fill=colors['ink'])
    draw.text((42,479),'Evening Lines',font=ImageFont.truetype(str(font),42),fill=colors['paper'])
    draw.text((44,540),'mio     /     local listening room',font=ImageFont.truetype(str(font),17),fill=colors['cyan'])
    image.save(out/'cover.png'); picture=(out/'cover.png').read_bytes()
    rgba=image.convert('RGBA').tobytes(); bgra=b''.join(rgba[i+2:i+3]+rgba[i+1:i+2]+rgba[i:i+1]+rgba[i+3:i+4] for i in range(0,len(rgba),4))
    fixtures=[]; rate=44100; count=rate*5//4
    for name,bits,channels,container in [('MONO',16,1,'flac'),('STEREO',24,2,'flac'),('OGG',16,2,'ogg')]:
        raw,reference=bytearray(),bytearray()
        for i in range(count):
            a=int(18000*math.sin(i*2*math.pi*313.7/rate)+5000*math.sin(i*2*math.pi*71.3/rate))
            b=int(15000*math.sin(i*2*math.pi*227.3/rate)+6000*math.sin(i*2*math.pi*127.7/rate))
            for c,value in enumerate([a,b][:channels]):
                if bits==24:
                    sample=value*256+((i*13+c*37)&255);raw.extend((sample&0xffffff).to_bytes(3,'little'))
                else:
                    raw.extend(struct.pack('<h',value))
            reference.extend(struct.pack('<hh',a,a if channels==1 else b))
        source=out/(name+'.raw');source.write_bytes(raw)
        destination=out/(name+'.'+('OGA' if container=='ogg' else 'FLAC'))
        run([args.ffmpeg,'-hide_banner','-nostdin','-v','error','-f','s'+str(bits)+'le','-ar',rate,'-ac',channels,
             '-i',source,'-c:a','flac','-compression_level','8','-f',container,destination],out)
        frames=[bytes(reference)[:2048*4],bytes(reference)[rate*4:(rate+2048)*4]]
        expected=struct.pack('<8I',count,fnv(reference),rate,channels,bits,fnv(frames[0]),fnv(frames[1]),2048)
        fixtures.append(dict(file=destination.name, expected_hex=expected.hex(),encoded_sha256=hashlib.sha256(destination.read_bytes()).hexdigest(),
                             reference_sha256=hashlib.sha256(reference).hexdigest()))
    (out/'ALBUM.FLAC').write_bytes(metadata(out/'MONO.FLAC',picture))
    data=(out/'MONO.FLAC').read_bytes();(out/'TRUNC.FLAC').write_bytes(data[:-500])
    damaged=bytearray(data);damaged[-200]^=128;(out/'CRC.FLAC').write_bytes(damaged)
    def sync(n):return bytes([(n>>21)&127,(n>>14)&127,(n>>7)&127,n&127])
    tags=[(b'TIT2',b'\x03Evening Lines'),(b'TPE1',b'\x03mio / SandCore'),(b'APIC',b'\x03image/png\0\x03\0'+picture)]
    tag=b''.join(key+struct.pack('>I',len(body))+b'\0\0'+body for key,body in tags)
    (out/'ALBUM.MP3').write_bytes(b'ID3\x03\0\0'+sync(len(tag))+tag+(ROOT/'build/m9-work/fs/SYS/TEST/TONE.MP3').read_bytes())
    result=dict(status='HOST_FIXTURES_ENCODED_NOT_GUEST_PASS',encoder=str(args.ffmpeg),encoder_sha256=hashlib.sha256(args.ffmpeg.read_bytes()).hexdigest(),
                fixtures=fixtures,art_expected_hex=struct.pack('<III',600,600,fnv(bgra)).hex(),art_title='Evening Lines',art_artist='mio / SandCore')
    (out/'fixtures.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result))

if __name__=='__main__':main()
