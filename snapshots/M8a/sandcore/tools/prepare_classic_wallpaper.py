#!/usr/bin/env python3
"""mio：按用户明确要求，把选定的仿古壁纸实际处理成低像素/有限色。

输入是CLASSIC-FALSE-V3生成原图，不采用被否定的数学Logo。逻辑图
320×180，最近邻取样，16色固定表量化且不使用误差扩散：原图的像素
边缘不能被滤镜磨成灰边。预览按整数6倍放大，实际系统用逻辑索引
图缩放；PNG为P模式而非真彩色。只产生派生资源，不覆盖原始生成图。
调色表使用已登记的Classic16，后续上盘SCB按正式DAC槽位转换。
"""
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'assets/wallpapers'
source=OUT/'CLASSIC-FALSE-V3.png'
colors=json.loads((ROOT/'assets/design/tokens.json').read_text(encoding='utf-8'))['classic16']
palette=[]
for rgb in colors:
    palette.extend(int(rgb[i:i+2],16) for i in (1,3,5))
reference=Image.new('P',(1,1));reference.putpalette(palette+[0]*(768-len(palette)))
image=Image.open(source).convert('RGB').resize((320,180),Image.Resampling.NEAREST)
image=image.quantize(palette=reference,dither=Image.Dither.NONE)
target=OUT/'CLASSIC-LOW320.png'
image.save(target,optimize=True)
image.resize((1920,1080),Image.Resampling.NEAREST).save(OUT/'CLASSIC-LOW320-preview.png',optimize=True)
record={'author':'mio','source':source.name,'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'output':target.name,'size':[320,180],'mode':'P','color_count':len(image.getcolors()),
        'palette':colors,'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
        'preview':'nearest-neighbor 6x, 1920x1080'}
(OUT/'classic-processing.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(record['size'],record['mode'],record['color_count'],'colors')
