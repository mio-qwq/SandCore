#!/usr/bin/env python3
"""mio：M8彩色资源用已登记槽位绘制，源图可审计，不引入第二种字体。

图标轮廓/层次在32x32像素上设计；亮边/背影统一位置。默认菜单在
数据盘是可编辑文字，应用名单不编进内核。只生成发布树，不修改用户盘。
"""
import re, struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
FS=ROOT/'build/fs'
PAL={name:int(number) for name,number in re.findall(r'#define\s+(PAL_\w+)\s+(\d+)',(ROOT/'kernel/palette.h').read_text(encoding='utf-8'))}
def write(path,data):
    p=FS/path;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data if isinstance(data,bytes) else data.encode('utf-8'))
def scb(w,h,pixels):return b'SCB1MIO\0'+struct.pack('<IIII',w,h,1,0x004F494D)+bytes(pixels)
def icon(kind):
    p=bytearray(32*32)
    def box(x,y,w,h,c):
        for yy in range(max(0,y),min(32,y+h)):
            for xx in range(max(0,x),min(32,x+w)):p[yy*32+xx]=c
    ink=PAL['PAL_UI_INK'];edge=PAL['PAL_UI_TEXT'];cyan=PAL['PAL_UI_CYAN'];gold=PAL['PAL_UI_GOLD']
    for y in range(5,28):box(5,y,24,1,ink);box(3,y-2,24,1,cyan+min(7,(28-y)//4))
    box(3,3,24,2,edge);box(3,3,2,23,cyan+7)
    if kind=='FILES':
        box(7,8,9,3,gold+7);box(7,12,17,11,gold+4);box(7,12,17,2,gold+7)
    elif kind=='LENS':
        box(7,8,17,15,ink);box(8,9,15,13,PAL['PAL_SKY']+5)
        for x in range(15):box(8+x,18-abs(x-7)//2,1,4,cyan+5)
        box(19,11,3,3,gold+7)
    elif kind=='CANVAS':
        box(7,8,17,15,edge)
        for i in range(13):box(9+i,20-i//2,2,2,cyan+7)
        box(21,8,3,7,gold+7)
    elif kind=='SETTINGS':
        for y in (10,15,20):box(7,y,17,1,edge)
        for x,y in ((11,9),(19,14),(14,19)):box(x,y,3,3,gold+7)
    elif kind=='MONITOR':
        for x,h in ((8,4),(12,8),(16,6),(20,11)):box(x,23-h,3,h,cyan+7)
        box(7,24,18,1,edge)
    elif kind in ('STUDIO','DEBUG'):
        for y,w in ((9,13),(13,9),(17,14),(21,7)):box(7,y,w,1,edge)
        if kind=='DEBUG':box(21,8,4,4,gold+7)
    elif kind in ('RACE','WORLD','LUMEN'):
        box(9,11,14,12,PAL['PAL_GRASS']+4 if kind=='WORLD' else gold+4)
        box(9,8,14,4,gold+7);box(7,22,4,3,ink);box(21,22,4,3,ink)
        if kind=='LUMEN':box(13,12,4,4,edge)
    else:
        box(7,9,17,2,edge);box(9,13,2,2,edge);box(11,15,2,2,edge);box(9,17,2,2,edge);box(16,19,7,2,gold+7)
    return scb(32,32,p)
def main():
    entries=[('Files','apps/files.scx','FILES'),('Studio','apps/ide.scx','STUDIO'),('Debug','apps/debugger.scx','DEBUG'),
             ('Settings','apps/settings.scx','SETTINGS'),('Lens','apps/lens.scx','LENS'),('Canvas','apps/canvas.scx','CANVAS'),
             ('Monitor','apps/monitor.scx','MONITOR'),('Shell','bin/shell.scx','APP'),('Lumen','apps/lumen.scx','LUMEN'),
             ('Race','apps/race.scx','RACE'),('World','apps/world.scx','WORLD'),('Notes','apps/note.scx','APP'),
             ('Palette','apps/palette.scx','CANVAS'),('Calculator','apps/calc.scx','APP'),('Mines','apps/mines.scx','WORLD')]
    for name in {e[2] for e in entries}:write('SYS/ICONS/'+name+'.SCB',icon(name))
    write('SYS/MENU.CFG','SMENU1MIO\n'+''.join(f'{name}|{cmd}|SYS/ICONS/{kind}.SCB\n' for name,cmd,kind in entries))
    write('SYS/DISPLAY.CFG','SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
    for name,cmd,kind in entries:
        if name in ('Files','Shell','Notes','Palette'):continue
        write(f'DESK/Z-{name.upper()}.LNK',f'{name}\n{cmd}\nSYS/ICONS/{kind}.SCB\n')
    # 欢迎图片用三个景深层与光轨；整幅只选已有槽，不携带任何字形。
    w,h=640,360;p=bytearray(w*h)
    for y in range(h):
        for x in range(w):
            sky=PAL['PAL_UI_NIGHT']+min(7,y*8//h)
            dune=245-((x-200)*(x-200)//2200)%35
            c=sky if y<dune else PAL['PAL_ROCK']+min(7,(y-dune)//18)
            if (x-490)**2+(y-96)**2<38**2:c=PAL['PAL_UI_GOLD']+7
            if abs(y-(190+x//9))<2:c=PAL['PAL_UI_CYAN']+7
            if y<160 and (x*37+y*71)%2003==0:c=PAL['PAL_STAR']
            p[y*w+x]=c
    write('HOME/WELCOME.SCB',scb(w,h,p))
    print('M8 assets: color icons, editable menu/display, welcome image')
if __name__=='__main__':main()
