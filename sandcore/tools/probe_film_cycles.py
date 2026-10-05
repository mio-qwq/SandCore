#!/usr/bin/env python3
"""mio：由真实Blender枚举Cycles设备，不改正在渲染的工程或用户偏好。"""
import datetime
import hashlib
import json
from pathlib import Path
import sys

import bpy


def main():
    # mio：使用后台独立实例，只查询设备。核显能运行OpenCL并不意味着
    # 当前Cycles有该核显的后端；必须保存真实枚举而不能从厂商名推断。
    # 不调用save_userpref，不启动render，不连接/终止整片工作进程。
    args=sys.argv[sys.argv.index('--')+1:]
    if len(args)!=1:
        raise ValueError('provide one new output directory after --')
    out=Path(args[0]);out.mkdir(parents=True,exist_ok=False)
    source=Path(__file__).read_bytes();(out/'probe_film_cycles.py').write_bytes(source)
    preferences=bpy.context.preferences.addons['cycles'].preferences
    report={'author':'mio','scope':'HOST_CYCLES_DEVICE_ENUMERATION_ONLY',
            'observed_local':datetime.datetime.now().isoformat(timespec='seconds'),
            'blender_version':bpy.app.version_string,
            'executable':bpy.app.binary_path,
            'probe_sha256':hashlib.sha256(source).hexdigest(),
            'backends':{},'render_started':False,
            'limitations':'真实设备枚举，不是渲染速度/画质或SandCore GPU驱动证明'}
    for backend in ('ONEAPI','CUDA','OPTIX','HIP'):
        try:
            preferences.compute_device_type=backend
            preferences.get_devices()
            devices=[{'name':device.name,'type':device.type,'id':device.id}
                     for device in preferences.devices]
            report['backends'][backend]={'devices':devices,
                'gpu_devices':[device for device in devices if device['type']!='CPU']}
        except Exception as error:
            report['backends'][backend]={'error':repr(error),'gpu_devices':[]}
    report['available_gpu_backends']=[backend for backend,entry in report['backends'].items()
                                     if entry['gpu_devices']]
    report['status']='ENUMERATED'
    (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,indent=2),flush=True)


if __name__=='__main__':main()
