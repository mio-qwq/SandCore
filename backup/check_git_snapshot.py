#!/usr/bin/env python3
"""检查Git备份范围、单文件大小与明确的密钥格式；日志只输出路径。"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
GIT=shutil.which('git') or r'C:\Program Files\Git\cmd\git.exe'
PATTERNS=[
 re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'),
 re.compile(rb'gh[pousr]_[A-Za-z0-9]{36,}'),
 re.compile(rb'github_pat_[A-Za-z0-9_]{70,}'),
 re.compile(rb'AKIA[0-9A-Z]{16}')]

def main():
    paths=subprocess.check_output([GIT,'ls-files','-z'],cwd=ROOT).decode('utf-8').split('\0')
    records=[];findings=[]
    for name in filter(None,paths):
        path=ROOT/name;size=path.stat().st_size
        if size>=100*1024*1024:findings.append(dict(path=name,reason='Git file >=100MiB'))
        if path.name in ('.env','id_rsa','id_ed25519','hosts.yml') or path.suffix.lower() in ('.p12','.pfx'):
            findings.append(dict(path=name,reason='credential-like filename'))
        raw=path.read_bytes()
        if any(pattern.search(raw) for pattern in PATTERNS):findings.append(dict(path=name,reason='private key/token pattern'))
        records.append(dict(path=name,size=size,sha256=hashlib.sha256(raw).hexdigest()))
    report=dict(status='PASS' if not findings else 'REVIEW_REQUIRED',files=len(records),bytes=sum(r['size'] for r in records),
                largest=sorted(records,key=lambda r:r['size'],reverse=True)[:10],findings=findings)
    target=ROOT/'backup-output/git-audit.json';target.parent.mkdir(exist_ok=True)
    target.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False))
    if findings:raise SystemExit(1)

if __name__=='__main__':main()
