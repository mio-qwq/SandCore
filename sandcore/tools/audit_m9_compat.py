#!/usr/bin/env python3
"""M9静态兼容核对：旧公开头、任务布局与原版LEGACY字节，不替代实际运行。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile
from mkfs_m9 import read_image
from published_baseline import PublishedBaseline

ROOT=Path(__file__).resolve().parents[1]
BASELINE=ROOT/'build/SandCore-M7-2026-10-02.zip'
BASELINE_SHA='516811f6477c33a5028f8bfd736d9f7b98cdfc40bce84d35ae730f298d3278fc'
M8A=ROOT/'build/SandCore-M8a-2026-10-04.zip'
M8A_SHA='e9db18c1dd3c6987c4b66a2a33e1bf354e39b6da8b2b920db2cb366f42ca61e5'


def sha_file(path):
    digest=hashlib.sha256()
    with path.open('rb') as stream:
        while block:=stream.read(1024*1024):
            digest.update(block)
    return digest.hexdigest()


def clean(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)


def tokens(text):
    return re.findall(r'[A-Za-z_]\w*|0x[0-9a-fA-F]+|\d+|[^\s]',text)


def signatures(text):
    return {m[2]:tokens(m[1]+'('+m[3]+')') for m in
            re.finditer(r'static\s+inline\s+([\w\s*]+?)\s+(\w+)\s*\((.*?)\)\s*\{',clean(text),re.S)}


def macros(text):
    return {m[1]:tokens(m[2]) for m in re.finditer(r'^\s*#define\s+(\w+)\b([^\n]*)',clean(text),re.M)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data',type=Path,action='append',required=True)
    parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args()
    digest=BASELINE_SHA
    with PublishedBaseline('M7') as archive:
        old_header=archive.read('sandcore/user/SCAPI.H').decode('utf-8-sig')
        old_task=archive.read('sandcore/kernel/task.h').decode('utf-8-sig')
        legacy={('LEGACY/'+name[len('sandcore/build/fs/'):]).upper():archive.read(name)
                for name in archive.namelist()
                if name.startswith(('sandcore/build/fs/apps/','sandcore/build/fs/bin/')) and name.lower().endswith('.scx')}
    current=(ROOT/'user/SCAPI.H').read_text(encoding='utf-8-sig')
    before,after=signatures(old_header),signatures(current)
    if not before or any(after.get(name)!=value for name,value in before.items()):
        raise ValueError('旧公开函数签名缺失或变化')
    old_macros,new_macros=macros(old_header),macros(current)
    if any(new_macros.get(name)!=value for name,value in old_macros.items()):
        raise ValueError('旧公开宏缺失或变化')
    layout=r'typedef\s+struct\s*\{(.*?)\}\s*task_t\s*;'
    current_task=(ROOT/'kernel/task.h').read_text(encoding='utf-8-sig')
    if tokens(re.search(layout,clean(old_task),re.S)[1])!=tokens(re.search(layout,clean(current_task),re.S)[1]):
        raise ValueError('旧task_t字段布局变化')
    # M7不足以代表M8a新增的已发布接口。用独立固定发布包再核对，
    # 不写冻结文件，也不把签名/常量通过当寄存器语义的运行证明。
    with PublishedBaseline('M8a') as archive:
        m8_storage=str(archive.path)
        m8_header=archive.read('sandcore/user/SCAPI.H').decode('utf-8-sig')
        m8_task=archive.read('sandcore/kernel/task.h').decode('utf-8-sig')
    m8_functions,m8_macros=signatures(m8_header),macros(m8_header)
    if not m8_functions or any(after.get(name)!=value for name,value in m8_functions.items()):
        raise ValueError('M8a已发布函数签名缺失或变化')
    if any(new_macros.get(name)!=value for name,value in m8_macros.items()):
        raise ValueError('M8a已发布常量缺失或变化')
    if tokens(re.search(layout,clean(m8_task),re.S)[1])!=tokens(re.search(layout,clean(current_task),re.S)[1]):
        raise ValueError('M8a已发布task_t字段布局变化')
    disks=[]
    for path in args.data:
        blob=path.read_bytes();records=read_image(blob);files=[]
        for name,payload in sorted(legacy.items()):
            if name not in records or records[name].payload!=payload:
                raise ValueError(str(path)+': 原版LEGACY缺失或字节变化: '+name)
            files.append(dict(path=name,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest()))
        if hashlib.sha256(path.read_bytes()).digest()!=hashlib.sha256(blob).digest():
            raise RuntimeError('核对期间输入盘变化')
        disks.append(dict(path=str(path.resolve()),sha256=hashlib.sha256(blob).hexdigest(),legacy=files))
    result=dict(status='STATIC_COMPATIBILITY_PASS_RUNTIME_PENDING',baseline_sha256=digest,
                old_api_count=len(before),old_macro_count=len(old_macros),old_apis=sorted(before),
                m8a=dict(archive=m8_storage,sha256=M8A_SHA,old_api_count=len(m8_functions),
                         old_macro_count=len(m8_macros),old_apis=sorted(m8_functions)),
                current_header_sha256=hashlib.sha256(current.encode('utf-8')).hexdigest(),disks=disks,
                scope='signatures, constants, task layout and immutable legacy bytes; runtime still required')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    with args.out.open('x',encoding='utf-8') as stream:
        json.dump(result,stream,ensure_ascii=False,indent=2);stream.write('\n')
    print(f'M7 API {len(before)}/宏 {len(old_macros)}，M8a API {len(m8_functions)}/宏 {len(m8_macros)}，任务布局及每盘{len(legacy)}份LEGACY原字节通过；运行另验')


if __name__=='__main__':main()
