#!/usr/bin/env python3
"""在独立输出目录重建历史版本；保持原始快照和既有交付产物不变。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from published_baseline import REPO

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def legacy_bridge(work):
    # M8a原Makefile硬绑原M7 ZIP。独立副本仅替换取旧字节的脚本，
    # C/ASM/头文件和原生成资源程序保持快照字节；M7字节来源验证由统一读取器负责。
    script='''import sys
from pathlib import Path
import hashlib, json
sys.path.insert(0, REPO_TOOLS)
from published_baseline import PublishedBaseline
ROOT=Path(__file__).resolve().parents[1]
with PublishedBaseline('M7') as z:
    saved=[]
    for entry in z.namelist():
        if entry.endswith('/'):continue
        runtime=entry.startswith(('sandcore/build/fs/apps/','sandcore/build/fs/bin/')) and entry.lower().endswith('.scx')
        source=entry.startswith('sandcore/user/')
        symbols=entry.startswith('sandcore/build/fs/') and entry.endswith('.map')
        if not (runtime or source or symbols):continue
        blob=z.read(entry);target=ROOT/'legacy/M7'/entry[len('sandcore/'):]
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(blob)
        if runtime:
            short=entry[len('sandcore/build/fs/'):].upper();target=ROOT/'build/fs/LEGACY'/short
            target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(blob)
            saved.append(dict(path='LEGACY/'+short,sha256=hashlib.sha256(blob).hexdigest()))
    notes='SandCore LEGACY / mio\\nVerified M7 applications. Open APPS or BIN with Files.\\nRun an SCX to start the original program.\\nSource and maps: repository legacy/M7.\\nThese binaries are preserved, not rebuilt with M8.\\n'
    (ROOT/'build/fs/LEGACY/README.TXT').write_text(notes,encoding='utf-8')
    (ROOT/'legacy/M7/provenance.json').write_text(json.dumps(dict(source_sha256=z.source_sha,storage_sha256=z.storage_sha,runtime=saved),indent=2)+'\\n',encoding='utf-8')
print('Repository M7 legacy verified and installed')
'''
    script=script.replace('REPO_TOOLS',repr(str(REPO/'sandcore/tools')))
    (work/'tools/preserve_legacy.py').write_text(script,encoding='utf-8')
    makefile=work/'Makefile'
    raw=makefile.read_bytes();needle=b'tools/preserve_legacy.py build/SandCore-M7-2026-10-02.zip'
    if raw.count(needle)!=1:raise ValueError('M8a历史构建规则未知')
    makefile.write_bytes(raw.replace(needle,b'tools/preserve_legacy.py'))

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('version',choices=['M6a','M6','M7','M8a'])
    p.add_argument('--jobs',type=int,default=4);p.add_argument('--out',type=Path);a=p.parse_args()
    if os.name=='nt':raise SystemExit('历史ELF重建请用WSL: build-version.bat '+a.version)
    source=REPO/'snapshots'/a.version/'sandcore';target=a.out or REPO/'rebuild-output'/a.version
    target=target.resolve()
    if target.exists():raise FileExistsError('历史重建输出已存在，请使用新的--out；不覆盖旧数据')
    if target.is_relative_to(source) or target==REPO:raise ValueError('输出不能是源码快照')
    index=json.loads((REPO/'releases/MANIFEST.json').read_text(encoding='utf-8'))
    records=index['historical_sources'][a.version]
    for item in records:
        path=REPO/'snapshots'/a.version/item['path']
        if path.stat().st_size!=item['size'] or sha(path)!=item['sha256']:raise ValueError('历史源码发生变化: '+item['path'])
    work=target/'sandcore';work.parent.mkdir(parents=True);shutil.copytree(source,work)
    shutil.copy2(REPO/'vonwaon-bitmap.ttf.zip',target/'vonwaon-bitmap.ttf.zip')
    if a.version=='M8a':legacy_bridge(work)
    env=dict(os.environ,PYTHONUTF8='1');command=['make','-j'+str(a.jobs)]
    result=subprocess.run(command,cwd=work,env=env)
    if result.returncode:raise SystemExit(result.returncode)
    report=dict(status='HISTORICAL_SOURCE_BUILD_PASS',version=a.version,source=str(source),
        source_files_verified=len(records),command=command,
        adaptation=['M8a独立副本仅替换M7原字节读取脚本与其ZIP文件前置条件'] if a.version=='M8a' else [],
        artifacts=[dict(path=str(work/'build'/name),size=(work/'build'/name).stat().st_size,sha256=sha(work/'build'/name)) for name in ['sandcore.img','sanddata.img']],
        scope='historical snapshot host build; native compiler publication and QEMU runtime remain separate')
    for item in records:
        if sha(REPO/'snapshots'/a.version/item['path'])!=item['sha256']:raise ValueError('重建修改了历史源码')
    (target/'BUILD-RESULT.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False),flush=True)

if __name__=='__main__':main()
