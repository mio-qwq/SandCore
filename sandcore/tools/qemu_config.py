#!/usr/bin/env python3
"""mio：Windows QEMU 加速选择，真实测试显式选模式并持久化来源。

SandCore仍是i386客体；x86_64是宿主QEMU可执行文件的目标名称。
当前安装的i386文件只提供TCG，x86_64文件才编入WHPX。单vCPU/
qemu32/传统PC及原设备继续保留；本模块不改镜像，不开启Windows
功能，不重启宿主，也不把软件回退报告成硬件加速性能。
"""
import hashlib
import os
from pathlib import Path
import subprocess
import json
import socket
import time

DIRECTORY=Path(r'C:\Program Files\qemu')


def prefix(mode=None):
    """返回显式启动参数与来源字典；历史测试缺省仍固定TCG。

    auto仅在一个暂停的无盘VM成功初始化WHPX后选它；此探测没有
    数据盘，使用系统分配的临时QMP端口，不会碰用户虚拟机。正式VM选定后不再自动回退，
    避免客体异常被当成加速不可用，又重启一次掩盖错误。显式whpx
    从不回退，让对照测试的加速身份可以由命令本身证明。
    """
    requested=mode or os.environ.get('SANDCORE_QEMU_ACCEL','tcg')
    if requested not in ('tcg','whpx','auto'):
        raise ValueError('SANDCORE_QEMU_ACCEL must be tcg, whpx or auto')
    actual=requested;probe=None
    accelerated=DIRECTORY/'qemu-system-x86_64.exe'
    if requested=='auto':
        actual='tcg'
        if accelerated.is_file():
            with socket.socket() as reservation:
                reservation.bind(('127.0.0.1',0));port=reservation.getsockname()[1]
            probe_command=[str(accelerated),'-accel','whpx','-cpu','qemu32',
                '-smp','1','-m','128','-S','-nodefaults','-display','none',
                '-monitor','none','-serial','none','-qmp',f'tcp:127.0.0.1:{port},server=on,wait=off']
            process=None;dialog=[]
            try:
                # 用TCP逐条握手，避免Windows控制台stdio在初始化/
                # 重新注册读事件时丢掉管道里的首字符。旧两次探测
                # 的超时保留，它们不是硬件不可用的证据。
                process=subprocess.Popen(probe_command,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                    creationflags=subprocess.CREATE_NO_WINDOW)
                deadline=time.monotonic()+15
                while True:
                    if process.poll() is not None:raise RuntimeError('QEMU terminated before QMP greeting')
                    try:connection=socket.create_connection(('127.0.0.1',port),.2);break
                    except OSError:
                        if time.monotonic()>=deadline:raise TimeoutError('QMP connection timeout')
                        time.sleep(.05)
                with connection:
                    connection.settimeout(5)
                    stream=connection.makefile('rb');greeting=json.loads(stream.readline());dialog.append(greeting)
                    if 'QMP' not in greeting:raise RuntimeError('missing QMP greeting')
                    for command_name in ('qmp_capabilities','quit'):
                        connection.sendall((json.dumps(dict(execute=command_name,id=command_name))+'\n').encode())
                        while True:
                            message=json.loads(stream.readline());dialog.append(message)
                            if message.get('id')==command_name:
                                if 'error' in message:raise RuntimeError(message['error'])
                                break
                output,error=process.communicate(timeout=5)
                probe=dict(command=probe_command,exit_code=process.returncode,qmp=dialog,
                    stdout=output.decode('utf-8','replace'),stderr=error.decode('utf-8','replace'))
                if process.returncode==0:actual='whpx'
            except (OSError,ValueError,RuntimeError,subprocess.TimeoutExpired) as error:
                if process is not None:
                    if process.poll() is None:process.terminate()
                    output,errors=process.communicate(timeout=5)
                else:output=errors=b''
                probe=dict(command=probe_command,error=repr(error),qmp=dialog,
                    stdout=output.decode('utf-8','replace'),stderr=errors.decode('utf-8','replace'))
    tcg_target=os.environ.get('SANDCORE_QEMU_TCG_TARGET','i386')
    if tcg_target not in ('i386','x86_64'):raise ValueError('SANDCORE_QEMU_TCG_TARGET must be i386 or x86_64')
    # 相同宿主可执行文件的TCG/WHPX对照显式选x86_64；历史测试
    # 不设此项仍走原i386。两者都用同一qemu32客体CPU和单vCPU。
    executable=accelerated if actual=='whpx' else DIRECTORY/f'qemu-system-{tcg_target}.exe'
    if not executable.is_file():raise FileNotFoundError(executable)
    command=[str(executable),'-accel',actual,'-cpu','qemu32','-smp','1']
    report=dict(requested=requested,selected=actual,tcg_target=tcg_target,executable=str(executable),
        executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
        probe=probe,scope='ACCELERATOR_SELECTION_ONLY')
    return command,report
