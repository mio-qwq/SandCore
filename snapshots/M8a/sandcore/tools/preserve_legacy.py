#!/usr/bin/env python3
"""mio：从不可变、已验收的M7包建立可运行LEGACY与仓库源码归档。

当前源码已进入M8，不能把它重新构建后冒充旧版备份。必须先验证原
验收ZIP哈希，再逐文件读取；脚本不改原ZIP，不把用户盘做factory重置。
沙核盘上保留所有旧SCX及阅读说明，源码/符号在仓库legacy/M7完整归档。
"""
import hashlib
import json
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
PACKAGE=ROOT/'build/SandCore-M7-2026-10-02.zip'
EXPECTED='516811f6477c33a5028f8bfd736d9f7b98cdfc40bce84d35ae730f298d3278fc'

def main():
    if hashlib.sha256(PACKAGE.read_bytes()).hexdigest()!=EXPECTED:raise ValueError('M7原验收包哈希不符，停止备份而非猜测旧版本')
    saved=[]
    with zipfile.ZipFile(PACKAGE) as z:
        for entry in z.namelist():
            if entry.endswith('/'):continue
            runtime=entry.startswith(('sandcore/build/fs/apps/','sandcore/build/fs/bin/')) and entry.lower().endswith('.scx')
            source=entry.startswith('sandcore/user/')
            symbols=entry.startswith('sandcore/build/fs/') and entry.endswith('.map')
            if not (runtime or source or symbols):continue
            relative=entry[len('sandcore/'):]
            # 手工构造固定前缀，额外检查每一段，避免extractall的路径穿越。
            if any(part in ('..','.') for part in relative.split('/')):raise ValueError('ZIP路径不合法')
            blob=z.read(entry)
            archive=ROOT/'legacy/M7'/relative
            archive.parent.mkdir(parents=True,exist_ok=True);archive.write_bytes(blob)
            if runtime:
                short=entry[len('sandcore/build/fs/'):].upper()
                target=ROOT/'build/fs/LEGACY'/short
                target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(blob)
                saved.append({'path':'LEGACY/'+short,'bytes':len(blob),'sha256':hashlib.sha256(blob).hexdigest()})
    notes='SandCore LEGACY / mio\nVerified M7 applications. Open APPS or BIN with Files.\nRun an SCX to start the original program.\nSource and maps: repository legacy/M7.\nThese binaries are preserved, not rebuilt with M8.\n'
    (ROOT/'build/fs/LEGACY/README.TXT').write_text(notes,encoding='utf-8')
    (ROOT/'legacy/M7/provenance.json').write_text(json.dumps({'author':'mio','package':PACKAGE.name,'package_sha256':EXPECTED,'runtime':saved},indent=2),encoding='utf-8')
    print(f'LEGACY: {len(saved)} unchanged M7 executables; source/maps archived')
if __name__=='__main__':main()
