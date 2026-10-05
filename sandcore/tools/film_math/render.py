#!/usr/bin/env python3
"""mio：自研GPU逐帧影片；草图/母版分批冻结，完整16位PNG、计时与续渲。"""
import argparse
from collections import Counter
import ctypes as C
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import sys
import time
import traceback
import zlib

from PIL import Image, ImageDraw, ImageFont

HERE=Path(__file__).resolve().parent
IO_SOURCE=HERE/'film_io.py' if (HERE/'film_io.py').is_file() else HERE.parent/'film_io.py'
sys.path.insert(0,str(IO_SOURCE.parent))
from film_io import validate_png
from opencl_host import Gpu

FPS=60;SECONDS=42;FRAMES=FPS*SECONDS
# 沿用已审阅的六幕视角，但每个时刻真正重新计算相机与射线；
# 这里给的是相机轨迹控制点，不是用于插值播放的预渲染图像。
SHOTS=[
    (0,6,(-4.8,-4.8,1.65),(-2.7,-3.7,1.45),(-.75,0,.95),(-.15,0,.90),65,'A single ray.'),
    (6,13,(3.2,-5.8,2.35),(2.2,-4.9,2.05),(0,0,.92),(0,0,.94),62,'Glass. Light. Possibility.'),
    (13,20,(6.8,-7.9,3.35),(5.7,-6.6,2.7),(1.25,.30,.95),(1.05,.3,.95),48,'Every color, revealed.'),
    (20,27,(2.4,-7.7,4.4),(1.0,-7.3,3.65),(.55,1.25,1.7),(.45,1.1,1.6),42,'A wider world.'),
    (27,34,(-4.5,-8.6,3.5),(-2.1,-8.9,3.2),(.45,1.0,1.35),(.7,.9,1.2),46,'Made of light.'),
    (34,42,(6.0,-10.4,3.5),(5.0,-10.0,3.05),(1.5,.15,1.0),(1.4,.15,1.0),45,'Welcome to\ntrue color.'),
]


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def commit(path,value):
    pending=path.with_name(path.name+'.pending')
    pending.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');pending.replace(path)
def smooth(value):
    value=max(0,min(1,value));return value*value*(3-2*value)
def mix(a,b,t):return tuple(x+(y-x)*t for x,y in zip(a,b))
def vec_sub(a,b):return tuple(x-y for x,y in zip(a,b))
def normalized(v):
    length=math.sqrt(sum(x*x for x in v))
    if length<1e-9:raise ValueError('degenerate camera vector')
    return tuple(x/length for x in v)
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def axis(v):return (v[0],v[2],v[1])


def prism_planes():
    # 凸三角柱五个原始支持平面；九条棱各增加三个真实倒角面。
    # 用球半径.009的Minkowski支持函数计算切面，不能只插值法线
    # 画一个“看起来像倒角”的假高光。法线/截距仅在宿主预计算
    # 一次，GPU每射线只做32个点积/区间裁剪，不重复开方生成面。
    base=[((0,-1,0),-.18),((math.sqrt(3)/2,.5,0),.79),
          ((-math.sqrt(3)/2,.5,0),.79),((0,0,1),.58),((0,0,-1),.58)]
    result=list(base)
    for i in range(5):
        for j in range(i+1,5):
            if (i,j)==(3,4):continue
            for weight in (.25,.5,.75):
                normal=mix(base[i][0],base[j][0],weight);length=math.sqrt(sum(x*x for x in normal))
                if length<1e-9:raise ValueError('opposite bevel planes')
                distance=base[i][1]*(1-weight)+base[j][1]*weight-.009*(1-length)
                result.append((tuple(x/length for x in normal),distance/length))
    if len(result)!=32:raise AssertionError('prism support plane capacity')
    return tuple(value for normal,distance in result for value in (*normal,distance))


PRISM_PLANES=prism_planes()


def camera(frame,width,height):
    elapsed=(frame-1)/FPS
    shot=next(i for i,row in enumerate(SHOTS) if row[0]<=elapsed<row[1])
    began,end,eye0,eye1,target0,target1,lens,label=SHOTS[shot]
    local=(elapsed-began)/(end-began);t=smooth(local)
    eye=axis(mix(eye0,eye1,t));target=axis(mix(target0,target1,t))
    forward=normalized(vec_sub(target,eye));right=normalized(cross((0,1,0),forward));up=cross(forward,right)
    focal=width*lens/36
    white=smooth(elapsed/2);fan=smooth((elapsed-8)/4)
    rainbow=smooth((elapsed-19.3)/2.4)*(1-smooth((elapsed-33)/2))
    angle=.025*math.sin(elapsed*.25)
    opacity=smooth(local/.09)*(1-smooth((local-.88)/.12))
    values=(*eye,*forward,*right,*up,focal,float(width),float(height),elapsed,
            math.cos(angle),math.sin(angle),fan,white,rainbow,opacity,0,0,0,0,0,0,0,0,0,32)
    values+=PRISM_PLANES
    return shot,values


def overlay(width,height,shot,fonts):
    # 字幕独立于景深/反射，保持原宣传片美术合同。用本机Georgia与
    # Segoe UI Light作为影片栅格资产，不更换操作系统font16字库。
    # 一个镜头/一个尺寸只创建一次覆盖层，帧内只改变淡入包络；
    # 这不是预生成场景图或复用时间帧，GPU仍计算全部实际像素。
    image=Image.new('RGBA',(width,height),(0,0,0,0));draw=ImageDraw.Draw(image)
    serif=ImageFont.truetype(str(fonts[0]),max(10,int(height*(.077 if shot==5 else .040))))
    sans=ImageFont.truetype(str(fonts[1]),max(9,int(height*.018)))
    if shot==5:
        draw.multiline_text((int(width*.615),int(height*.17)),SHOTS[shot][-1],font=serif,
                            fill=(244,238,226,255),spacing=int(height*.013))
        draw.text((int(width*.618),int(height*.37)),'SANDCORE  /  M8',font=sans,fill=(244,238,226,205))
    else:
        draw.text((int(width*.065),int(height*.86)),SHOTS[shot][-1],font=serif,fill=(244,238,226,235))
    draw.text((int(width*.065),int(height*.945)),'mio',font=sans,fill=(244,238,226,170))
    return image.tobytes()


def png16(path,data,width,height):
    if len(data)!=width*height*6:raise ValueError('RGB16 complete body size mismatch')
    # GPU直接生成大端16位RGB，无Python逐像素循环。过滤行以字节为
    # 单位，zlib流分块输出；文件同目录原子发布，崩溃的.pending不能
    # 被续渲当成完整帧。没有生成8位后乘257冒充16位母版。
    def chunk(output,kind,payload):
        output.write(struct.pack('>I',len(payload))+kind+payload+struct.pack('>I',zlib.crc32(payload,zlib.crc32(kind))&0xffffffff))
    pending=path.with_name(path.name+'.pending');row=width*6
    with pending.open('wb') as output:
        output.write(b'\x89PNG\r\n\x1a\n');chunk(output,b'IHDR',struct.pack('>IIBBBBB',width,height,16,2,0,0,0))
        chunk(output,b'sRGB',b'\0');stream=zlib.compressobj(4);block=bytearray()
        for y in range(height):
            block.extend(stream.compress(b'\0'+data[y*row:(y+1)*row]))
            if len(block)>=1024*1024:chunk(output,b'IDAT',bytes(block));block.clear()
        block.extend(stream.flush());chunk(output,b'IDAT',bytes(block));chunk(output,b'IEND',b'')
    validate_png(pending,width,height,16);pending.replace(path)


def render_frame(gpu,kernels,buffers,frame,width,height,samples,band,batch,fonts,adaptive=False,minimum_samples=12,error_limit=.006):
    count=width*height;shot,config=camera(frame,width,height)
    gpu.write(buffers['config'],struct.pack('<160f',*config));gpu.write(buffers['errors'],b'\0'*4)
    if buffers.get('overlay_shot')!=shot:
        gpu.write(buffers['overlay'],overlay(width,height,shot,fonts));buffers['overlay_shot']=shot
    stats={name:0.0 for name in ('trace_gpu_seconds','average_gpu_seconds','denoise_gpu_seconds','encode_gpu_seconds')}
    began=time.monotonic();longest=0
    group_max=(samples+2)//3;group_min=(minimum_samples+2)//3
    for y in range(0,height,band):
        end=min(height,y+band)
        for sample in range(0,group_max if adaptive else samples,batch):
            amount=min(batch,(group_max if adaptive else samples)-sample)
            if adaptive:
                name='trace_adaptive_band'
                gpu.set_args(kernels[name],C.c_void_p(buffers['accum']),C.c_void_p(buffers['moments']),C.c_void_p(buffers['counts']),
                    C.c_void_p(buffers['guide']),C.c_void_p(buffers['albedo']),C.c_void_p(buffers['config']),C.c_uint(width),C.c_uint(height),
                    C.c_uint(y),C.c_uint(end),C.c_uint(sample),C.c_uint(amount),C.c_uint(group_min),C.c_uint(group_max),C.c_float(error_limit),C.c_void_p(buffers['errors']))
            else:
                name='trace_band'
                gpu.set_args(kernels[name],C.c_void_p(buffers['accum']),C.c_void_p(buffers['guide']),
                    C.c_void_p(buffers['albedo']),C.c_void_p(buffers['config']),C.c_uint(width),C.c_uint(height),
                    C.c_uint(y),C.c_uint(end),C.c_uint(sample),C.c_uint(amount),C.c_uint(frame),C.c_void_p(buffers['errors']))
            duration=gpu.run(kernels[name],width*(end-y));stats['trace_gpu_seconds']+=duration;longest=max(longest,duration)
        if y%max(band,band*8)==0:print('TRACE frame=%d rows=%d/%d samples=%d wall=%.2fs'%(frame,end,height,samples,time.monotonic()-began),flush=True)
    errors=struct.unpack('<I',gpu.read(buffers['errors'],4))[0]
    if errors:raise RuntimeError('nonfinite/negative traced RGB samples: %d'%errors)
    if adaptive:
        gpu.set_args(kernels['average_adaptive'],C.c_void_p(buffers['accum']),C.c_void_p(buffers['counts']),C.c_void_p(buffers['a']),C.c_uint(count))
        stats['average_gpu_seconds']=gpu.run(kernels['average_adaptive'],count)
        counts=[x&0x7fffffff for x in memoryview(gpu.read(buffers['counts'],count*4)).cast('I')]
        # 一次线性统计；不能对每一种样本数再扫描整张4K计数图，
        # 最坏171次全图扫描会把GPU节省的时间浪费在Python侧。
        histogram={str(n*3):frequency for n,frequency in Counter(counts).items()}
        stats.update(sample_histogram=histogram,actual_primary_samples=sum(counts)*3,minimum_actual_samples=min(counts)*3,
                     maximum_actual_samples=max(counts)*3,average_actual_samples=sum(counts)*3/count,adaptive_error_srgb=error_limit)
    else:
        gpu.set_args(kernels['average'],C.c_void_p(buffers['accum']),C.c_void_p(buffers['a']),C.c_uint(count),C.c_uint(samples))
        stats['average_gpu_seconds']=gpu.run(kernels['average'],count)
    source,target=buffers['a'],buffers['b']
    for step in (1,2,4,8,16):
        gpu.set_args(kernels['denoise'],C.c_void_p(source),C.c_void_p(target),C.c_void_p(buffers['guide']),
                     C.c_void_p(buffers['albedo']),C.c_uint(width),C.c_uint(height),C.c_uint(step))
        stats['denoise_gpu_seconds']+=gpu.run(kernels['denoise'],count);source,target=target,source
    gpu.set_args(kernels['encode'],C.c_void_p(source),C.c_void_p(buffers['rgb16']),C.c_void_p(buffers['overlay']),C.c_uint(count),C.c_float(config[21]))
    stats['encode_gpu_seconds']=gpu.run(kernels['encode'],count)
    read_start=time.monotonic();rgb16=gpu.read(buffers['rgb16'],count*6)
    stats.update(readback_seconds=time.monotonic()-read_start,compute_and_readback_seconds=time.monotonic()-began,
                 longest_trace_dispatch_seconds=longest,nonfinite_samples=errors,samples_per_physical_pixel=samples,
                 physical_width=width,physical_height=height,shot=shot+1)
    return rgb16,stats


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--frames',default='180,570,990,1410,1830,2221')
    parser.add_argument('--mode',choices=('draft','master'),default='draft')
    parser.add_argument('--width',type=int,default=960);parser.add_argument('--height',type=int,default=540)
    parser.add_argument('--samples',type=int,default=32);parser.add_argument('--band',type=int,default=32)
    parser.add_argument('--batch',type=int,default=4);parser.add_argument('--resume',action='store_true')
    parser.add_argument('--adaptive',action='store_true');parser.add_argument('--min-samples',type=int,default=12)
    parser.add_argument('--error-limit',type=float,default=.006)
    args=parser.parse_args()
    if not 16<=args.width<=3840 or not 16<=args.height<=2160 or not 1<=args.samples<=4096 or not 1<=args.band<=64 or not 1<=args.batch<=16:
        raise ValueError('invalid rendering bounds')
    if args.mode=='master' and (args.width,args.height)!=(3840,2160):raise ValueError('master must be real 4K')
    if args.mode=='master' and args.samples<128:raise ValueError('master starts at >=128 samples; visual/noise validation remains required')
    if not 3<=args.min_samples<=args.samples or not 0<args.error_limit<=.03:raise ValueError('invalid adaptive sample/error bounds')
    frames=list(range(1,FRAMES+1)) if args.frames=='all' else [int(x) for x in args.frames.split(',')]
    if len(set(frames))!=len(frames) or any(not 1<=x<=FRAMES for x in frames):raise ValueError('invalid/duplicate frames')
    out=Path(args.out).resolve();out.mkdir(parents=True,exist_ok=args.resume)
    import msvcrt
    # 同批次的续渲必须持操作系统锁，不能只相信PID文件。重复进程
    # 在触碰project/progress之前即拒绝；异常退出自动释放字节锁，
    # 不留下需要人工删除的“死锁文件”。不同批次可独立运行。
    with (out/'render.lock').open('a+b') as lock:
        lock.seek(0,os.SEEK_END)
        if lock.tell()==0:lock.write(b'\0');lock.flush()
        lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        try:render_batch(args,out,frames)
        finally:lock.seek(0);msvcrt.locking(lock.fileno(),msvcrt.LK_UNLCK,1)


def render_batch(args,out,frames):
    fonts=(Path(r'C:\Windows\Fonts\georgia.ttf'),Path(r'C:\Windows\Fonts\segoeuil.ttf'))
    sources={name:sha(HERE/name) for name in ('render.py','render.cl','opencl_host.py')}
    sources['film_io.py']=sha(IO_SOURCE)
    identity={'author':'mio','magic':'SCMATHFILM1MIO','version':1,'mode':args.mode,'width':args.width,'height':args.height,
              'samples':args.samples,'fps':FPS,'seconds':SECONDS,'frames':FRAMES,'output_bits':16,
              'band':args.band,'sample_batch':args.batch,'sources':sources,'fonts':{str(p):sha(p) for p in fonts},
              'model':'closed 32-plane beveled prism; Snell/Fresnel 3 RGB bands; stochastic rough reflection; 3 area lights; analytical art emission fields',
              'prism_planes':list(PRISM_PLANES),'pixel_antialias':'rotated Halton2/3; persistent deterministic pixel seeds; real frame geometry',
              'denoise':'five spatial normal/depth/material/variance guided a-trous passes; no time interpolation'}
    identity.update(adaptive=args.adaptive,minimum_samples=args.min_samples,adaptive_error_srgb=args.error_limit,
                    sample_model='three spectral stratified real samples per variance group' if args.adaptive else 'fixed independent random spectral samples')
    path=out/'project.json'
    if path.exists():
        if json.loads(path.read_text(encoding='utf-8'))!=identity:raise ValueError('changed renderer/parameters/fonts cannot resume old frames')
    else:
        commit(path,identity)
        for name in ('render.py','render.cl','opencl_host.py'):
            if (HERE/name).resolve()!=(out/name).resolve():shutil.copy2(HERE/name,out/name)
        if IO_SOURCE.resolve()!=(out/'film_io.py').resolve():shutil.copy2(IO_SOURCE,out/'film_io.py')
    report={'author':'mio','status':'RUNNING','scope':'HOST_MATH_GPU_REAL_RAY_FRAMES','identity_sha256':sha(path),
            'process_pid':os.getpid(),'frames':[],'requested_frames':frames,'full_movie_complete':False,'guest_player_complete':False,
            'limitations':'actual ray frames; visual/temporal acceptance and full movie/player separate'}
    if args.resume and (out/'progress.json').exists():
        previous=json.loads((out/'progress.json').read_text(encoding='utf-8'))
        if previous['identity_sha256']!=report['identity_sha256']:raise ValueError('previous frames from other source')
        for row in previous['frames']:
            file=out/('frame-%05d.png'%row['frame'])
            validate_png(file,args.width,args.height,16)
            if sha(file)!=row['sha256']:raise ValueError('changed completed frame')
            report['frames'].append(row)
    gpu=None
    try:
        frozen_source=(out/'render.cl').read_bytes()
        if hashlib.sha256(frozen_source).hexdigest()!=sources['render.cl']:raise ValueError('frozen shader source changed')
        gpu=Gpu(frozen_source,out/'build.log');report['device']=gpu.identity;report['build_seconds']=gpu.build_seconds
        kernels={name:gpu.kernel(name) for name in ('trace_band','average','trace_adaptive_band','average_adaptive','denoise','encode')}
        count=args.width*args.height
        sizes={'accum':count*16,'guide':count*16,'albedo':count*16,'a':count*16,'b':count*16,'rgb16':count*6,'overlay':count*4,'config':640,'errors':4}
        if args.adaptive:sizes.update(moments=count*16,counts=count*4)
        buffers={name:gpu.buffer(size) for name,size in sizes.items()};report['gpu_buffer_bytes']=sum(sizes.values())
        completed={row['frame'] for row in report['frames']}
        for frame in frames:
            if frame in completed:continue
            if shutil.disk_usage(out).free<12*1024**3:raise RuntimeError('free disk below 12GiB; preserve good frames')
            began=time.monotonic()
            body,stats=render_frame(gpu,kernels,buffers,frame,args.width,args.height,args.samples,args.band,args.batch,fonts,
                                    args.adaptive,args.min_samples,args.error_limit)
            file=out/('frame-%05d.png'%frame);save_start=time.monotonic();png16(file,body,args.width,args.height)
            stats['png_save_validate_seconds']=time.monotonic()-save_start
            preview=Image.frombytes('RGB',(args.width,args.height),body,'raw','RGB;16B')
            preview.thumbnail((1672,941));preview.save(out/('preview-%05d.png'%frame))
            row={'frame':frame,'seconds':time.monotonic()-began,'bytes':file.stat().st_size,'sha256':sha(file),'stats':stats}
            report['frames'].append(row);commit(out/'progress.json',report)
            print('FRAME DONE '+json.dumps(row,ensure_ascii=False),flush=True)
        report['status']='REQUESTED_REAL_FRAMES_COMPLETE'
        report['full_movie_complete']=len({row['frame'] for row in report['frames']})==FRAMES
    except BaseException as error:
        report.update(status='FAILED_RETAINED_OUTPUT',error=repr(error),traceback=traceback.format_exc());raise
    finally:
        if gpu:report['release_statuses']=gpu.close()
        commit(out/'progress.json',report);commit(out/'results.json',report)


if __name__=='__main__':main()
