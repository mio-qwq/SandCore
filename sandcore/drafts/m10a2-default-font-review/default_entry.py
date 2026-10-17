"""原样运行四文件入口，仅增加临时写入层与只读/真实输入QMP诊断口。"""
from pathlib import Path
import importlib.util
import json
import socket
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/m10a2-default-entry-01'
OUT.mkdir(exist_ok=False)
spec = importlib.util.spec_from_file_location('playable', ROOT/'drafts/m10a2-minimal-playable/run.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
# 模块路径只决定两盘目录；不复制或改写用户两盘。
module.__file__ = r'C:\Users\Administrator\Desktop\test\run.py'
sys.argv = [module.__file__]

def launch(command, **kwargs):
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    command += ['-snapshot', '-qmp', f'tcp:127.0.0.1:{port},server=on,wait=off']
    with (OUT/'qemu.stdout.txt').open('wb') as stdout, (OUT/'qemu.stderr.txt').open('wb') as stderr:
        process = subprocess.Popen(command, stdout=stdout, stderr=stderr, **kwargs)
        events = []
        record = dict(command=command, pid=process.pid, snapshot=True, actions=events,
                      scope='DEFAULT_ENTRY_VISUAL_DIAGNOSIS_ONLY')
        def persist():
            (OUT/'entry.json').write_text(json.dumps(record, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
        persist()
        connection = None
        try:
            deadline = time.monotonic()+15
            while connection is None:
                if process.poll() is not None:
                    raise RuntimeError('QEMU terminated: '+(OUT/'qemu.stderr.txt').read_text())
                try:
                    connection = socket.create_connection(('127.0.0.1', port), timeout=2)
                except OSError:
                    if time.monotonic()>deadline: raise
                    time.sleep(.1)
            connection.settimeout(20)
            stream = connection.makefile('rwb')
            record['greeting'] = json.loads(stream.readline())
            def qmp(name, arguments=None):
                request = dict(execute=name, id=str(len(events)))
                if arguments: request['arguments']=arguments
                stream.write((json.dumps(request)+'\n').encode());stream.flush()
                while True:
                    response = json.loads(stream.readline())
                    if response.get('id')==request['id']:return response
            qmp('qmp_capabilities')
            print(json.dumps(dict(ready=True, pid=process.pid, out=str(OUT))), flush=True)
            quit_requested=False
            for line in sys.stdin:
                request=json.loads(line)
                name=request.pop('execute')
                result=qmp(name, request.get('arguments'))
                events.append(dict(request=dict(execute=name, **request), result=result))
                persist();print(json.dumps(result), flush=True)
                if name=='quit':quit_requested=True;break
            if not quit_requested and process.poll() is None:qmp('quit')
            return process.wait(timeout=15)
        finally:
            if process.poll() is None:
                process.terminate();process.wait(timeout=10)
            if connection:connection.close()
            record['exit_code']=process.returncode;persist()

module.subprocess.call=launch
raise SystemExit(module.main())
