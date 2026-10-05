#!/usr/bin/env python3
"""mio：真实GPU回读输入、各支持面及原求交结果，定位交叉检查差异。

诊断逐项输出实际读取的数据，不能只比较一张最终图片猜硬件。
没有写生产缓冲或修改冻结渲染器；新目录保存完整输入/程序/回读。
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import struct
import sys
import traceback

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'tools/film_math'))
from opencl_host import Gpu
from verify_math_film_geometry import sha,reference,float32

KERNEL=r'''
__kernel void prism_debug(__global const float4 *input,__global float *output,
                         __constant float *cfg,uint count){
    uint i=get_global_id(0);if(i>=count)return;
    float4 eye=input[i*2],ray=input[i*2+1];uint at=i*192;
    vstore4(eye,0,output+at);vstore4(ray,0,output+at+4);
    float3 p=local_point(eye.xyz,cfg),d=local_point(ray.xyz,cfg);
    vstore3(p,0,output+at+8);vstore3(d,0,output+at+12);
    output[at+16]=cfg[16];output[at+17]=cfg[17];output[at+18]=cfg[31];
    float enter=-1e20f,leave=1e20f;int invalid=0;
    /* 诊断路径没有平面循环内提前退出，所有面写出真实值；
     * 单独的原prism仍原样调用，两结果不能互相覆盖。 */
    for(int j=0;j<32;j++){
        float4 plane=vload4(j,cfg+32);
        float slope=dot(plane.xyz,d),offset=plane.w-dot(plane.xyz,p);
        float t=fabs(slope)<1e-8f?0:offset/slope;
        output[at+32+j*4]=slope;output[at+33+j*4]=offset;
        output[at+34+j*4]=t;output[at+35+j*4]=plane.w;
        if(fabs(slope)<1e-8f){if(offset<0)invalid=1;}
        else if(slope<0){enter=fmax(enter,t);}else leave=fmin(leave,t);
    }
    output[at+19]=enter;output[at+20]=leave;output[at+21]=(float)invalid;
    Hit hit;hit.t=0;hit.n=(float3)(0);hit.material=-1;
    int found=prism(eye.xyz,ray.xyz,eye.w,&hit,cfg);
    output[at+22]=(float)found;output[at+23]=hit.t;vstore3(hit.n,0,output+at+24);
}
'''

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--geometry-batch',required=True)
    parser.add_argument('--baseline-batch',required=True)
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    geometric=ROOT/args.geometry_batch;baseline=ROOT/args.baseline_batch
    project=json.loads((baseline/'project.json').read_text(encoding='utf-8'))
    shader=(baseline/'render.cl').read_bytes();assert sha(shader)==project['sources']['render.cl']
    rays=(geometric/'rays.bin').read_bytes();count=len(rays)//32
    config=[0.0]*160;config[16]=1;config[31]=32
    config[32:]=project['prism_planes'];packed=struct.pack('<160f',*config)
    for name,blob in (('shader.cl',shader),('probe.cl',KERNEL.encode()),('rays.bin',rays),('config.bin',packed),('verifier.py',Path(__file__).read_bytes())):
        (out/name).write_bytes(blob)
    gpu=None;report=dict(author='mio',status='RUNNING',scope='GPU_ACTUAL_INPUT_AND_SUPPORT_PLANES',
        source_sha256=sha(shader),queries=count,input_mismatches=0,prism_interval_mismatches=[],independent_mismatches=[])
    try:
        gpu=Gpu(shader+b'\n'+KERNEL.encode(),out/'build.log');report['device']=gpu.identity
        kernel=gpu.kernel('prism_debug');input_buffer=gpu.buffer(len(rays));cfg_buffer=gpu.buffer(len(packed));output=gpu.buffer(count*192*4)
        gpu.write(input_buffer,rays);gpu.write(cfg_buffer,packed);gpu.write(output,b'\0'*(count*192*4))
        gpu.set_args(kernel,C.c_void_p(input_buffer),C.c_void_p(output),C.c_void_p(cfg_buffer),C.c_uint(count))
        gpu.run(kernel,count);raw=gpu.read(output,count*192*4);(out/'output.bin').write_bytes(raw)
        planes=[tuple(float32(x) for x in project['prism_planes'][i:i+4]) for i in range(0,128,4)]
        for i,row in enumerate(struct.iter_unpack('<8f',rays)):
            values=struct.unpack_from('<192f',raw,i*192*4)
            if tuple(values[:8])!=tuple(row):report['input_mismatches']+=1
            expected,ambiguous=reference(row,planes,1,0)
            parameter=values[19] if values[19]>.0002 else values[20]
            interval=not values[21] and values[19]<=values[20] and parameter>.0002 and parameter<row[3]
            if interval!=bool(values[22]):report['prism_interval_mismatches'].append(i)
            if not ambiguous and (expected is not None)!=bool(values[22]):report['independent_mismatches'].append(i)
        report.update(status='OBSERVATION_COMPLETE',output_sha256=sha(raw))
        print(json.dumps({key:value for key,value in report.items() if 'mismatches' not in key},ensure_ascii=False))
        print('MISMATCH COUNTS',report['input_mismatches'],len(report['prism_interval_mismatches']),len(report['independent_mismatches']))
        print('FIRST',report['prism_interval_mismatches'][:12],report['independent_mismatches'][:12])
    except BaseException as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc());raise
    finally:
        if gpu:report['release_statuses']=gpu.close()
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
