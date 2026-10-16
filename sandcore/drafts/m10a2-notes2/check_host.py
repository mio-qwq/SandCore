#!/usr/bin/env python3
"""实际SCCC宿主适配器检查；不冒充客体生成或执行。"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tests/m10a2'))
import test_scd_format as backend

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True);args=parser.parse_args()
    out=args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    backend.ROOT,backend.OUT=ROOT,out
    compiler=backend.build_sccc('SCCC-notes2-host')
    vfs=out/'VFS';inputs={}
    for source in sorted((ROOT/'drafts/m10a2-notes2').iterdir()):
        if source.suffix.lower() not in ('.c','.h','.json'):continue
        target=vfs/'TMP/NOTES2'/source.name.upper();target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(source.read_bytes());inputs[str(source)]=sha(source)
    for directory in ('user','user/stdlib','user/sclib','user/runtime'):
        for source in sorted((ROOT/directory).iterdir()):
            if not source.is_file() or source.suffix.lower()!='.h':continue
            if directory not in ('user','user/stdlib') and source.name not in ('SCLIB.H','SCLDR.H'):continue
            target=vfs/'SYS/INC'/source.name.upper();target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(source.read_bytes());inputs[str(source)]=sha(source)
    (vfs/'HOME').mkdir()
    command=[str(compiler),'/',str(vfs),'/TMP/NOTES2/NOTES2.C','/HOME/NOTES2.SCX',
        '--dynamic','/TMP/NOTES2/NOTES2.JSON','0']
    result=subprocess.run(command,capture_output=True,text=True)
    (out/'sccc.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    artifact=vfs/'HOME/NOTES2.SCX'
    report=dict(status='HOST_SCCC_PASS' if result.returncode==0 else 'FAIL',guest_execution=False,
        command=command,exit_code=result.returncode,source_sha256=inputs,compiler_sha256=sha(compiler))
    if artifact.exists():report.update(bytes=artifact.stat().st_size,sha256=sha(artifact))
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(report['status'],report.get('bytes'));print((result.stdout+result.stderr)[-1800:])
    return result.returncode

if __name__=='__main__':raise SystemExit(main())
