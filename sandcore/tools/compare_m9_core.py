#!/usr/bin/env python3
"""实际优化前后全尺寸桌面逐像素和磁盘内容对照。"""
import hashlib
import json
from pathlib import Path
import re
from mkfs_m9 import read_image

ROOT=Path(__file__).resolve().parents[1]

def ppm(path):
    data=path.read_bytes();m=re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s',data)
    return int(m[1]),int(m[2]),data[m.end():]

def main():
    report=dict(status='RUNNING',cases=[],scope='EXACT_NATIVE_PIXELS_SAME_DIMENSIONS_NO_DOWNSCALING')
    try:
        for i,name in enumerate(('current','legacy'),1):
            before=read_image((ROOT/f'build/m9-matrix-input/{name}-final-v5-30.img').read_bytes())
            after=read_image((ROOT/f'build/m9-matrix-input/{name}-final-v5-31.img').read_bytes())
            if before.keys()!=after.keys():raise AssertionError('磁盘对象集合变化')
            changed=[k for k in before if before[k].payload!=after[k].payload]
            if changed!=['SYS/CORE/CORE.SKM']:raise AssertionError('额外产品字节变化: '+repr(changed))
            old=ppm(ROOT/f'build/m9-performance-20261005-02/disk-{i}/desktop-idle.ppm')
            new=ppm(ROOT/f'build/m9-performance-20261005-03/disk-{i}/desktop-idle.ppm')
            if old[:2]!=new[:2]:raise AssertionError('分辨率变化')
            bottom=old[1]-(48 if i==1 else 32);count=old[0]*bottom*3
            if old[2][:count]!=new[2][:count]:raise AssertionError('完整工作区像素变化')
            report['cases'].append(dict(name=name+'-complete-desktop-pixels',status='PASS',width=old[0],height=bottom,
                rgb_bytes=count,sha256=hashlib.sha256(new[2][:count]).hexdigest(),changed_files=changed))
        report['status']='PASS'
    except BaseException as e:report.update(status='FAIL',failure=repr(e));raise
    finally:(ROOT/'build/m9-core-equivalence-01.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: same full-resolution workspace pixels; only CORE.SKM changed')

if __name__=='__main__':main()
