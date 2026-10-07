"""Contact sheets of source-rendered NUI buffers. These are NOT guest captures."""
from pathlib import Path
import struct
from PIL import Image, ImageDraw
root=Path(__file__).resolve().parents[1]
for file in (root/'build/host').glob('*.ppm'):
    im=Image.open(file);im.save(file.with_suffix('.png'))
names=['sheet-200','blocks-200','hex-200','pcalc-200','raider-menu-200','raider-settings-200','raider-guide-200','installer-200']
out=Image.new('RGB',(1282,1042),'#d7e6ed');d=ImageDraw.Draw(out)
for i,name in enumerate(names):
    path=root/'build/host'/ (name+'.png')
    if not path.exists():continue
    im=Image.open(path);im.thumbnail((641,240));x=(i%2)*641;y=(i//2)*260
    d.text((x+8,y+2),name+' - source preview, 200%',fill='#173247');out.paste(im,(x,y+20))
out.save(root/'build/host/layout-review.png')
out=Image.new('RGB',(900,170),'#fbfcfe');d=ImageDraw.Draw(out)
for i,name in enumerate(['pcalc','sheet','hexed','blocks','raider','installer']):
    raw=(root/'build'/ (name+'.scb')).read_bytes();w,h=struct.unpack_from('<II',raw,8)
    im=Image.frombytes('RGBA',(w,h),raw[32:],'raw','BGRA').resize((96,96),Image.Resampling.NEAREST)
    out.paste(im,(i*150+27,18),im);d.text((i*150+45,126),name,fill='#173247')
out.save(root/'build/icons-preview.png')
