#!/usr/bin/env python3
"""发布宿主工具实际:put/:get往返、空文件和失败后恢复；基线镜像只读。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('boot','data','out'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--host-tool',type=Path,default=ROOT/'tools/scserial.py',help='核对实际随包副本，不改变缺省发布工具')
    args=parser.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    source=out/'source.bin';empty=out/'empty.bin';target=out/'received.bin';zero=out/'zero.bin'
    content=bytes(range(256))*256;source.write_bytes(content);empty.write_bytes(b'')
    existing=out/'existing.bin';existing.write_bytes(b'KEEP-EXISTING')
    def quoted(p):return '"'+p.as_posix()+'"'
    commands=[f':put {quoted(source)} /TMP/PROGRESS.BIN',f':get /TMP/PROGRESS.BIN {quoted(target)}',
              f':put {quoted(empty)} /TMP/EMPTY.BIN',f':get /TMP/EMPTY.BIN {quoted(zero)}',
              f':get /TMP/PROGRESS.BIN {quoted(existing)}', ':get /TMP/MISSING /does/not/exist/out.bin',
              'echo M9-PROGRESS-ALIVE', ':quit']
    report=dict(status='RUNNING',cases=[],scope='HOST_PROGRESS_REAL_TRANSFER_ONLY')
    before=time.monotonic();env=dict(os.environ,PYTHONUTF8='1')
    command=[sys.executable,str(args.host_tool.resolve()),'--boot',str(args.boot.resolve()),'--data',str(args.data.resolve()),
             '--out',str(out/'session'),'--headless']
    try:
        result=subprocess.run(command,input=('\n'.join(commands)+'\n').encode(),capture_output=True,env=env,timeout=150)
        (out/'stdout.txt').write_bytes(result.stdout);(out/'stderr.txt').write_bytes(result.stderr)
        text=result.stdout.decode('utf-8','replace')
        def check(name,okay):
            report['cases'].append(dict(name=name,status='PASS' if okay else 'FAIL'))
            if not okay:raise AssertionError(name)
        check('published-host-exit',result.returncode==0)
        check('PUT-GET-exact-full-content',target.read_bytes()==content)
        check('PUT-GET-empty-file',zero.read_bytes()==b'')
        check('GET-preserve-existing',existing.read_bytes()==b'KEEP-EXISTING')
        for direction in ('PUT','GET'):
            check(direction+'-initial-bar',bool(re.search(direction+r' \[-+\]\s+0\.0%',text)))
            check(direction+'-intermediate-ACK-progress',bool(re.search(direction+r' \[=+-+\]\s+(?!0\.0)[0-9.]+%',text)))
            check(direction+'-speed-and-ETA','KiB/s ETA ' in text)
            check(direction+'-complete-after-commit',bool(re.search(direction+r' \[=+\]\s+100\.0%.*完成',text)))
        check('GET-SHA-before-save','校验并保存' in text)
        check('PUT-SHA-before-commit','校验并提交' in text)
        check('failure-reports-incomplete','GET 未完成，未标记成功' in text)
        check('no-receiving-file-left',not list(out.glob('*.receiving-*')))
        session=json.loads((out/'session/session.json').read_text(encoding='utf-8'))
        check('QEMU-exit-and-source-unchanged',session['exit_code']==0 and all(session['source_unchanged'].values()))
        report.update(status='HOST_PROGRESS_CASES_PASS',wall_seconds=time.monotonic()-before,sha256=hashlib.sha256(content).hexdigest(),
                      stdout_sha256=hashlib.sha256(result.stdout).hexdigest(),commands=commands)
    except BaseException as error:report.update(status='FAIL',failure=repr(error));raise
    finally:(out/'verification.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')

if __name__=='__main__':main()
