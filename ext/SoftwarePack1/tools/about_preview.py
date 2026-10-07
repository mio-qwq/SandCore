"""Six source-rendered About pages; not guest screenshots."""
from pathlib import Path
from PIL import Image, ImageDraw
root=Path(__file__).resolve().parents[1]
names=['pcalc-200','sheet-200','hex-200','blocks-night','raider','installer-200']
out=Image.new('RGB',(960,780),'#d7e6ed');draw=ImageDraw.Draw(out)
for i,name in enumerate(names):
    raw=root/'build/host'/('about-'+name+'.ppm')
    im=Image.open(raw);im.save(raw.with_suffix('.png'));im.thumbnail((480,236))
    x=(i%2)*480;y=(i//2)*260
    draw.text((x+12,y+5),name+' / source preview',fill='#173247')
    out.paste(im,(x,y+24))
out.save(root/'build/about-preview.png')
