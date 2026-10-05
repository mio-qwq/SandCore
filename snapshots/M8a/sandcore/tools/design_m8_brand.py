#!/usr/bin/env python3
"""mio：以可复算的 SVG 路径设计 SandCore 品牌壁纸，不调用图像生成服务。

用户要求设计感与克制：先用两条有方向的沙丘曲线建立轮廓，再选择
旋转、留白和折面。极光允许非常轻的材质层次；经典款则先在640×360
逻辑网格上逐点判定，输出的SVG全部由整数矩形组成，确实只有16种颜色。
高分辨率只影响栅格化尺寸，源曲线仍可编辑。文字严格取既有凤凰缓存，
不读宿主字体，更不会改用户的font16.txt。本脚本只产生设计资源，
系统安装与QEMU验证是另一条链，不能把这些预览当成系统截图。
"""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'assets/design'
OUT.mkdir(parents=True, exist_ok=True)
FONT = json.loads((ROOT / 'assets/font/phoenix-ascii.json').read_text(encoding='utf-8'))['rows16']

# 资源颜色集中命名，角色登记见GFX.md。照片的采样色不属于主题控件色。
# 同一组数值还会供数学图标使用，避免每个资源脚本各造一套近似颜色。
COLORS = {
    'paper': '#FBFCFE', 'white': '#FFFFFF', 'ice': '#E6F3F9',
    'mist': '#CAE6F0', 'cyan': '#57C5D8', 'accent': '#147F9F',
    'accent_deep': '#125E78', 'ink': '#173247', 'muted': '#5F788C',
    'line': '#D7E6ED', 'gold': '#EAC470', 'gold_deep': '#BF8D32',
    'sand': '#F4E5C9', 'green': '#4EA580', 'violet': '#817DD3',
    'rose': '#DC798A', 'shadow': '#17384D',
}
RETRO = [
    '#008080', '#005F64', '#0E1B28', '#101F3F',
    '#24618F', '#00A8A8', '#78DCDD', '#FFF8DF',
    '#FFD17F', '#A87020', '#AE5595', '#E4A6C6',
    '#B4BAB6', '#D9DED3', '#C67461', '#324551',
]

# 每段用四个控制点定义三次贝塞尔；两个轮廓都在尖端收拢，中部增厚。
# 顶部沙丘先上升、再舒展开；下部更短且向右上回收，形成开放的S负形。
# 它们是有意不等宽的两片沙，不是机械旋转的三叶风扇。
UPPER = [
    ((18,96),(72,23),(117,18),(160,42)),
    ((160,42),(202,67),(216,139),(282,105)),
    ((282,105),(250,153),(213,173),(173,142)),
    ((173,142),(127,107),(102,46),(58,64)),
    ((58,64),(42,72),(29,85),(18,96)),
]
LOWER = [
    ((0,154),(52,110),(82,109),(119,147)),
    ((119,147),(160,188),(207,217),(260,167)),
    ((260,167),(222,240),(168,253),(113,221)),
    ((113,221),(62,192),(42,144),(0,154)),
]

def curve_path(segments):
    start = segments[0][0]
    chunks = [f'M{start[0]} {start[1]}']
    for _, a, b, c in segments:
        chunks.append(f'C{a[0]} {a[1]} {b[0]} {b[1]} {c[0]} {c[1]}')
    return ' '.join(chunks) + ' Z'

def sampled(segments):
    # 经典款先求几何轮廓再做逐点覆盖；采样精度远小于一个逻辑像素，
    # 不需要随机噪声，重复运行必定产生同样的像素和摘要。
    points = []
    for p, a, b, c in segments:
        for step in range(40):
            t = step / 40
            inv = 1-t
            points.append(tuple(inv**3*p[k] + 3*inv**2*t*a[k] + 3*inv*t**2*b[k] + t**3*c[k] for k in range(2)))
    return points

def inside(x, y, polygon):
    hit = False
    previous = polygon[-1]
    for current in polygon:
        x0,y0 = previous; x1,y1 = current
        if (y0>y) != (y1>y) and x < x0+(y-y0)*(x1-x0)/(y1-y0):
            hit = not hit
        previous = current
    return hit

def pixel_text(text, x, y, scale, fill):
    # 字形行仍用bit15代表最左，ASCII只取8列；SVG的文字由像素路径
    # 构成，所以换机器也不会被系统字体替换，品牌与系统控件同源。
    rectangles = []
    for index, ch in enumerate(text):
        if not 32 <= ord(ch) <= 126:
            raise ValueError('品牌辅助文案只使用现有凤凰ASCII')
        for row, bits in enumerate(FONT[ord(ch)-32]):
            for col in range(8):
                if bits & (0x8000>>col):
                    rectangles.append(f'M{x+(index*8+col)*scale} {y+row*scale}h{scale}v{scale}h-{scale}Z')
    return f'<path d="{"".join(rectangles)}" fill="{fill}" shape-rendering="crispEdges"/>'

def modern():
    c = COLORS
    # Logo放在视觉重心右侧，左边给桌面留白。-12度旋转让沙丘有前进
    # 方向；高光只占窄边，不用几块玻璃/强光去补救本身无力的轮廓。
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="3840" height="2160" viewBox="0 0 1920 1080">
<title>SandCore Aurora — Dune / mio</title>
<defs>
 <linearGradient id="paper" x1="0" y1="0" x2="1" y2="1"><stop stop-color="{c['white']}"/><stop offset=".64" stop-color="{c['paper']}"/><stop offset="1" stop-color="{c['ice']}"/></linearGradient>
 <linearGradient id="crest" x1=".18" y1="0" x2=".75" y2="1"><stop stop-color="{c['cyan']}"/><stop offset=".26" stop-color="{c['accent']}"/><stop offset="1" stop-color="{c['accent_deep']}"/></linearGradient>
 <linearGradient id="core" x1="0" y1="0" x2=".85" y2="1"><stop stop-color="{c['sand']}"/><stop offset=".28" stop-color="{c['gold']}"/><stop offset="1" stop-color="{c['gold_deep']}"/></linearGradient>
 <linearGradient id="fold" x1="0" y1="0" x2=".4" y2="1"><stop stop-color="{c['white']}" stop-opacity=".72"/><stop offset="1" stop-color="{c['white']}" stop-opacity="0"/></linearGradient>
 <filter id="shadow" x="-.25" y="-.25" width="1.5" height="1.6"><feGaussianBlur stdDeviation="5"/></filter>
</defs>
<rect width="1920" height="1080" fill="url(#paper)"/>
<path d="M-120 902 C400 590 800 1170 2040 640 L2040 1160 L-120 1160Z" fill="{c['mist']}" opacity=".30"/>
<path d="M-120 940 C500 665 1130 1190 2040 716 L2040 786 C1220 1205 520 728 -120 1005Z" fill="{c['white']}" opacity=".64"/>
<g transform="translate(1238 385) rotate(-12 141 135) scale(1.2)">
 <g transform="translate(1 8)" opacity=".09" fill="{c['shadow']}" filter="url(#shadow)"><path d="{curve_path(UPPER)}"/><path d="{curve_path(LOWER)}"/><circle cx="139" cy="124" r="18"/></g>
 <path d="{curve_path(UPPER)}" fill="url(#crest)"/>
 <path d="{curve_path(LOWER)}" fill="url(#crest)"/>
 <path d="M18 96 C72 23 117 18 160 42 C118 30 76 36 18 96Z" fill="url(#fold)"/>
 <path d="M0 154 C52 110 82 109 119 147 C82 120 53 119 0 154Z" fill="url(#fold)"/>
 <circle cx="139" cy="124" r="18" fill="url(#core)"/>
</g>
{pixel_text('SandCore',1272,733,2,c['ink'])}
{pixel_text('Welcome to true color',1272,783,1,c['muted'])}
</svg>'''
    (OUT/'AURORA-DUNE.svg').write_text(svg,encoding='utf-8')
    # 独立透明标志复用同一曲线，便于开机/设置/图标使用；不把文字
    # 或桌面背景烘焙进品牌资源，否则小尺寸会产生无法辨认的装饰。
    mark = f'<svg xmlns="http://www.w3.org/2000/svg" width="1024" height="1024" viewBox="-32 -32 346 346"><title>SandCore Dune / mio</title><g transform="rotate(-12 141 135)"><path d="{curve_path(UPPER)}" fill="{c["accent"]}"/><path d="{curve_path(LOWER)}" fill="{c["accent_deep"]}"/><circle cx="139" cy="124" r="18" fill="{c["gold"]}"/></g></svg>'
    (OUT/'SANDCORE-MARK.svg').write_text(mark,encoding='utf-8')

def classic():
    w,h = 640,360
    upper,lower = sampled(UPPER),sampled(LOWER)
    pixels = bytearray([0])*(w*h)
    # 经典款不用灰阶透明混合：阴影、抖色和轮廓都是实际的16槽索引。
    # 点状织纹仅每4×4一处，色差很小，不用满屏小Logo抢走桌面信息。
    for y in range(h):
        for x in range(w):
            if x%4==0 and y%4==0: pixels[y*w+x]=1
    center=(462,160); angle=math.radians(10)
    cosine,sine = math.cos(angle),math.sin(angle)
    def classify(x,y):
        # 先逆旋转屏幕点，再判定母版轮廓。三倍缩小后才取像素中心，
        # 所以角度不会让边缘变成灰色，阶梯仍是明确的整像素。
        dx=(x-center[0])*2.1; dy=(y-center[1])*2.1
        px=141+cosine*dx-sine*dy; py=135+sine*dx+cosine*dy
        if (px-139)**2+(py-124)**2<18**2: return 8
        if inside(px,py,upper): return 6
        if inside(px,py,lower): return 10
        return 0
    for y in range(70,252):
        for x in range(365,557):
            current = classify(x+.5,y+.5)
            shadow = classify(x-2+.5,y-3+.5)
            if not current:
                if shadow: pixels[y*w+x]=3
                continue
            # 顶边一像素象牙色，下边一像素深蓝；内部只有双色交错，
            # 这是早期屏幕的假彩色材质，而不是降低现代渐变的色深。
            if not classify(x+.5,y-.5): color=7
            elif not classify(x+.5,y+1.5): color=3
            elif current==6: color=5 if (x+y)%4==0 else 6
            elif current==10: color=11 if (x-y)%4==0 else 10
            else: color=9 if (x+y)%5==0 else 8
            pixels[y*w+x]=color
    # 同源凤凰像素文字置于Logo下。两色偏移产生很轻的古典凹凸边。
    def draw_text(text,x,y,color):
        for index,ch in enumerate(text):
            for row,bits in enumerate(FONT[ord(ch)-32]):
                for col in range(8):
                    if bits&(0x8000>>col): pixels[(y+row)*w+x+index*8+col]=color
    draw_text('SandCore',429,249,3); draw_text('SandCore',428,248,7)
    rectangles=[]
    # 同色游程合并成一个SVG矩形；比逐像素230400个节点更小，同时
    # 保持完全等价的索引图。classic.index8供后续SCB槽位映射使用。
    for y in range(h):
        x=0
        while x<w:
            color=pixels[y*w+x]; end=x+1
            while end<w and pixels[y*w+end]==color: end+=1
            rectangles.append(f'<path d="M{x} {y}h{end-x}v1h-{end-x}Z" fill="{RETRO[color]}"/>')
            x=end
    svg='<svg xmlns="http://www.w3.org/2000/svg" width="3840" height="2160" viewBox="0 0 640 360" shape-rendering="crispEdges"><title>SandCore Classic — False color / mio</title>'+''.join(rectangles)+'</svg>'
    (OUT/'CLASSIC-DUNE.svg').write_text(svg,encoding='utf-8')
    (OUT/'CLASSIC-DUNE.index8').write_bytes(pixels)

if __name__ == '__main__':
    modern(); classic()
    (OUT/'tokens.json').write_text(json.dumps({'author':'mio','aurora':COLORS,'classic16':RETRO},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('SVG designed: Aurora, Classic, transparent mark; Classic has 16-color index source')
