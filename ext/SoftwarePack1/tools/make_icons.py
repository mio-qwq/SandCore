"""逐像素画六个 32px 图标，保留可编辑的 icons/*.txt 作为构建源。

使用 GFX.md 已登记的语义色：轮廓、金属、高光和不同应用的强调色。
不使用字体资源、第三方图形或生成式图像；每个物件有独立轮廓。
"""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
COLORS={'.':None,'K':'173247','N':'0B1018','M':'78828C','L':'D7E6ED',
        'W':'FBFCFE','I':'E6F3F9','C':'4BC9DB','B':'497DDD','G':'58BD73',
        'Y':'EAC470','O':'FF8B35','R':'ED3548','V':'A86BD4','S':'5F788C',
        'D':'806B51','F':'18202A'}


class Art:
    def __init__(self): self.p=[['.']*32 for _ in range(32)]
    def dot(self,x,y,c):
        if 0<=x<32 and 0<=y<32: self.p[y][x]=c
    def rect(self,x,y,w,h,c):
        for yy in range(y,y+h):
            for xx in range(x,x+w): self.dot(xx,yy,c)
    def line(self,x,y,xx,yy,c):
        steps=max(abs(xx-x),abs(yy-y),1)
        for i in range(steps+1): self.dot(round(x+(xx-x)*i/steps),round(y+(yy-y)*i/steps),c)
    def round(self,x,y,w,h,c):
        self.rect(x+1,y,w-2,h,c);self.rect(x,y+1,w,h-2,c)
    def tile(self,x,y,s,c):
        self.round(x,y,s,s,'K');self.rect(x+1,y+1,s-2,s-2,c);self.line(x+1,y+1,x+s-2,y+1,'W');self.line(x+1,y+2,x+1,y+s-3,'I')
    def save(self,name):
        header=f'# SoftwarePack1 1.1 / {name}: authored 32px silhouette\n32 32\n'
        header+='\n'.join(f'{k} {v}' for k,v in COLORS.items() if v)+'\n'
        (ROOT/'icons'/f'{name}.txt').write_text(header+'\n'.join(''.join(r) for r in self.p)+'\n',encoding='ascii')


def main():
    a=Art();a.round(7,4,21,27,'S');a.round(5,2,21,27,'K');a.round(6,3,19,25,'L');a.rect(7,4,17,1,'W')
    a.round(8,6,15,7,'K');a.rect(9,7,13,5,'C');a.rect(11,8,2,3,'K');a.rect(15,8,5,1,'W');a.rect(19,9,1,2,'W')
    for y in (15,20,25):
        for x in (8,13,18):a.tile(x,y,4,'Y' if x==18 else 'M')
    a.save('pcalc')
    a=Art();a.round(5,4,23,26,'S');a.round(3,2,23,26,'K');a.rect(4,3,20,23,'W');a.rect(4,3,20,5,'G');a.rect(5,4,17,1,'I')
    a.rect(21,2,5,5,'K');a.rect(22,3,3,3,'I');a.rect(24,2,2,2,'.')
    for x in (5,10,15,20):a.line(x,9,x,25,'L')
    for y in (9,13,17,21,25):a.line(5,y,23,y,'L')
    a.rect(11,14,4,3,'Y');a.rect(6,18,3,2,'G');a.rect(16,10,3,2,'B');a.save('sheet')
    a=Art();a.round(6,7,22,22,'S')
    for x in (8,13,18,23):a.rect(x,3,2,4,'Y');a.rect(x,27,2,4,'Y')
    for y in (8,13,18,23):a.rect(2,y,4,2,'Y');a.rect(27,y,4,2,'Y')
    a.round(5,5,23,23,'K');a.round(6,6,21,21,'M');a.rect(8,8,17,17,'N');a.rect(7,7,18,1,'I')
    a.rect(10,11,6,10,'C');a.rect(12,13,2,6,'N');a.line(18,13,23,19,'Y');a.line(23,13,18,19,'Y');a.rect(11,23,10,1,'S');a.save('hexed')
    a=Art();a.rect(4,28,25,2,'S')
    for x,y,c in [(5,19,'B'),(5,24,'B'),(10,24,'B'),(15,24,'Y'),(20,24,'Y'),(15,19,'Y'),(20,19,'Y'),(10,14,'V'),(15,14,'V'),(20,14,'V'),(15,9,'V'),(4,4,'C'),(9,4,'C'),(14,4,'C'),(19,4,'C')]:a.tile(x,y,6,c)
    a.save('blocks')
    a=Art()
    for y in range(32):
        for x in range(32):
            d=(x-16)**2+(y-14)**2
            if 100<=d<=144:a.dot(x,y,'K' if d>121 else 'C')
    a.line(16,0,16,8,'Y');a.line(16,20,16,27,'Y');a.line(2,14,10,14,'Y');a.line(22,14,30,14,'Y')
    a.rect(15,13,3,3,'R');a.line(8,26,22,18,'K');a.line(9,27,23,19,'M');a.line(10,28,24,20,'M');a.line(10,25,21,19,'W')
    a.rect(10,26,5,5,'K');a.rect(11,27,3,3,'D');a.rect(23,18,5,3,'K');a.rect(24,19,3,1,'O');a.save('raider')
    a=Art();a.round(5,18,24,12,'S');a.rect(4,17,24,12,'K');a.rect(5,18,22,10,'Y');a.rect(16,19,10,8,'D');a.line(5,18,26,18,'W')
    a.line(3,16,12,12,'K');a.line(12,12,27,16,'K');a.line(3,16,16,20,'K');a.line(27,16,16,20,'K');a.line(5,16,12,13,'I');a.line(12,13,25,16,'I')
    a.rect(12,2,8,12,'K');a.rect(13,3,6,10,'C')
    for yy,w in ((12,14),(13,12),(14,10),(15,8),(16,6),(17,4),(18,2)):a.rect(16-w//2,yy,w,1,'K');a.rect(17-w//2,yy,w-2,1,'C')
    a.rect(22,23,3,3,'G');a.save('installer')
    print('Redrew all six editable 32 x 32 icons')


if __name__=='__main__':main()
