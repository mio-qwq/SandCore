#!/usr/bin/env python3
"""mio：实际GPU玻璃求交的旧新全字段对照与独立平面交点参考。

此检查只验证有界玻璃几何及保守粗筛，不测整片帧率。可以与生产
GPU队列同时执行，但报告明确记录竞争，不将其事件时间当性能收益。
参考逐个求平面交点后验证全部半空间，不复制GPU的进入/离开裁剪。
旧shader/新shader/夹具/参数和完整回读分别冻结，失败也保留。
"""
import argparse
import ctypes as C
import hashlib
import json
import math
from pathlib import Path
import random
import struct
import sys
import traceback

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools/film_math'))
from opencl_host import Gpu

KERNEL=r'''
/* mio：直接调用被测工程prism，未替换几何或导入预算命中。 */
__kernel void verify_prism(__global const float4 *rays,__global float *output,
                          __constant float *cfg,uint count){
    uint i=get_global_id(0);if(i>=count)return;
    float4 eye=rays[i*2],ray=rays[i*2+1];
    Hit h;h.t=0;h.n=(float3)(0);h.material=-1;
    int found=prism(eye.xyz,ray.xyz,eye.w,&h,cfg);
    uint at=8+i*8;output[at]=(float)found;output[at+1]=h.t;
    output[at+2]=h.n.x;output[at+3]=h.n.y;output[at+4]=h.n.z;
    output[at+5]=(float)h.material;output[at+6]=0;output[at+7]=0;
}
'''

def sha(blob):return hashlib.sha256(blob).hexdigest()
def float32(value):return struct.unpack('<f',struct.pack('<f',value))[0]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def normalize(v):
    length=math.sqrt(dot(v,v));return tuple(x/length for x in v)
def local(p,cosine,sine):return (cosine*p[0]+sine*p[2],p[1],-sine*p[0]+cosine*p[2])
def world(p,cosine,sine):return (cosine*p[0]-sine*p[2],p[1],sine*p[0]+cosine*p[2])

def reference(row,planes,cosine,sine):
    origin=local(row[:3],cosine,sine);direction=local(row[4:7],cosine,sine);limit=row[3]
    candidates=[];near_boundary=False
    for index,plane in enumerate(planes):
        denominator=dot(plane[:3],direction)
        if abs(denominator)<1e-8:continue
        parameter=(plane[3]-dot(plane[:3],origin))/denominator
        if parameter<=0:continue
        point=tuple(x+d*parameter for x,d in zip(origin,direction))
        distances=[dot(other[:3],point)-other[3] for other in planes]
        if max(distances)>3e-6:continue
        if abs(parameter-.0002)<3e-6 or abs(parameter-limit)<max(3e-6,limit*3e-6):near_boundary=True
        if parameter<=.0002 or parameter>=limit:continue
        edge=any(abs(value)<3e-5 for j,value in enumerate(distances) if j!=index)
        candidates.append((parameter,index,edge))
    if not candidates:return None,near_boundary
    parameter,index,edge=min(candidates)
    # 另一面几乎同距时，浮点点积次序可能选不同的面；该少量边界
    # 仍要求GPU旧新逐字节一致，独立double参考只不强制法线排序。
    near_boundary|=edge or any(j!=index and abs(t-parameter)<3e-5 for t,j,_ in candidates)
    return (parameter,world(planes[index][:3],cosine,sine)),near_boundary

def cases(planes):
    rng=random.Random(0x4D494F);result=[]
    def add(eye,direction,limit=30):
        result.append(tuple(float32(x) for x in (*eye,limit,*direction,0)))
    for i in range(2048):
        eye=tuple(rng.uniform(-8,8) for _ in range(3))
        target=(rng.uniform(-1.2,1.2),rng.uniform(.05,1.8),rng.uniform(-.8,.8))
        add(eye,normalize(tuple(x-y for x,y in zip(target,eye))),rng.choice((1,3,12,30)))
    for i in range(512):
        while True:
            eye=(rng.uniform(-.6,.6),rng.uniform(.23,1.3),rng.uniform(-.5,.5))
            if all(dot(plane[:3],eye)<plane[3]-.03 for plane in planes):break
        add(eye,normalize(tuple(rng.uniform(-1,1) for _ in range(3))))
    for i in range(512):
        eye=tuple(rng.uniform(-8,8) for _ in range(3))
        add(eye,normalize((eye[0],eye[1]-.8,eye[2])))
    for i in range(512):
        axis=i%3;direction=[0,0,0];direction[axis]=1 if i&1 else -1
        eye=[rng.uniform(-1.5,1.5),rng.uniform(-.2,2),rng.uniform(-1,1)]
        # 实际原面、粗筛盒外边及内点，覆盖平行轴/掠射与严格limit。
        eye[(axis+1)%3]=rng.choice((-.81,.81,.17,1.59,-.58,.58,0))
        add(eye,direction,rng.choice((.0002,.1,1,4,30)))
    for i in range(512):
        eye=(rng.uniform(-2,2),rng.uniform(.1,1.7),rng.uniform(-2,2))
        direction=normalize(tuple(rng.uniform(-1,1) for _ in range(3)))
        add(eye,direction,rng.choice((0,.0001,.0002,.0003,.1,2,30)))
    assert len(result)==4096
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--baseline-batch',required=True)
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    baseline=ROOT/args.baseline_batch
    project=json.loads((baseline/'project.json').read_text(encoding='utf-8'))
    old=(baseline/'render.cl').read_bytes();new=(ROOT/'tools/film_math/render.cl').read_bytes()
    assert sha(old)==project['sources']['render.cl'],'旧批次shader被改变'
    planes=[tuple(float32(x) for x in project['prism_planes'][i:i+4]) for i in range(0,128,4)]
    rows=cases(planes);rays=struct.pack('<%df'%(len(rows)*8),*(x for row in rows for x in row))
    for name,blob in (('OLD.cl',old),('NEW.cl',new),('probe.cl',KERNEL.encode()),('rays.bin',rays),
                      ('opencl_host.py',(ROOT/'tools/film_math/opencl_host.py').read_bytes()),('verifier.py',Path(__file__).read_bytes())):
        (out/name).write_bytes(blob)
    report=dict(author='mio',magic='SCFILMGEOM1MIO',version=1,status='RUNNING',
        scope='ACTUAL_GPU_PRISM_FIELDS_AND_INDEPENDENT_PLANE_REFERENCE',source_sha256={'OLD':sha(old),'NEW':sha(new)},
        ray_sha256=sha(rays),poses=[],independent_checks=0,reference_boundary_cases=0,
        limits='生产GPU可能同时在渲染；本夹具只证几何/粗筛/缓冲，不测整片速度或美术')
    machines={};guard=b'\xA5'*32;body_size=len(rows)*32
    try:
        for kind,shader in (('OLD',old),('NEW',new)):
            gpu=Gpu(shader+b'\n'+KERNEL.encode(),out/(kind+'-build.log'));machines[kind]=dict(gpu=gpu)
            kernel=gpu.kernel('verify_prism');ray_buffer=gpu.buffer(len(rays));cfg_buffer=gpu.buffer(640);output=gpu.buffer(body_size+64)
            gpu.write(ray_buffer,rays);machines[kind].update(kernel=kernel,rays=ray_buffer,cfg=cfg_buffer,output=output)
            report['device']=gpu.identity
        for index,angle in enumerate((0,.001,-.025,.025,math.pi/4,-math.pi/4,math.pi/2,math.pi)):
            cosine,sine=float32(math.cos(angle)),float32(math.sin(angle))
            config=[0.0]*160;config[16]=cosine;config[17]=sine;config[31]=32
            config[32:]=[x for plane in planes for x in plane];packed=struct.pack('<160f',*config)
            values={}
            for kind,machine in machines.items():
                gpu=machine['gpu'];gpu.write(machine['cfg'],packed);gpu.write(machine['output'],guard+b'\0'*body_size+guard)
                gpu.set_args(machine['kernel'],C.c_void_p(machine['rays']),C.c_void_p(machine['output']),C.c_void_p(machine['cfg']),C.c_uint(len(rows)))
                gpu.run(machine['kernel'],len(rows));raw=gpu.read(machine['output'],body_size+64)
                assert raw[:32]==guard and raw[-32:]==guard,'GPU完整前后护栏改变'
                values[kind]=raw[32:-32];(out/('%02d-%s.bin'%(index,kind))).write_bytes(raw)
            assert values['OLD']==values['NEW'],('保守粗筛改变原求交字段',angle)
            hits=0;reference_hits=0
            for i,observed in enumerate(struct.iter_unpack('<8f',values['NEW'])):
                assert all(math.isfinite(x) for x in observed),('非有限字段',index,i)
                hits+=int(observed[0])
                if i%4:continue
                expected,ambiguous=reference(rows[i],planes,cosine,sine)
                if ambiguous:report['reference_boundary_cases']+=1;continue
                report['independent_checks']+=1
                assert bool(observed[0])==(expected is not None),('独立平面命中判定',angle,i,rows[i],observed,expected)
                if expected:
                    reference_hits+=1;t,normal=expected
                    assert abs(observed[1]-t)<=max(2e-5,abs(t)*3e-5),('独立距离',angle,i,observed,t)
                    assert max(abs(a-b) for a,b in zip(observed[2:5],normal))<=3e-5,('独立外法线',angle,i,observed,normal)
            report['poses'].append(dict(angle=angle,queries=len(rows),hits=hits,reference_hits=reference_hits,
                                        all_field_sha256=sha(values['NEW']),guards='PASS'))
            print('PRISM FIELDS EQUAL',index,len(rows),'hits',hits,flush=True)
        assert report['independent_checks']>7000 and sum(row['reference_hits'] for row in report['poses'])>500
        report.update(status='PASS',paired_queries=len(rows)*len(report['poses']))
    except BaseException as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc());raise
    finally:
        report['release_statuses']={kind:machine['gpu'].close() for kind,machine in machines.items()}
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
