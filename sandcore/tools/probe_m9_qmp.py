#!/usr/bin/env python3
"""无盘、无串口的Windows QMP诊断，不计作SandCore运行验收。"""
import argparse
import json
from pathlib import Path
import secrets
import subprocess
import time
from scserial import WindowsPipe, StdioQmp
import qemu_config


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--single', action='store_true')
    parser.add_argument('--stdio', action='store_true')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    command, source = qemu_config.prefix('tcg')
    if args.single:
        command[command.index('-accel')+1] = 'tcg,thread=single'
    name = 'sandcore-'+secrets.token_hex(16)+'-qmp'
    command += ['-m','128','-S','-nodefaults','-display','none',
                '-monitor','none','-serial','none','-nic','none','-qmp','stdio' if args.stdio else 'pipe:'+name]
    report = dict(scope='HOST_QMP_ONLY_NO_GUEST_IMAGE',command=command,
                  executable=source,events=[])
    process, pipe, buffer = None, None, b''
    with (args.out/'qemu.log').open('xb') as log:
        try:
            process = subprocess.Popen(command,stdout=subprocess.PIPE if args.stdio else log,
                                       stdin=subprocess.PIPE if args.stdio else subprocess.DEVNULL,stderr=log,bufsize=0,
                                       creationflags=subprocess.CREATE_NO_WINDOW)
            if args.stdio:
                pipe=StdioQmp(process)
                report['transport']='ANONYMOUS_PARENT_CHILD_ONLY'
            else:
                pipe = WindowsPipe(name, process.pid)
                report['acl']=pipe.acl
            report['pid'] = process.pid
            def receive():
                nonlocal buffer
                deadline = time.monotonic()+10
                while b'\n' not in buffer:
                    buffer += pipe.read()
                    if process.poll() is not None:
                        raise RuntimeError('QEMU退出')
                    if time.monotonic() > deadline:
                        raise TimeoutError('无盘QMP响应超时')
                    time.sleep(.005)
                line, buffer = buffer.split(b'\n',1)
                message = json.loads(line)
                report['events'].append(dict(receive=message))
                return message
            receive()
            for index, operation in enumerate(('qmp_capabilities','query-status','cont','query-status','quit'),1):
                request = dict(execute=operation,id=index)
                report['events'].append(dict(send=request))
                pipe.write((json.dumps(request)+'\n').encode())
                while receive().get('id') != index:
                    pass
            report['status']='HOST_QMP_RESPONDED'
        except BaseException as error:
            report.update(status='FAIL',failure=repr(error))
        finally:
            if process:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=5)
                report['exit_code']=process.returncode
            if pipe:
                pipe.close()
            (args.out/'probe.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(report['status'],report.get('failure',''))
    return 0 if report['status']=='HOST_QMP_RESPONDED' else 1


if __name__=='__main__':
    raise SystemExit(main())
