#!/usr/bin/env python3
"""mio：同原生尺寸实际PNG的颜色误差/空间分布与对照图。

指标是八位sRGB显示误差，不冒充完整HDR光学或主观视觉验收。
固定机位/物理尺寸/字体/构图须由各工程身份另外核对。Pillow在C
侧做全图差和直方图，避免Python逐像素三通道循环成为性能热路。
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from PIL import Image,ImageChops,ImageDraw,ImageFilter

ROOT=Path(__file__).resolve().parent.parent
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def percentile(histogram,rank):
    target=sum(histogram)*rank;accumulated=0
    for index,count in enumerate(histogram):
        accumulated+=count
        if accumulated>=target:return index
    return len(histogram)-1

def describe(difference):
    counts=difference.histogram();histogram=[sum(counts[channel*256+i] for channel in range(3)) for i in range(256)]
    total=sum(histogram);mean=sum(i*n for i,n in enumerate(histogram))/total
    mse=sum(i*i*n for i,n in enumerate(histogram))/total
    return dict(mean_absolute_code=mean,rms_code=math.sqrt(mse),psnr_db=math.inf if mse==0 else 10*math.log10(255*255/mse),
        absolute_code_p95=percentile(histogram,.95),absolute_code_p99=percentile(histogram,.99),
        channels_over_8=sum(histogram[9:])/total,channels_over_16=sum(histogram[17:])/total)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--reference',required=True)
    parser.add_argument('--candidate',required=True);parser.add_argument('--frames',default='1,1410,2221')
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    reference=ROOT/args.reference;candidate=ROOT/args.candidate
    projects={kind:json.loads((directory/'project.json').read_text(encoding='utf-8')) for kind,directory in [('reference',reference),('candidate',candidate)]}
    for key in ('width','height','fps','seconds','frames','output_bits','fonts','prism_planes'):
        assert projects['reference'][key]==projects['candidate'][key],('物理规格/美术身份不同',key)
    report=dict(author='mio',magic='SCFILMQUALITY1MIO',version=1,status='MEASURED',
        scope='FULL_NATIVE_SRGB_DISPLAY_ERROR',width=projects['reference']['width'],height=projects['reference']['height'],
        inputs={kind:sha(directory/'project.json') for kind,directory in [('reference',reference),('candidate',candidate)]},frames=[],
        limitations='静态帧八位sRGB误差；非完整16位HDR/时域/最终视觉通过，不自动设PASS阈值')
    for frame in [int(value) for value in args.frames.split(',')]:
        images={};files={}
        for kind,directory in [('reference',reference),('candidate',candidate)]:
            path=directory/('frame-%05d.png'%frame);files[kind]=dict(bytes=path.stat().st_size,sha256=sha(path))
            with Image.open(path) as original:
                assert original.size==(report['width'],report['height']) and original.format=='PNG'
                images[kind]=original.convert('RGB')
        difference=ImageChops.difference(images['reference'],images['candidate'])
        raw=describe(difference)
        smooth=describe(ImageChops.difference(images['reference'].filter(ImageFilter.GaussianBlur(1.2)),
                                             images['candidate'].filter(ImageFilter.GaussianBlur(1.2))))
        # 原生裁片保留玻璃、台面高光等高频细节，不能仅看缩小后的
        # 海报觉得“差不多”就掩盖随机噪点、锯齿或反射边界损失。
        for index,area in enumerate(((int(report['width']*.25),int(report['height']*.33),int(report['width']*.50),int(report['height']*.68)),
                                    (int(report['width']*.32),int(report['height']*.75),int(report['width']*.62),int(report['height']*.95)))):
            crops=[images[kind].crop(area) for kind in ('reference','candidate')]
            compare=Image.new('RGB',(crops[0].width*2,crops[0].height))
            compare.paste(crops[0],(0,0));compare.paste(crops[1],(crops[0].width,0))
            compare.save(out/('frame-%05d-native-crop-%d.png'%(frame,index)))
        panels=[]
        for kind in ('reference','candidate'):
            image=images[kind].copy();image.thumbnail((960,540));panels.append(image)
        overview=Image.new('RGB',(1920,580),(11,16,24));overview.paste(panels[0],(0,40));overview.paste(panels[1],(960,40))
        draw=ImageDraw.Draw(overview);draw.text((12,12),'REFERENCE / actual full resolution',fill='white')
        draw.text((972,12),'CANDIDATE / actual full resolution',fill='white');overview.save(out/('frame-%05d-overview.png'%frame))
        report['frames'].append(dict(frame=frame,files=files,raw=raw,gaussian_1_2_pixel=smooth))
        print('FRAME QUALITY',frame,json.dumps(raw),flush=True)
    (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')

if __name__=='__main__':main()
