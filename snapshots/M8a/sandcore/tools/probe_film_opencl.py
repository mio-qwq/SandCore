#!/usr/bin/env python3
"""mio：本机影片GPU通路的能力、真实编译/执行和完整数据回读探测。

Blender的ONEAPI列表为空只说明Cycles目前没有可用ONEAPI设备，不能
由此推断Intel显卡没有其它计算接口。本工具只用已安装OpenCL驱动，
明确选GPU、编译小型整数夹具、实际执行并完整核对，不装驱动、不改
注册表，也不以此夹具吞吐冒充4K电影光追速度或质量。
所有OpenCL对象按创建的逆序释放；编译错误保留原始日志，不能暗换
CPU设备然后继续称GPU成功。此宿主工具不进入SandCore内核或三环。
"""
import argparse
import ctypes as C
import hashlib
import json
from pathlib import Path
import struct
import time
import traceback

SOURCE=b'''__kernel void film_probe(__global uint *out, uint count) {
    uint i=(uint)get_global_id(0);if(i>=count)return;
    uint h=i*374761393u+668265263u;
    h=(h^(h>>13))*1274126177u;h=h^(h>>16);
    out[i]=0xff000000u|(h&0xffffffu);
}'''
COUNT=262144


def require(code,label):
    if code:raise RuntimeError((label,code))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',required=True)
    args=parser.parse_args();out=Path(args.out);out.mkdir(parents=True,exist_ok=False)
    (out/'probe.cl').write_bytes(SOURCE)
    report=dict(author='mio',status='RUNNING',scope='HOST_GPU_COMPUTE_PROBE_ONLY',
                source_sha256=hashlib.sha256(SOURCE).hexdigest(),words=COUNT,platforms=[],
                limitations='真实GPU内核/完整整数输出，不是影片渲染器、光追质量或4K帧率证明')
    cleanup=[]
    try:
        cl=C.WinDLL('OpenCL.dll')
        def api(name,types,result=C.c_int):
            fun=getattr(cl,name);fun.argtypes=types;fun.restype=result;return fun
        p=C.c_void_p;u=C.c_uint;z=C.c_size_t;err=C.c_int;ul=C.c_ulonglong
        platforms=api('clGetPlatformIDs',[u,C.POINTER(p),C.POINTER(u)])
        devices=api('clGetDeviceIDs',[p,ul,u,C.POINTER(p),C.POINTER(u)])
        info=api('clGetDeviceInfo',[p,u,z,p,C.POINTER(z)])
        context=api('clCreateContext',[p,u,C.POINTER(p),p,p,C.POINTER(err)],p)
        queue=api('clCreateCommandQueue',[p,p,ul,C.POINTER(err)],p)
        program=api('clCreateProgramWithSource',[p,u,C.POINTER(C.c_char_p),C.POINTER(z),C.POINTER(err)],p)
        build=api('clBuildProgram',[p,u,C.POINTER(p),C.c_char_p,p,p])
        build_info=api('clGetProgramBuildInfo',[p,p,u,z,p,C.POINTER(z)])
        kernel=api('clCreateKernel',[p,C.c_char_p,C.POINTER(err)],p)
        buffer=api('clCreateBuffer',[p,ul,z,p,C.POINTER(err)],p)
        argument=api('clSetKernelArg',[p,u,z,p])
        launch=api('clEnqueueNDRangeKernel',[p,p,u,C.POINTER(z),C.POINTER(z),C.POINTER(z),u,C.POINTER(p),C.POINTER(p)])
        finish=api('clFinish',[p])
        read=api('clEnqueueReadBuffer',[p,p,u,z,z,p,u,C.POINTER(p),C.POINTER(p)])
        profile=api('clGetEventProfilingInfo',[p,u,z,p,C.POINTER(z)])
        for kind in ('Context','CommandQueue','Program','Kernel','MemObject','Event'):
            api('clRelease'+kind,[p])
        def own(kind,handle,status):
            require(status.value,'create-'+kind)
            if not handle:raise RuntimeError('null '+kind)
            cleanup.append((getattr(cl,'clRelease'+kind),handle));return handle
        n=u();require(platforms(0,None,C.byref(n)),'platform-count')
        handles=(p*n.value)();require(platforms(n,handles,None),'platform-list')
        candidates=[]
        for handle in handles:
            ndev=u();status=devices(handle,4,0,None,C.byref(ndev))
            entry=dict(gpu_status=status,devices=[]);report['platforms'].append(entry)
            if status==-1:continue
            require(status,'gpu-count')
            devs=(p*ndev.value)();require(devices(handle,4,ndev,devs,None),'gpu-list')
            for dev in devs:
                identity={}
                for name,key in [('name',0x102B),('vendor',0x102C),('device_version',0x102F),('driver_version',0x102D)]:
                    text=C.create_string_buffer(4096);require(info(dev,key,len(text),text,None),'device-'+name)
                    identity[name]=text.value.decode('utf-8',errors='replace')
                entry['devices'].append(identity);candidates.append((dev,identity))
        if not candidates:raise RuntimeError('没有GPU设备；不回退CPU冒充')
        device,identity=candidates[0];report['selected']=identity;selected=(p*1)(device);status=err()
        ctx=own('Context',context(None,1,selected,None,None,C.byref(status)),status)
        cmd=own('CommandQueue',queue(ctx,device,2,C.byref(status)),status)
        strings=(C.c_char_p*1)(SOURCE);lengths=(z*1)(len(SOURCE))
        prog=own('Program',program(ctx,1,strings,lengths,C.byref(status)),status)
        start=time.monotonic();code=build(prog,1,selected,b'',None,None)
        size=z();require(build_info(prog,device,0x1183,0,None,C.byref(size)),'build-log-size')
        log=C.create_string_buffer(max(1,size.value));require(build_info(prog,device,0x1183,len(log),log,None),'build-log')
        (out/'build.log').write_bytes(log.value);require(code,'build')
        report['build_seconds']=round(time.monotonic()-start,6)
        fun=own('Kernel',kernel(prog,b'film_probe',C.byref(status)),status)
        mem=own('MemObject',buffer(ctx,2,COUNT*4,None,C.byref(status)),status)
        memarg=p(mem);countarg=u(COUNT)
        require(argument(fun,0,C.sizeof(memarg),C.byref(memarg)),'arg-buffer')
        require(argument(fun,1,C.sizeof(countarg),C.byref(countarg)),'arg-count')
        global_size=(z*1)(COUNT);event=p()
        require(launch(cmd,fun,1,None,global_size,None,0,None,C.byref(event)),'launch')
        cleanup.append((cl.clReleaseEvent,event))
        require(finish(cmd),'finish')
        result=(u*COUNT)();require(read(cmd,mem,1,0,COUNT*4,result,0,None,None),'read-complete')
        expected=bytearray()
        for i in range(COUNT):
            h=(i*374761393+668265263)&0xFFFFFFFF
            h=((h^(h>>13))*1274126177)&0xFFFFFFFF;h^=h>>16
            value=0xFF000000|(h&0xFFFFFF)
            if result[i]!=value:raise AssertionError(('GPU完整值不匹配',i,result[i],value))
            expected+=struct.pack('<I',value)
        actual=bytes(result);assert actual==expected
        (out/'gpu-output.bin').write_bytes(actual)
        begin=ul();end=ul()
        require(profile(event,0x1282,8,C.byref(begin),None),'profile-start')
        require(profile(event,0x1283,8,C.byref(end),None),'profile-end')
        report.update(status='PASS',output_sha256=hashlib.sha256(actual).hexdigest(),
                      device_kernel_nanoseconds=end.value-begin.value,complete_readback='PASS')
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        raise
    finally:
        releases=[]
        for release,handle in reversed(cleanup):releases.append(release(handle))
        report['release_statuses']=releases
        if any(releases):report['status']='FAIL'
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(report,ensure_ascii=False),flush=True)


if __name__=='__main__':main()
