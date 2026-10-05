#!/usr/bin/env python3
"""mio：发布ARGB图标和用户选定的壁纸；不生成另一套Logo。

现代图标从128px母版计算轮廓、渐变和柔和透明投影，3倍采样后缩小；
经典图标实际32px、固定16色、硬边，不给现代图标换灰色就称为复古。
所有物件色来自已登记tokens，渐变仅混合这些端点。字体不参与绘制。
照片和已选Logo只做无损RGBA格式转换，不改变源图的构图/颜色/尺寸。
"""
import json
import math
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

ROOT=Path(__file__).resolve().parent.parent
FS=ROOT/'build/fs'
TOKENS=json.loads((ROOT/'assets/design/tokens.json').read_text(encoding='utf-8'))
KINDS=('FILES','STUDIO','DEBUG','SETTINGS','LENS','CANVAS','MONITOR','SHELL','LUMEN','RACE','WORLD','NOTE','PALETTE','CALC','MINES')

def scb2(pic):
    w,h=pic.size
    if not 1<=w<=1920 or not 1<=h<=1080:raise ValueError('SCB2 dimensions outside contract')
    payload=pic.convert('RGBA').tobytes('raw','BGRA')
    return b'SCB2MIO\0'+struct.pack('<IIIIII',w,h,2,0,len(payload),0x004F494D)+payload

def write(path,data):
    target=FS/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)

def color(value):
    return tuple(int(value.lstrip('#')[i:i+2],16) for i in (0,2,4))

def icon(kind,classic=False):
    # 经典物件有实像素棱角；现代物件的图形仍共享可辨识轮廓，视觉身份
    # 由物件形状而不是同一个方形牌子承担。坐标在同一个128px设计网格上。
    scale=.25 if classic else 3
    n=int(128*scale)
    pic=Image.new('RGBA',(n,n))
    a={k:color(v) for k,v in TOKENS['aurora'].items()}
    if classic:
        p=[color(v) for v in TOKENS['classic16']]
        a.update(white=p[7],paper=p[13],gold=p[8],gold_deep=p[9],sand=p[13],
                 accent=p[4],accent_deep=p[3],cyan=p[6],ink=p[2],muted=p[15],
                 green=p[0],violet=p[10],rose=p[14],shadow=p[2],line=p[12],ice=p[6])
    def coords(box):return tuple(round(v*scale) for v in box)
    def layer(shape,base,light=None,radius=0,shadow=True):
        mask=Image.new('L',(n,n));d=ImageDraw.Draw(mask)
        if shape[0]=='poly':d.polygon([coords(point) for point in shape[1]],fill=255)
        elif shape[0]=='ellipse':d.ellipse(coords(shape[1]),fill=255)
        else:d.rounded_rectangle(coords(shape[1]),radius=round(radius*scale),fill=255)
        if shadow and not classic:
            sm=mask.filter(ImageFilter.GaussianBlur(3*scale))
            sm=sm.point(lambda x:x*48//255)
            sh=Image.new('RGBA',(n,n),a['shadow']+(0,));sh.putalpha(sm)
            shifted=Image.new('RGBA',(n,n));shifted.paste(sh,(0,round(3*scale)))
            pic.alpha_composite(shifted)
        top=a[light or base];bottom=a[base]
        paint=Image.new('RGBA',(n,n));pd=ImageDraw.Draw(paint)
        for y in range(n):
            t=y/max(1,n-1)
            rgb=tuple(round(x*(1-t)+z*t) for x,z in zip(top,bottom))
            pd.line((0,y,n,y),fill=rgb+(255,))
        paint.putalpha(mask);pic.alpha_composite(paint)
    def rect(box,c,light=None,r=0,shadow=False):layer(('rect',box),c,light,r,shadow)
    def poly(points,c,light=None,shadow=False):layer(('poly',points),c,light,0,shadow)
    def ellipse(box,c,light=None,shadow=False):layer(('ellipse',box),c,light,0,shadow)
    def line(points,c,width=4):
        ImageDraw.Draw(pic).line([coords(p) for p in points],fill=a[c]+(255,),width=max(1,round(width*scale)),joint='curve')
    if kind=='FILES':
        poly([(18,34),(51,34),(61,44),(108,44),(108,100),(18,100)],'gold_deep','gold',True)
        poly([(15,54),(112,54),(102,103),(21,103)],'gold','sand',True)
        line([(22,57),(103,57)],'white',2)
    elif kind in ('STUDIO','NOTE','CALC'):
        poly([(30,16),(79,16),(100,37),(100,110),(30,110)],'paper','white',True)
        poly([(79,16),(79,37),(100,37)],'line','ice')
        if kind=='CALC':
            rect((38,43,89,61),'accent_deep',r=3)
            for y in (71,86):
                for x in (40,58,76):rect((x,y,x+10,y+10),'muted',r=2)
        else:
            for y,w in ((47,46),(59,37),(71,44),(83,29)):line([(40,y),(40+w,y)],'accent' if kind=='STUDIO' else 'muted',3)
            if kind=='STUDIO':
                poly([(64,109),(108,53),(117,61),(73,117)],'gold_deep','gold',True)
                poly([(64,109),(73,117),(60,120)],'ink')
    elif kind in ('LENS','CANVAS','PALETTE'):
        if kind=='LENS':
            rect((14,26,114,101),'paper','white',r=6,shadow=True)
            rect((22,34,106,92),'accent','ice',r=2)
            ellipse((83,43,96,56),'gold','sand')
            poly([(22,88),(49,57),(65,77),(80,65),(106,90)],'green','cyan')
        elif kind=='CANVAS':
            # 画板用木质画架/矩形画布/斜笔，调色盘保留椭圆盘。
            # 即使16px任务栏也能靠轮廓区分，不再仅换名字共用图形。
            poly([(38,18),(46,18),(34,118),(24,118)],'gold_deep','gold',True)
            poly([(82,18),(90,18),(104,118),(94,118)],'gold_deep','gold',True)
            rect((22,28,106,96),'gold_deep','sand',r=2,shadow=True)
            rect((28,34,100,90),'paper','white',r=1)
            poly([(28,86),(49,58),(64,73),(82,52),(100,87)],'green','cyan')
            ellipse((35,40,48,53),'gold','sand')
            poly([(75,106),(111,65),(118,72),(83,112)],'accent_deep','accent',True)
            poly([(75,106),(83,112),(69,117)],'ink')
        else:
            ellipse((17,22,109,109),'sand','white',True)
            for box,c in [((30,43,48,61),'accent'),((57,30,75,48),'rose'),((82,48,100,66),'gold'),((64,78,82,96),'green')]:ellipse(box,c)
            # 笔尖和斜杆越出调色盘轮廓，透明外缘帮助小尺寸辨识。
            poly([(76,95),(107,29),(117,34),(86,100)],'gold_deep','gold',True)
            poly([(76,95),(86,100),(70,111)],'ink')
    elif kind=='SETTINGS':
        points=[]
        for i in range(64):
            angle=i*math.tau/64;r=43 if i%8 in (0,1,6,7) else 35
            points.append((64+math.cos(angle)*r,64+math.sin(angle)*r))
        poly(points,'muted','line',True)
        ellipse((39,39,89,89),'accent','cyan');ellipse((52,52,76,76),'paper','white')
    elif kind in ('MONITOR','SHELL','DEBUG'):
        rect((14,25,114,92),'ink','muted',r=6,shadow=True)
        rect((21,32,107,84),'accent_deep','accent',r=2)
        rect((55,92,73,104),'muted');rect((40,104,88,110),'line',r=3)
        if kind=='MONITOR':
            line([(28,66),(40,66),(47,45),(56,76),(64,59),(79,59),(88,42),(99,42)],'cyan',4)
        elif kind=='SHELL':
            line([(32,47),(44,58),(32,69)],'white',4);line([(54,70),(79,70)],'gold',4)
        else:
            ellipse((76,69,116,109),'rose','sand',True);line([(85,80),(105,100)],'ink',4);line([(85,100),(105,80)],'ink',4)
    elif kind=='WORLD':
        poly([(64,17),(109,42),(64,69),(19,43)],'green','cyan',True)
        poly([(19,43),(64,69),(64,116),(19,90)],'gold_deep','gold')
        poly([(64,69),(109,42),(109,90),(64,116)],'accent_deep','green')
        line([(64,69),(64,111)],'sand',2)
    elif kind=='RACE':
        poly([(44,27),(86,27),(102,79),(97,103),(29,103),(24,79)],'rose','paper',True)
        poly([(46,35),(82,35),(89,63),(39,63)],'accent_deep','cyan')
        rect((26,89,41,108),'ink',r=4);rect((85,89,100,108),'ink',r=4)
        rect((33,74,46,83),'gold','white',r=3);rect((81,74,94,83),'gold','white',r=3)
        line([(58,70),(68,70),(68,101),(58,101)],'paper',3)
    elif kind=='MINES':
        for angle in range(0,360,45):
            dx=math.cos(math.radians(angle));dy=math.sin(math.radians(angle))
            line([(64+dx*26,70+dy*26),(64+dx*42,70+dy*42)],'ink',5)
        ellipse((33,39,95,101),'ink','muted',True);ellipse((46,48,58,60),'white')
        line([(84,44),(101,25)],'gold',5);ellipse((99,17,109,27),'rose')
    else:
        ellipse((19,19,109,109),'accent_deep','cyan',True)
        poly([(65,25),(80,55),(106,64),(78,76),(64,105),(52,77),(25,64),(52,53)],'gold','white')
    if classic:
        # 只对已经32px的物件量化，透明度保持0/255；无现代抗锯齿/柔影。
        palette=Image.new('P',(1,1));colors=[color(v) for v in TOKENS['classic16']]
        palette.putpalette([n for c in colors for n in c]+[0]*(768-48))
        mask=pic.getchannel('A').point(lambda x:255 if x else 0)
        pic=pic.convert('RGB').quantize(palette=palette,dither=Image.Dither.NONE).convert('RGBA');pic.putalpha(mask)
    else:pic=pic.resize((128,128),Image.Resampling.LANCZOS)
    return pic

def main():
    atlas=Image.new('RGBA',(8*160,4*160),color(TOKENS['aurora']['paper'])+(255,))
    for classic in (False,True):
        group='CLASSIC' if classic else 'AURORA'
        for index,kind in enumerate(KINDS):
            pic=icon(kind,classic)
            write(f'SYS/ICONS/{group}/{kind}.SCB',scb2(pic))
            if not classic:write(f'SYS/ICONS/{kind}.SCB',scb2(pic))
            preview=pic.resize((128,128),Image.Resampling.NEAREST) if classic else pic
            atlas.alpha_composite(preview,((index%8)*160+16,(index//8+(2 if classic else 0))*160+16))
    out=ROOT/'build/m8-art';out.mkdir(parents=True,exist_ok=True);atlas.save(out/'icon-atlas.png')
    sources={'AURORA':'AURORA-DUNE-V2.png','SATIN':'AURORA-SATIN-V3.png','CLASSIC':'CLASSIC-LOW320.png','TIDAL':'TIDAL-FINE-V4.png'}
    for name,source in sources.items():
        with Image.open(ROOT/'assets/wallpapers'/source) as pic:write(f'SYS/WALL/{name}.SCB',scb2(pic))
    # mio：用户2026-10-04指定独立的三棱镜/彩虹宣传艺术图作Lens默认。
    # 复制真实PNG原字节，不把SCB改扩展名，也不替换三环实时短片。
    # 壁纸仍保留用户选定的三主题资源，宣传图采用独立机位和构图。
    # 构建仅检查尺寸/格式，资源不在构建时悄悄重绘或改变色彩。
    welcome=ROOT/'assets/pictures/WELCOME-TRUECOLOR-V1.png'
    with Image.open(welcome) as original:
        if original.format!='PNG' or original.width>1920 or original.height>1080:
            raise ValueError('WELCOME must be a real PNG within 1920x1080')
        original.verify()
    write('SYS/PICTURES/WELCOME.PNG',welcome.read_bytes())
    print('ARGB resources: 128px Aurora / real 32px 16-color Classic; selected source wallpapers')
if __name__=='__main__':main()
