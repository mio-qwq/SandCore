#!/usr/bin/env python3
"""mio：Welcome to true color. 的可复现Blender离线渲染工程。

在Blender的Python中执行，不在SandCore/普通Python中导入bpy。
六幕共用真实封闭玻璃、金属台座、光谱与灯光，摄影机独立构图；
彩色出射光为美术光学装置，不把RGB引擎称为完整波长色散模拟。
母版逐帧路径追踪，固定采样种子/法线反照率去噪/16位输出；
草图是另外的规格与目录，不以草图或插帧冒充最终4K/60FPS。
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import sys
import time

import bpy
import bmesh
from mathutils import Matrix, Vector

AUTHOR = 'mio'
FPS = 60
SECONDS = 42
# 先在GFX.md登记端点，再在此引用；连续色由相邻端点插值得到。
COLORS = dict(ink='0B1018', floor='18202A', metal='78828C', ivory='F4EEE2',
              glass='9DD2D8', optical='F3FAFC', red='ED3548', orange='FF8B35', yellow='F4DD45',
              green='58BD73', cyan='4BC9DB', blue='497DDD', violet='A86BD4')
SPECTRUM = [COLORS[name] for name in ('red','orange','yellow','green','cyan','blue','violet')]
# 每幕以秒为边界；镜头硬切发生于边界，幕内使用平滑速度而非线性急停。
SHOTS = [
    (0,6,(-4.8,-4.8,1.65),(-2.7,-3.7,1.45),(-.75,0,.95),(-.15,0,.90),65,'A single ray.'),
    (6,13,(3.2,-5.8,2.35),(2.2,-4.9,2.05),(0,0,.92),(0,0,.94),62,'Glass. Light. Possibility.'),
    (13,20,(6.8,-7.9,3.35),(5.7,-6.6,2.7),(1.25,.30,.95),(1.05,.3,.95),48,'Every color, revealed.'),
    (20,27,(2.4,-7.7,4.4),(1.0,-7.3,3.65),(.55,1.25,1.7),(.45,1.1,1.6),42,'A wider world.'),
    (27,34,(-4.5,-8.6,3.5),(-2.1,-8.9,3.2),(.45,1.0,1.35),(.7,.9,1.2),46,'Made of light.'),
    (34,42,(6.0,-10.4,3.5),(5.0,-10.0,3.05),(1.5,.15,1.0),(1.4,.15,1.0),45,'Welcome to\ntrue color.'),
]


def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as src:
        for block in iter(lambda:src.read(1024*1024),b''):h.update(block)
    return h.hexdigest()


def linear(hexcode):
    """美术端点是sRGB，节点接受线性光；不把编码值直接当光能。"""
    rgb=[int(hexcode[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in rgb)+(1,)


def smooth(t):return t*t*(3-2*t)
def mix(a,b,t):return tuple(x+(y-x)*t for x,y in zip(a,b))


def spectral(t):
    position=max(0,min(1,t))*6;i=min(5,int(position))
    return mix(linear(SPECTRUM[i]),linear(SPECTRUM[i+1]),position-i)


def surface(name,color,roughness=.2,metallic=0,transmission=0):
    mat=bpy.data.materials.new(name);mat.use_nodes=True
    node=mat.node_tree.nodes.get('Principled BSDF')
    node.inputs['Base Color'].default_value=linear(color)
    node.inputs['Roughness'].default_value=roughness
    node.inputs['Metallic'].default_value=metallic
    node.inputs['Transmission Weight'].default_value=transmission
    node.inputs['IOR'].default_value=1.5168 if transmission else 1.45
    return mat


def emitter(name,color,power):
    mat=bpy.data.materials.new(name);mat.use_nodes=True
    nodes=mat.node_tree.nodes;nodes.clear()
    emit=nodes.new('ShaderNodeEmission');emit.inputs['Color'].default_value=color
    emit.inputs['Strength'].default_value=power
    # 独立透明混合让“关闭的光”真正消失，避免强度0变成黑色实体条。
    transparent=nodes.new('ShaderNodeBsdfTransparent');blend=nodes.new('ShaderNodeMixShader')
    blend.inputs[0].default_value=.42
    mat.node_tree.links.new(transparent.outputs[0],blend.inputs[1])
    mat.node_tree.links.new(emit.outputs[0],blend.inputs[2])
    output=nodes.new('ShaderNodeOutputMaterial')
    mat.node_tree.links.new(blend.outputs[0],output.inputs['Surface'])
    return mat,emit


def finish(obj,name,mat):
    obj.name=name;obj.data.materials.append(mat)
    return obj


def bevel(obj,width=.025,segments=4):
    modifier=obj.modifiers.new('精细倒角捕捉灯箱高光','BEVEL')
    modifier.width=width;modifier.segments=segments
    return obj


def box(name,location,scale,mat,rounded=.025):
    bpy.ops.mesh.primitive_cube_add(size=1,location=location)
    obj=finish(bpy.context.object,name,mat);obj.dimensions=scale
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if rounded:bevel(obj,rounded)
    return obj


def curve(name,points,radius,mat):
    # 高细分三维曲线是可见光束/彩虹装置，不是屏幕后贴的静态光条。
    data=bpy.data.curves.new(name,'CURVE');data.dimensions='3D'
    data.bevel_depth=radius;data.bevel_resolution=4;data.resolution_u=32
    spline=data.splines.new('POLY');spline.points.add(len(points)-1)
    for target,point in zip(spline.points,points):target.co=(*point,1)
    obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
    data.materials.append(mat);return obj


def aim(obj,target):
    obj.rotation_euler=(Vector(target)-obj.location).to_track_quat('-Z','Y').to_euler()


def area(name,location,target,power,size,color):
    # 摄影棚使用条形灯箱；圆盘灯在棱镜正面产生一个巨大的白色圆斑，
    # 会抢掉折射和色彩的层次。长方形高光仍由真实玻璃反射计算。
    data=bpy.data.lights.new(name,'AREA');data.energy=power;data.shape='RECTANGLE'
    data.size=size;data.size_y=size*.38;data.color=linear(color)[:3]
    obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
    obj.location=location;aim(obj,target);return obj


def spot(name,location,target,power,color,angle):
    # 光束照亮的雾和地面都由路径追踪计算，不能只画发光实线冒充光照。
    data=bpy.data.lights.new(name,'SPOT');data.energy=power;data.color=color[:3]
    data.spot_size=angle;data.spot_blend=.60;data.shadow_soft_size=.006
    obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
    obj.location=location;aim(obj,target);return data


def beam_volume(name,start,end,color,near_radius,far_radius,power):
    """静态光场缓存：把均匀稀雾中的窄光路预定义为柔边发射体积。

    这里保留摄影机射线的真实体积积分/玻璃透射/反射，避免整间
    空气在每个采样点重新查询35道狭窄光源。出射谱本来就是美术
    装置，所以采用明确的高斯径向光场与沿程衰减，不宣称完整
    色散/多次散射；地面与实体照明仍由真正的对应聚光灯计算。
    """
    vector=Vector(end)-Vector(start);length=vector.length
    mat=bpy.data.materials.new(name+' / Gaussian lightfield');mat.use_nodes=True
    nodes=mat.node_tree.nodes;nodes.clear();links=mat.node_tree.links
    coord=nodes.new('ShaderNodeTexCoord');split=nodes.new('ShaderNodeSeparateXYZ')
    links.new(coord.outputs['Generated'],split.inputs[0])
    def arithmetic(operation,a,b=None):
        node=nodes.new('ShaderNodeMath');node.operation=operation
        if isinstance(a,(float,int)):node.inputs[0].default_value=a
        else:links.new(a,node.inputs[0])
        if b is not None:
            if isinstance(b,(float,int)):node.inputs[1].default_value=b
            else:links.new(b,node.inputs[1])
        return node.outputs[0]
    ratio=near_radius/far_radius
    width=arithmetic('ADD',ratio,arithmetic('MULTIPLY',split.outputs['Z'],1-ratio))
    xx=arithmetic('DIVIDE',arithmetic('SUBTRACT',split.outputs['X'],.5),width)
    yy=arithmetic('DIVIDE',arithmetic('SUBTRACT',split.outputs['Y'],.5),width)
    squared=arithmetic('ADD',arithmetic('MULTIPLY',xx,xx),arithmetic('MULTIPLY',yy,yy))
    falloff=arithmetic('EXPONENT',arithmetic('MULTIPLY',squared,-28))
    depth=arithmetic('MULTIPLY',split.outputs['Z'],length)
    attenuation=arithmetic('ADD',1,arithmetic('MULTIPLY',depth,depth))
    # 时序亮度只改变这一项，静态径向/沿程光场仍复用。此前仅让聚光灯
    # 淡入，发射体积本身却突然出现，甚至在白光源尚暗时已看见白线；
    # 两者必须使用同一幕内包络，才能保持因果和连续画面的观感。
    gain=nodes.new('ShaderNodeValue');gain.name='幕内光能包络'
    gain.outputs[0].default_value=1
    weight=arithmetic('DIVIDE',arithmetic('MULTIPLY',
        arithmetic('MULTIPLY',falloff,power),gain.outputs[0]),attenuation)
    emit=nodes.new('ShaderNodeEmission');emit.inputs['Color'].default_value=color
    links.new(weight,emit.inputs['Strength']);output=nodes.new('ShaderNodeOutputMaterial')
    links.new(emit.outputs[0],output.inputs['Volume'])
    bpy.ops.mesh.primitive_cone_add(vertices=64,radius1=near_radius,radius2=far_radius,
                                   depth=length,location=(Vector(start)+Vector(end))/2)
    obj=finish(bpy.context.object,name,mat);obj.rotation_euler=vector.to_track_quat('Z','Y').to_euler()
    return obj,gain


class LightField:
    """共享连续光场节点，避免把一张光谱拆成35个重叠体积。

    原35道光源仍负责实体/地面照明。这里只将可见空气中的光谱
    写成单一闭合体积的连续颜色函数，减少每条摄影机射线反复
    进入/退出几十个体积边界；不是降低像素、采样或光线反弹数。
    坐标使用Object局部空间，摄影机移动不会把光谱贴到屏幕上。
    """
    def __init__(self,name):
        self.material=bpy.data.materials.new(name);self.material.use_nodes=True
        self.nodes=self.material.node_tree.nodes;self.nodes.clear()
        self.links=self.material.node_tree.links
        coordinates=self.nodes.new('ShaderNodeTexCoord')
        xyz=self.nodes.new('ShaderNodeSeparateXYZ')
        self.links.new(coordinates.outputs['Object'],xyz.inputs[0])
        self.x,self.y,self.z=(xyz.outputs[key] for key in ('X','Y','Z'))

    def math(self,operation,a,b=None):
        node=self.nodes.new('ShaderNodeMath');node.operation=operation
        for i,value in enumerate((a,) if b is None else (a,b)):
            if isinstance(value,(int,float)):node.inputs[i].default_value=value
            else:self.links.new(value,node.inputs[i])
        return node.outputs[0]

    def soft(self,value):
        value=self.math('MINIMUM',1,self.math('MAXIMUM',0,value))
        return self.math('MULTIPLY',self.math('MULTIPLY',value,value),
                         self.math('SUBTRACT',3,self.math('MULTIPLY',2,value)))

    def spectrum(self,value):
        node=self.nodes.new('ShaderNodeValToRGB');ramp=node.color_ramp
        ramp.interpolation='LINEAR';ramp.elements[0].color=linear(SPECTRUM[0])
        ramp.elements[1].color=linear(SPECTRUM[-1])
        for i in range(1,6):ramp.elements.new(i/6).color=linear(SPECTRUM[i])
        self.links.new(value,node.inputs[0]);return node.outputs['Color']

    def finish(self,color,power):
        gain=self.nodes.new('ShaderNodeValue');gain.name='幕内光能包络'
        gain.outputs[0].default_value=1
        emission=self.nodes.new('ShaderNodeEmission')
        self.links.new(color,emission.inputs['Color'])
        self.links.new(self.math('MULTIPLY',power,gain.outputs[0]),emission.inputs['Strength'])
        output=self.nodes.new('ShaderNodeOutputMaterial')
        self.links.new(emission.outputs[0],output.inputs['Volume'])
        return self.material,gain


def field_mesh(name,vertices,faces,material,location=(0,0,0)):
    """封闭光场边界只限定积分域，不给空气加一个不透明表面。"""
    mesh=bpy.data.meshes.new(name);mesh.from_pydata(vertices,[],faces);mesh.update()
    bm=bmesh.new();bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
    obj=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(obj)
    obj.location=location;obj.data.materials.append(material);return obj


def spectral_fan():
    start=Vector((.52,.015,.89));red=Vector((6.3,.4,1.43));violet=Vector((6.3,2.15,.18))
    across=(violet-red).normalized();normal=Vector((1,0,0)).cross(across)
    center=(red+violet)/2-start;length=center.x;width=(violet-red).length
    drift=center.dot(across);rise=center.dot(normal)
    near_width=.025;near_depth=.012;far_depth=.095
    field=LightField('连续出射光谱 / 单体积柔边光场')
    u=field.math('DIVIDE',field.x,length)
    span=field.math('ADD',near_width,field.math('MULTIPLY',u,width-near_width))
    t=field.math('ADD',.5,field.math('DIVIDE',
        field.math('SUBTRACT',field.y,field.math('MULTIPLY',u,drift)),span))
    edge=field.soft(field.math('MULTIPLY',12,field.math('MINIMUM',t,field.math('SUBTRACT',1,t))))
    thickness=field.math('ADD',near_depth,field.math('MULTIPLY',u,far_depth-near_depth))
    cross=field.math('DIVIDE',field.math('SUBTRACT',field.z,field.math('MULTIPLY',u,rise)),thickness)
    soft_depth=field.math('EXPONENT',field.math('MULTIPLY',-5,field.math('MULTIPLY',cross,cross)))
    distance=field.math('MULTIPLY',u,length)
    power=field.math('DIVIDE',field.math('MULTIPLY',35,field.math('MULTIPLY',edge,soft_depth)),
                     field.math('ADD',1,field.math('MULTIPLY',distance,distance)))
    material,gain=field.finish(field.spectrum(t),power)
    vertices=[]
    for x,y,z,w,h in ((0,0,0,near_width/2,near_depth),(length,drift,rise,width/2,far_depth)):
        vertices.extend(((x,y-w,z-h),(x,y+w,z-h),(x,y+w,z+h),(x,y-w,z+h)))
    obj=field_mesh('单体积连续光谱 / 摄影机真实体积积分',vertices,
                   [(0,3,2,1),(4,5,6,7),(0,1,5,4),(1,2,6,5),(2,3,7,6),(3,0,4,7)],material)
    basis=Matrix(((1,across.x,normal.x),(0,across.y,normal.y),(0,across.z,normal.z))).to_4x4()
    basis.translation=start;obj.matrix_world=basis
    return obj,gain


def rainbow_field():
    # 彩虹是柔和的连续体积，不再用35根硬轮廓圆管。径向颜色连续、
    # 内外缘和两端平滑衰减，玻璃里仍可见实际的反射/透射彩虹。
    outer=3.35;band=.63;depth=.085;segments=160
    field=LightField('彩虹 / 连续径向光谱与柔边体积')
    radius=field.math('SQRT',field.math('ADD',field.math('MULTIPLY',field.x,field.x),
                                     field.math('MULTIPLY',field.z,field.z)))
    t=field.math('DIVIDE',field.math('SUBTRACT',outer,radius),band)
    radial=field.soft(field.math('MULTIPLY',8,field.math('MINIMUM',t,field.math('SUBTRACT',1,t))))
    ends=field.soft(field.math('DIVIDE',field.math('SUBTRACT',
        field.math('DIVIDE',field.z,radius),math.sin(math.radians(12))),.18))
    cross=field.math('DIVIDE',field.y,depth)
    thickness=field.math('EXPONENT',field.math('MULTIPLY',-5,field.math('MULTIPLY',cross,cross)))
    power=field.math('MULTIPLY',7,field.math('MULTIPLY',radial,field.math('MULTIPLY',ends,thickness)))
    material,gain=field.finish(field.spectrum(t),power)
    vertices=[];faces=[]
    for j in range(segments+1):
        angle=math.radians(12+j*156/segments);c,s=math.cos(angle),math.sin(angle)
        vertices.extend(((outer*c,-depth,outer*s),((outer-band)*c,-depth,(outer-band)*s),
                         ((outer-band)*c,depth,(outer-band)*s),(outer*c,depth,outer*s)))
    for j in range(segments):
        a=j*4;b=a+4
        for k in range(4):faces.append((a+k,a+(k+1)%4,b+(k+1)%4,b+k))
    faces.extend(((0,3,2,1),(segments*4,segments*4+1,segments*4+2,segments*4+3)))
    obj=field_mesh('柔和光谱彩虹弧 / 连续闭合体积',vertices,faces,material,(1.10,3.15,.04))
    return obj,gain


def make_prism(glass):
    # 三角柱必须真正封闭且法线朝外，避免薄面玻璃只有镜面而无折射。
    triangle=[(-.82,.16),(.82,.16),(0,1.58)]
    vertices=[(x,y,z) for y in (-.58,.58) for x,z in triangle]
    mesh=bpy.data.meshes.new('Optical solid / closed triangular prism')
    mesh.from_pydata(vertices,[],[(0,2,1),(3,4,5),(0,1,4,3),(1,2,5,4),(2,0,3,5)])
    mesh.update();bm=bmesh.new();bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(mesh);bm.free()
    prism=bpy.data.objects.new('玻璃三棱镜：真实封闭光学实体',mesh)
    bpy.context.collection.objects.link(prism);prism.data.materials.append(glass)
    bevel(prism,.009,6)
    return prism


def text_object(name,font,material):
    data=bpy.data.curves.new(name,'FONT');data.font=font;data.size=1
    data.align_x='LEFT';data.align_y='TOP_BASELINE';data.space_character=1.02
    obj=bpy.data.objects.new(name,data);bpy.context.collection.objects.link(obj)
    data.materials.append(material);return obj


def setup(args):
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene;scene.render.engine='CYCLES';scene.cycles.device='CPU'
    scene.render.threads_mode='FIXED';scene.render.threads=os.cpu_count() or 12
    scene.cycles.samples=args.samples;scene.cycles.use_adaptive_sampling=True
    scene.cycles.adaptive_threshold=.003 if args.mode=='master' else .018
    scene.cycles.adaptive_min_samples=128 if args.mode=='master' else 16
    scene.cycles.seed=821104;scene.cycles.use_animated_seed=False
    scene.cycles.use_denoising=True;scene.cycles.denoiser='OPENIMAGEDENOISE'
    scene.cycles.denoising_input_passes='RGB_ALBEDO_NORMAL'
    scene.cycles.denoising_prefilter='ACCURATE'
    scene.cycles.max_bounces=16;scene.cycles.transmission_bounces=12
    scene.cycles.glossy_bounces=8;scene.cycles.transparent_max_bounces=12
    scene.cycles.volume_bounces=2;scene.cycles.sample_clamp_indirect=8
    scene.render.use_persistent_data=True
    scene.render.resolution_x=3840 if args.mode=='master' else 1280
    scene.render.resolution_y=2160 if args.mode=='master' else 720
    scene.render.resolution_percentage=100;scene.render.fps=FPS
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB'
    scene.render.image_settings.color_depth='16' if args.mode=='master' else '8'
    scene.render.image_settings.compression=30
    scene.frame_start=1;scene.frame_end=FPS*SECONDS
    scene.view_settings.view_transform='AgX';scene.view_settings.look='AgX - Medium High Contrast'
    scene.view_settings.exposure=.4
    world=bpy.data.worlds.new('安静的深色摄影棚');world.use_nodes=True
    world.node_tree.nodes['Background'].inputs[0].default_value=linear(COLORS['ink'])
    world.node_tree.nodes['Background'].inputs[1].default_value=.24;scene.world=world

    floor=surface('FLOOR / satin charcoal',COLORS['floor'],.285,.35)
    # 细微粗糙度只让反射更写实，不铺满凸凹噪声干扰光谱与玻璃轮廓。
    nodes=floor.node_tree.nodes;noise=nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value=180;noise.inputs['Detail'].default_value=2
    bump=nodes.new('ShaderNodeBump');bump.inputs['Strength'].default_value=.065
    bump.inputs['Distance'].default_value=.015
    floor.node_tree.links.new(noise.outputs['Fac'],bump.inputs['Height'])
    floor.node_tree.links.new(bump.outputs['Normal'],nodes['Principled BSDF'].inputs['Normal'])
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,-.075))
    finish(bpy.context.object,'无缝深色地面 / 真实反射',floor)
    metal=surface('METAL / brushed titanium',COLORS['metal'],.23,1)
    base=surface('BASE / smoked ceramic',COLORS['ink'],.16,.32)
    glass=surface('GLASS / optical crown', COLORS['optical'],.008,0,1)
    make_prism(glass)
    box('玻璃下方纤薄陶瓷台座',(0,0,.04),(2.15,1.85,.18),base,.075)
    # 细环和刻度组成有节制的仪器细节，避免场景只是一个玻璃三角形。
    bpy.ops.mesh.primitive_torus_add(major_radius=1.26,minor_radius=.012,
                                   major_segments=192,minor_segments=12,location=(0,0,-.055))
    finish(bpy.context.object,'钛金属测量环',metal)
    for i in range(36):
        angle=i*math.tau/36;radius=1.32
        tick=box('刻度 %02d'%i,(math.cos(angle)*radius,math.sin(angle)*radius,-.061),
                 (.055 if i%3 else .10,.009,.006),metal,.001)
        tick.rotation_euler[2]=angle
    box('白光源的精密黑色壳体',(-4.2,0,.84),(.62,.74,.66),base,.075)
    box('发光狭缝',(-3.883,0,.9),(.015,.28,.035),emitter('WHITE SLIT',linear(COLORS['ivory']),7)[0],.008)
    # 多道窄灯箱让玻璃边缘自然亮起；光谱灯的光能保留在地面反射中。
    area('KEY / large softbox',(-3,-3.5,7),(0,0,.7),850,5,COLORS['ivory'])
    area('GLASS / fine frontal strip',(-1.5,-4,3.6),(0,0,.8),1000,1.7,COLORS['optical'])
    area('RIM / cool vertical strip',(2,3.5,5),(0,0,.8),1100,3,COLORS['glass'])
    area('EDGE / gentle warm box',(-3,2.8,2.6),(0,0,.7),450,2,COLORS['ivory'])
    white_node=spot('白光入射 / 真实体积散射',(-3.87,0,.9),(-.43,0,.9),450,
                    linear(COLORS['ivory']),.024)
    white_beam,white_gain=beam_volume('入射白光 / 柔边静态光场',(-3.87,0,.9),(-.43,0,.9),
                             linear(COLORS['ivory']),.008,.026,35)
    spectra=[]
    # 保留35个实体照明光源；可见光谱使用同一连续体积，避免35道
    # 体积边界/反复积分。灯光与光场仍按同一时序淡入。
    for i in range(35):
        t=i/34;color=spectral(t)
        end=Vector((6.3,.4+1.75*t,.18+1.25*(1-t)))
        start=Vector((.52,.015,.89+(t-.5)*.025))
        node=spot('出射连续光谱 %02d'%i,tuple(start),tuple(end),32,color,.035)
        spectra.append(node)
    fan=spectral_fan();rainbow=rainbow_field()
    # 中景光学样片平放在低矮支座上。原竖板与终幕字幕/彩色光束
    # 重叠，形成无意义的黑色竖线；降低样片仍保留折射与材质层次，
    # 同时让主棱镜、光谱和片名拥有各自的构图空间。
    box('中景光学样片支座',(2.4,2.15,.025),(.82,1.05,.15),base,.025)
    panel=box('中景光学玻璃样片',(2.4,2.15,.14),(.90,1.15,.04),glass,.006)
    panel.rotation_euler=(0,.06,-.28)
    camera_data=bpy.data.cameras.new('Cinema camera / six authored shots')
    camera=bpy.data.objects.new('摄影机',camera_data);bpy.context.collection.objects.link(camera)
    scene.camera=camera;camera_data.sensor_width=36;camera_data.dof.use_dof=True
    camera_data.dof.aperture_fstop=6.3;camera_data.clip_start=.05;camera_data.clip_end=250
    fontdir=Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts'
    serif=bpy.data.fonts.load(str(fontdir/'georgia.ttf'))
    sans=bpy.data.fonts.load(str(fontdir/'segoeuil.ttf'))
    typemat,_=emitter('TITLE / ivory',linear(COLORS['ivory']),1)
    # 字幕不属于半透明发光装置。直接使用完整不透明字形，避免此前
    # 共用42%透明混合把主标题变成灰字；不修改系统的凤凰字体来源。
    next(node for node in typemat.node_tree.nodes if node.type=='MIX_SHADER').inputs[0].default_value=1
    title=text_object('主标题 / 镜头字幕',serif,typemat)
    subtitle=text_object('署名 / 品牌字幕',sans,typemat)
    # 字幕使用独立无景深透明场景：摄影机近处贴字会被真实DOF模糊，
    # 把它移到对焦距离又会让玻璃挡住文字。合成才是正确的电影字幕。
    overlay=bpy.data.scenes.new('FILM / crisp typography overlay')
    overlay.render.engine='BLENDER_EEVEE_NEXT';overlay.render.film_transparent=True
    overlay.render.resolution_x=scene.render.resolution_x;overlay.render.resolution_y=scene.render.resolution_y
    overlay.render.resolution_percentage=100
    overlaycam_data=bpy.data.cameras.new('Typography camera');overlaycam_data.type='ORTHO'
    overlaycam_data.ortho_scale=2*16/9
    overlaycam=bpy.data.objects.new('Typography camera',overlaycam_data)
    overlay.collection.objects.link(overlaycam);overlaycam.location=(0,0,5);overlay.camera=overlaycam
    for obj in (title,subtitle):
        for collection in list(obj.users_collection):collection.objects.unlink(obj)
        overlay.collection.objects.link(obj);obj.rotation_euler=(0,0,0)
    # 去噪后只加轻微镜头光晕，不用大片雾白遮住真实材质和细节。
    scene.use_nodes=True;tree=scene.node_tree;tree.nodes.clear()
    layer=tree.nodes.new('CompositorNodeRLayers');glow=tree.nodes.new('CompositorNodeGlare')
    glow.glare_type='FOG_GLOW';glow.quality='HIGH'
    # 4.5的Glare参数已迁移到节点输入；旧threshold/size/mix属性会
    # 触发RNA警告，并不能作为可复现的光晕合同。只取Glare输出后
    # 明确按8%叠加，原图直连保留黑位/玻璃高光，不依赖隐式mix。
    glow.inputs['Threshold'].default_value=.6
    glow.inputs['Size'].default_value=.5
    glow.inputs['Strength'].default_value=1
    light_mix=tree.nodes.new('CompositorNodeMixRGB');light_mix.blend_type='ADD'
    light_mix.inputs[0].default_value=.08
    titles=tree.nodes.new('CompositorNodeRLayers');titles.scene=overlay
    over=tree.nodes.new('CompositorNodeAlphaOver');over.inputs[0].default_value=1
    output=tree.nodes.new('CompositorNodeComposite')
    tree.links.new(layer.outputs['Image'],glow.inputs['Image'])
    tree.links.new(layer.outputs['Image'],light_mix.inputs[1])
    tree.links.new(glow.outputs['Glare'],light_mix.inputs[2])
    tree.links.new(light_mix.outputs[0],over.inputs[1])
    tree.links.new(titles.outputs['Image'],over.inputs[2]);tree.links.new(over.outputs[0],output.inputs[0])
    return scene,camera,title,subtitle,spectra,fan,rainbow,white_node,white_beam,white_gain


def pose(state,frame):
    scene,camera,title,subtitle,spectra,fan,rainbow,white_node,white_beam,white_gain=state
    second=(frame-1)/FPS;shot=next((row for row in SHOTS if row[0]<=second<row[1]),SHOTS[-1])
    start,end,pos0,pos1,target0,target1,lens,caption=shot
    progress=smooth(max(0,min(1,(second-start)/(end-start))))
    camera.location=mix(pos0,pos1,progress);target=mix(target0,target1,progress)
    camera.data.lens=lens;aim(camera,target)
    camera.data.dof.focus_distance=(Vector(target)-camera.location).length
    spectral_power=smooth(max(0,min(1,(second-5.5)/5)))
    for node in spectra:node.energy=32*spectral_power
    fan[0].hide_render=second<5.5;fan[1].outputs[0].default_value=spectral_power
    arch_power=smooth(max(0,min(1,(second-18)/3)))
    if second>31:arch_power*=1-smooth(min(1,(second-31)/3))
    rainbow[1].outputs[0].default_value=.8*arch_power
    rainbow[0].hide_render=second<18 or second>=34
    white_power=smooth(min(1,second/1.8))
    white_node.energy=450*white_power;white_gain.outputs[0].default_value=white_power
    white_beam.hide_render=white_power==0
    # 字幕固定在摄影视角坐标中，变化焦距不使字忽大忽小或越出安全边。
    half_w=16/9;half_h=1
    final=start==34;title.data.body=caption
    title.data.size=half_h*(.18 if final else .080)
    title.data.space_line=1.15
    title.location=(half_w*(.06 if final else -.87),half_h*(.48 if final else .80),0)
    subtitle.data.body='SandCore M8 / mio' if final else 'SANDCORE  /  WELCOME TO TRUE COLOR'
    subtitle.data.size=half_h*.045
    subtitle.location=(title.location.x,half_h*(.12 if final else .65),0)
    scene.frame_set(frame)


def parse_frames(value):
    if value=='all':return range(1,FPS*SECONDS+1)
    result=[]
    for block in value.split(','):
        if '-' in block:
            start,end=map(int,block.split('-'));result.extend(range(start,end+1))
        else:result.append(int(block))
    assert result and all(1<=f<=FPS*SECONDS for f in result)
    return sorted(set(result))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--mode',choices=('scout','master'),default='master')
    parser.add_argument('--frames',default='all');parser.add_argument('--samples',type=int,default=512)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    assert args.samples>=128 if args.mode=='master' else args.samples>=16
    out=Path(args.out).resolve();out.mkdir(parents=True,exist_ok=True)
    source=Path(__file__).resolve();source_sha=digest(source)
    fontdir=Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts'
    reference=source.parent.parent/'assets/pictures/WELCOME-TRUECOLOR-V1.png'
    identity=dict(author=AUTHOR,magic='SCFILM1MIO',source_sha256=source_sha,
        blender_version=bpy.app.version_string,blender_build_hash=bpy.app.build_hash.decode(),
        mode=args.mode,width=3840 if args.mode=='master' else 1280,
        height=2160 if args.mode=='master' else 720,fps=FPS,seconds=SECONDS,samples=args.samples,
        threads=os.cpu_count(),seed=821104,denoising='OIDN RGB_ALBEDO_NORMAL ACCURATE',
        output_bits=16 if args.mode=='master' else 8,colors=COLORS,shots=SHOTS,
        fonts={name:digest(fontdir/name) for name in ('georgia.ttf','segoeuil.ttf')},
        reference='assets/pictures/WELCOME-TRUECOLOR-V1.png',reference_sha256=digest(reference),
        rainbow_required=True)
    manifest=out/'project.json'
    if manifest.exists():assert json.loads(manifest.read_text(encoding='utf-8'))==json.loads(json.dumps(identity)),'工程改变，必须另建渲染批次'
    else:manifest.write_text(json.dumps(identity,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    shutil.copy2(source,out/'film_scene.py');state=setup(args);scene=state[0]
    pose(state,2161);bpy.ops.wm.save_as_mainfile(filepath=str(out/'WELCOME-TRUECOLOR.blend'))
    progress_path=out/'progress.json';progress=dict(author=AUTHOR,status='RENDERING',identity_sha256=digest(manifest),frames=[])
    if progress_path.exists():
        old=json.loads(progress_path.read_text(encoding='utf-8'))
        assert old['identity_sha256']==progress['identity_sha256'];progress=old;progress['status']='RENDERING'
    done={row['frame']:row for row in progress['frames']};frames=list(parse_frames(args.frames))
    for frame in frames:
        path=out/('frame-%05d.png'%frame)
        if frame in done and path.exists() and digest(path)==done[frame]['sha256']:continue
        # 不覆盖用户任意旧图；仅提交本工程同名的临时帧。空间检查失败可续渲。
        assert shutil.disk_usage(out).free>12*1024**3,'剩余空间不足12GiB，保留现有帧并等待处理'
        temporary=out/('pending-%05d.png'%frame);scene.render.filepath=str(temporary)
        pose(state,frame);began=time.monotonic();bpy.ops.render.render(write_still=True)
        elapsed=time.monotonic()-began
        assert temporary.exists() and temporary.stat().st_size>1000,'渲染未产生完整图像'
        with temporary.open('rb') as src:
            header=src.read(33);src.seek(-12,2);tail=src.read()
        assert header[:8]==b'\x89PNG\r\n\x1a\n' and tail==b'\0\0\0\0IEND\xaeB`\x82'
        assert int.from_bytes(header[16:20],'big')==identity['width']
        assert int.from_bytes(header[20:24],'big')==identity['height'] and header[24]==identity['output_bits']
        temporary.replace(path);row=dict(frame=frame,seconds=round(elapsed,3),bytes=path.stat().st_size,sha256=digest(path))
        done[frame]=row;progress['frames']=sorted(done.values(),key=lambda r:r['frame'])
        durations=[item['seconds'] for item in progress['frames']]
        progress.update(requested_frames=len(frames),completed_requested=sum(f in done for f in frames),
            mean_frame_seconds=round(sum(durations)/len(durations),3),
            remaining_estimate_seconds=round((len(frames)-sum(f in done for f in frames))*sum(durations)/len(durations)),
            disk_free_bytes=shutil.disk_usage(out).free,
            projected_full_sequence_bytes=round(sum(r['bytes'] for r in done.values())/len(done)*FPS*SECONDS))
        temp_report=out/'progress.pending.json';temp_report.write_text(json.dumps(progress,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        temp_report.replace(progress_path);print('SCFILM1MIO',json.dumps(row),flush=True)
    progress['status']='REQUESTED_FRAMES_RENDERED';progress['full_sequence_complete']=len(done)==FPS*SECONDS
    progress_path.write_text(json.dumps(progress,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # 单帧渲染成功与整部影片/播放器完成严格分开；最终打包还须全序列复核。
    print('SCFILM1MIO requested complete; full sequence =',progress['full_sequence_complete'],flush=True)


if __name__=='__main__':main()
