"""mio：宿主OpenCL1.2最小运行层；只选GPU，不隐式回退CPU。"""
import ctypes as C
import time


def require(status,label):
    if status:
        raise RuntimeError('%s: OpenCL error %d'%(label,status))


class Gpu:
    def __init__(self,source,log_path):
        # 所有句柄归本实例，构造中途失败也逆序释放。没有跨项目全局
        # 队列/隐藏缓存；独立渲染批次不能借用另一批的缓冲或程序。
        self.owned=[];self.cl=C.WinDLL('OpenCL.dll');self.closed=False
        p=C.c_void_p;u=C.c_uint;z=C.c_size_t;e=C.c_int;l=C.c_ulonglong
        signatures={
            'clGetPlatformIDs':([u,C.POINTER(p),C.POINTER(u)],e),
            'clGetDeviceIDs':([p,l,u,C.POINTER(p),C.POINTER(u)],e),
            'clGetDeviceInfo':([p,u,z,p,C.POINTER(z)],e),
            'clCreateContext':([p,u,C.POINTER(p),p,p,C.POINTER(e)],p),
            'clCreateCommandQueue':([p,p,l,C.POINTER(e)],p),
            'clCreateProgramWithSource':([p,u,C.POINTER(C.c_char_p),C.POINTER(z),C.POINTER(e)],p),
            'clBuildProgram':([p,u,C.POINTER(p),C.c_char_p,p,p],e),
            'clGetProgramBuildInfo':([p,p,u,z,p,C.POINTER(z)],e),
            'clCreateKernel':([p,C.c_char_p,C.POINTER(e)],p),
            'clCreateBuffer':([p,l,z,p,C.POINTER(e)],p),
            'clSetKernelArg':([p,u,z,p],e),
            'clEnqueueNDRangeKernel':([p,p,u,C.POINTER(z),C.POINTER(z),C.POINTER(z),u,C.POINTER(p),C.POINTER(p)],e),
            'clEnqueueReadBuffer':([p,p,u,z,z,p,u,C.POINTER(p),C.POINTER(p)],e),
            'clEnqueueWriteBuffer':([p,p,u,z,z,p,u,C.POINTER(p),C.POINTER(p)],e),
            'clGetEventProfilingInfo':([p,u,z,p,C.POINTER(z)],e),
            'clFinish':([p],e)}
        for kind in ('Context','CommandQueue','Program','Kernel','MemObject','Event'):
            signatures['clRelease'+kind]=([p],e)
        for name,(arguments,result) in signatures.items():
            function=getattr(self.cl,name);function.argtypes=arguments;function.restype=result
        try:
            count=u();require(self.cl.clGetPlatformIDs(0,None,C.byref(count)),'platform-count')
            platforms=(p*count.value)();require(self.cl.clGetPlatformIDs(count,platforms,None),'platforms')
            candidates=[]
            for platform in platforms:
                count=u();status=self.cl.clGetDeviceIDs(platform,4,0,None,C.byref(count))
                if status==-1:continue
                require(status,'gpu-count');devices=(p*count.value)()
                require(self.cl.clGetDeviceIDs(platform,4,count,devices,None),'gpu-devices')
                candidates.extend(devices)
            if not candidates:raise RuntimeError('No GPU OpenCL device; CPU fallback is not claimed')
            self.device=candidates[0];selection=(p*1)(self.device);status=e()
            self.identity={name:self.device_text(key) for name,key in
                           (('name',0x102B),('vendor',0x102C),('version',0x102F),('driver',0x102D))}
            self.context=self.own('Context',self.cl.clCreateContext(None,1,selection,None,None,C.byref(status)),status)
            self.queue=self.own('CommandQueue',self.cl.clCreateCommandQueue(self.context,self.device,2,C.byref(status)),status)
            strings=(C.c_char_p*1)(source);lengths=(z*1)(len(source))
            self.program=self.own('Program',self.cl.clCreateProgramWithSource(self.context,1,strings,lengths,C.byref(status)),status)
            began=time.monotonic()
            code=self.cl.clBuildProgram(self.program,1,selection,b'-cl-std=CL1.2',None,None)
            size=z();require(self.cl.clGetProgramBuildInfo(self.program,self.device,0x1183,0,None,C.byref(size)),'log-size')
            log=C.create_string_buffer(max(1,size.value))
            require(self.cl.clGetProgramBuildInfo(self.program,self.device,0x1183,len(log),log,None),'log')
            log_path.write_bytes(log.value);require(code,'program-build')
            self.build_seconds=time.monotonic()-began
        except BaseException:
            self.close();raise

    def device_text(self,key):
        value=C.create_string_buffer(4096)
        require(self.cl.clGetDeviceInfo(self.device,key,len(value),value,None),'device-info')
        return value.value.decode('utf-8',errors='replace')

    def own(self,kind,handle,status):
        require(status.value,'create-'+kind)
        if not handle:raise RuntimeError('null '+kind)
        self.owned.append((kind,handle));return handle

    def kernel(self,name):
        status=C.c_int()
        return self.own('Kernel',self.cl.clCreateKernel(self.program,name.encode('ascii'),C.byref(status)),status)

    def buffer(self,size):
        if not 1<=size<=1024**3:raise ValueError('invalid buffer byte capacity')
        status=C.c_int()
        return self.own('MemObject',self.cl.clCreateBuffer(self.context,1,size,None,C.byref(status)),status)

    def set_args(self,kernel,*arguments):
        # 传句柄要显式包装void*，整数值明确用uint/float；Python裸int
        # 不能从调用位置猜它是内存地址还是尺寸，避免64位宿主截断。
        for index,argument in enumerate(arguments):
            require(self.cl.clSetKernelArg(kernel,index,C.sizeof(argument),C.byref(argument)),'argument-%d'%index)

    def write(self,buffer,data):
        block=C.create_string_buffer(data,len(data))
        require(self.cl.clEnqueueWriteBuffer(self.queue,buffer,1,0,len(data),block,0,None,None),'write')

    def read(self,buffer,size):
        block=C.create_string_buffer(size)
        require(self.cl.clEnqueueReadBuffer(self.queue,buffer,1,0,size,block,0,None,None),'read')
        return block.raw

    def run(self,kernel,size):
        global_size=(C.c_size_t*1)(size);event=C.c_void_p()
        require(self.cl.clEnqueueNDRangeKernel(self.queue,kernel,1,None,global_size,None,0,None,C.byref(event)),'launch')
        # 分带/采样批次限制单次GPU占用，避免Windows显示驱动TDR。
        # 每个批次确实等待完成并读取设备计时，不用Python提交耗时当
        # 光追时间；事件立即释放，2520帧不会累计几百万个事件句柄。
        try:
            require(self.cl.clFinish(self.queue),'finish')
            begin=C.c_ulonglong();end=C.c_ulonglong()
            require(self.cl.clGetEventProfilingInfo(event,0x1282,8,C.byref(begin),None),'profile-begin')
            require(self.cl.clGetEventProfilingInfo(event,0x1283,8,C.byref(end),None),'profile-end')
            return (end.value-begin.value)/1e9
        finally:require(self.cl.clReleaseEvent(event),'release-event')

    def close(self):
        if self.closed:return []
        statuses=[getattr(self.cl,'clRelease'+kind)(handle) for kind,handle in reversed(self.owned)]
        self.owned=[];self.closed=True
        if any(statuses):raise RuntimeError(('release-errors',statuses))
        return statuses
