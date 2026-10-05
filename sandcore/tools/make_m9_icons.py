#!/usr/bin/env python3
"""M9原创Sound双主题资源，Shell严格复用原两主题字节。"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from PIL import Image, ImageDraw, ImageFilter
from mkfs_m9 import read_image

ROOT=Path(__file__).resolve().parents[1]
TOKENS=json.loads((ROOT/'assets/design/tokens.json').read_text(encoding='utf-8'))


def rgb(text):
    return tuple(int(text.lstrip('#')[i:i+2],16) for i in (0,2,4))


def aurora(kind):
    # 128px母版的四倍采样，只混合已登记角色；透明阴影是物件层次，
    # 不给每个工具套同一块彩色底牌，缩到任务栏仍由轮廓辨识。
    n,s=512,4
    colors={key:rgb(value) for key,value in TOKENS['aurora'].items()}
    pic=Image.new('RGBA',(n,n))
    def box(points):return tuple(round(v*s) for v in points)
    def mask(shape,coords,r=0):
        out=Image.new('L',(n,n));d=ImageDraw.Draw(out)
        if shape=='oval':d.ellipse(box(coords),fill=255)
        else:d.rounded_rectangle(box(coords),radius=r*s,fill=255)
        return out
    def paint(shape,coords,top,bottom=None,r=0,shadow=False):
        m=mask(shape,coords,r)
        if shadow:
            shade=Image.new('RGBA',(n,n),colors['shadow']+(0,))
            shade.putalpha(m.filter(ImageFilter.GaussianBlur(2.5*s)).point(lambda v:v*48//255))
            pic.alpha_composite(shade,(0,3*s))
        layer=Image.new('RGBA',(n,n));d=ImageDraw.Draw(layer)
        a,b=colors[top],colors[bottom or top]
        y0,y1=coords[1],coords[3]
        for y in range(n):
            t=max(0,min(1,(y/s-y0)/max(1,y1-y0)))
            c=tuple(round(v*(1-t)+w*t) for v,w in zip(a,b))
            d.line((0,y,n,y),fill=c+(255,))
        layer.putalpha(m);pic.alpha_composite(layer)
    def line(points,color,width):
        d=ImageDraw.Draw(pic)
        d.line([box(p) for p in points],fill=colors[color]+(255,),width=width*s,joint='curve')
    if kind=='SHELL':
        paint('rect',(14,22,114,99),'muted','ink',8,True)
        paint('rect',(18,26,110,94),'white','line',5)
        paint('rect',(22,30,106,89),'ink','accent_deep',3)
        paint('rect',(22,30,106,42),'accent','accent_deep',3)
        for x,c in ((29,'rose'),(36,'gold'),(43,'green')):paint('oval',(x,34,x+3,37),c)
        line([(31,52),(41,60),(31,68)],'cyan',4)
        line([(48,68),(64,68)],'gold',4)
        line([(48,52),(80,52)],'ice',2)
        line([(70,60),(91,60)],'muted',2)
        line([(31,78),(53,78)],'muted',2)
        line([(60,78),(84,78)],'accent',2)
        paint('rect',(57,100,71,105),'line','muted',2)
        paint('rect',(44,106,84,112),'white','line',3,True)
    else:
        # 开放的耳机轮廓，外圈金属、内圈软垫与两侧耳罩独立形成深度。
        d=ImageDraw.Draw(pic)
        for bounds,c,w in (((25,17,103,110),'muted',10),((28,20,100,108),'line',6),((31,25,97,110),'ink',4)):
            d.arc(box(bounds),180,360,fill=colors[c]+(255,),width=w*s)
        line([(28,62),(28,77)],'muted',5);line([(100,62),(100,77)],'muted',5)
        paint('rect',(21,65,36,103),'line','muted',5,True)
        paint('rect',(30,64,44,105),'accent','accent_deep',6)
        paint('rect',(33,68,42,101),'ink','shadow',4)
        paint('rect',(92,65,107,103),'line','muted',5,True)
        paint('rect',(84,64,98,105),'accent','accent_deep',6)
        paint('rect',(86,68,95,101),'ink','shadow',4)
        line([(23,73),(23,94)],'white',2);line([(103,73),(103,94)],'white',2)
        # 中央小音符留下足够空白，暖金和冷青保持整套桌面的色彩纪律。
        line([(67,49),(67,82)],'gold_deep',5)
        line([(67,49),(80,46),(80,55),(67,58)],'gold',4)
        paint('oval',(54,76,69,87),'gold','gold_deep')
        line([(48,110),(58,113),(74,113)],'muted',2)
    return pic.resize((128,128),Image.Resampling.LANCZOS)


def classic(kind):
    # 直接在32x32格上设计，以登记的16色画棱角/高光；不由现代图量化。
    pic=Image.new('RGBA',(32,32));d=ImageDraw.Draw(pic)
    p=[rgb(value)+(255,) for value in TOKENS['classic16']]
    def rect(box,c):d.rectangle(box,fill=p[c])
    def line(points,c,w=1):d.line(points,fill=p[c],width=w)
    if kind=='SHELL':
        rect((3,5,29,25),2);rect((3,5,27,23),13)
        line([(3,23),(3,5),(27,5)],7);line([(4,22),(26,22),(26,6)],12)
        rect((5,7,25,20),3);rect((5,7,25,9),4)
        rect((7,8,8,8),14);rect((10,8,11,8),8);rect((13,8,14,8),6)
        line([(8,12),(11,14),(8,16)],6);rect((13,16,18,17),8)
        rect((13,24,18,26),12);rect((10,27,22,28),13);line([(10,27),(21,27)],7)
    else:
        line([(5,19),(5,10),(8,6),(12,4),(20,4),(24,6),(27,10),(27,19)],2,3)
        line([(5,16),(5,10),(9,6),(12,5),(20,5),(23,6),(26,10),(26,16)],13)
        line([(7,17),(7,11),(10,8),(13,7),(19,7),(22,8),(25,11),(25,17)],0)
        rect((3,17,10,25),2);rect((3,17,8,24),12);line([(3,24),(3,17),(8,17)],7)
        rect((8,16,11,25),4);line([(8,17),(8,23)],6)
        rect((22,17,29,25),2);rect((23,17,28,24),12);line([(23,17),(28,17),(28,23)],7)
        rect((21,16,24,25),4);line([(22,17),(22,23)],6)
        rect((17,11,18,21),9);rect((17,11,21,12),8);rect((20,11,21,15),8)
        d.ellipse((13,20,18,23),fill=p[8]);line([(12,27),(16,28),(20,27)],15)
    return pic


def scb(pic):
    data=pic.tobytes('raw','BGRA');w,h=pic.size
    return b'SCB2MIO\0'+struct.pack('<6I',w,h,2,0,len(data),0x004F494D)+data


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree',type=Path,required=True)
    parser.add_argument('--preview',type=Path,required=True)
    parser.add_argument('--shell-source-image',type=Path,required=True)
    args=parser.parse_args();args.preview.mkdir(parents=True,exist_ok=True)
    report=dict(author='mio',source='tools/make_m9_icons.py',palette='assets/design/tokens.json',icons=[])
    original=args.shell_source_image.read_bytes();source_digest=hashlib.sha256(original).hexdigest()
    records=read_image(original)
    report['shell_original_source']=dict(path=str(args.shell_source_image),sha256=source_digest,files=[])
    for name in ('SYS/ICONS/AURORA/SHELL.SCB','SYS/ICONS/CLASSIC/SHELL.SCB','SYS/ICONS/SHELL.SCB'):
        data=records[name].payload
        if data is None or data[:8]!=b'SCB2MIO\0':raise ValueError('原Shell主题图标缺失或格式不符')
        path=args.tree/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        report['shell_original_source']['files'].append(dict(path=name,sha256=hashlib.sha256(data).hexdigest()))
    for group,draw in (('AURORA',aurora),('CLASSIC',classic)):
        for kind in ('SOUND',):
            pic=draw(kind);data=scb(pic);path=args.tree/'SYS/ICONS'/group/(kind+'.SCB')
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
            pic.save(args.preview/(group+'-'+kind+'.png'))
            if group=='AURORA':(args.tree/'SYS/ICONS'/(kind+'.SCB')).write_bytes(data)
            if group=='CLASSIC':
                assert len(set(pic.convert('RGB').getdata()))<=17
                assert set(pic.getchannel('A').getdata())<={0,255}
            report['icons'].append(dict(theme=group,name=kind,size=pic.size,sha256=hashlib.sha256(data).hexdigest()))
    (args.preview/'resources.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    if hashlib.sha256(args.shell_source_image.read_bytes()).hexdigest()!=source_digest:raise RuntimeError('原图标来源盘改变')
    print('M9 Sound: Aurora 128px / Classic original 32px; Shell original bytes preserved')


if __name__=='__main__':main()
