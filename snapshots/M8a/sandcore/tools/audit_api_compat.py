#!/usr/bin/env python3
"""mio：与不可变M7验收头核对已有API签名/常量/任务结构。

这是静态兼容合同核对，不冒充系统调用行为验证。旧忽略寄存器、
输出边界、历史SCX运行另由无头QEMU/sysprobe验证。
"""
import hashlib,json,re,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/m8-system';OUT.mkdir(parents=True,exist_ok=True)

def clean(text):
    return re.sub(r'/\*.*?\*/|//[^\n]*','',text,flags=re.S)

def tokens(text):
    return re.findall(r'[A-Za-z_]\w*|0x[0-9a-fA-F]+|\d+|[^\s]',text)

def functions(text):
    return {m[2]:tokens(m[1]+'('+m[3]+')') for m in
            re.finditer(r'static\s+inline\s+([\w\s*]+?)\s+(\w+)\s*\((.*?)\)\s*\{',clean(text),re.S)}

def defines(text):
    return {m[1]:tokens(m[2]) for m in re.finditer(r'^\s*#define\s+(\w+)\b([^\n]*)',clean(text),re.M)}

def main():
    package=ROOT/'build/SandCore-M7-2026-10-02.zip'
    with zipfile.ZipFile(package) as archive:
        original=archive.read('sandcore/user/SCAPI.H').decode('utf-8-sig')
        old_task=archive.read('sandcore/kernel/task.h').decode('utf-8-sig')
    current=(ROOT/'user/SCAPI.H').read_text(encoding='utf-8-sig')
    before,after=functions(original),functions(current)
    assert before and all(name in after and after[name]==signature for name,signature in before.items()),'已有API缺失或签名改变'
    constants,new_constants=defines(original),defines(current)
    assert all(name in new_constants and value==new_constants[name] for name,value in constants.items()),'旧头宏被删除/改值'
    old_sys,new_sys=defines(old_task),defines((ROOT/'kernel/task.h').read_text(encoding='utf-8-sig'))
    assert all(name in new_sys and value==new_sys[name] for name,value in old_sys.items()),'旧调用号/任务槽数改变'
    pattern=r'typedef\s+struct\s*\{(.*?)\}\s*task_t\s*;'
    assert tokens(re.search(pattern,clean(old_task),re.S)[1])==tokens(re.search(pattern,clean((ROOT/'kernel/task.h').read_text(encoding='utf-8-sig')),re.S)[1]),'旧task_t字段布局改变'
    result=dict(author='mio',status='PASS',baseline='user-accepted M7 immutable archive',
                old_api_count=len(before),old_macro_count=len(constants),
                old_apis=sorted(before),added_apis=sorted(after.keys()-before.keys()),
                package_sha256=hashlib.sha256(package.read_bytes()).hexdigest(),
                current_header_sha256=hashlib.sha256((ROOT/'user/SCAPI.H').read_bytes()).hexdigest(),
                limitation='核对签名/宏/功能号/任务字段；动态寄存器/缓冲/消费行为需真实QEMU证据')
    (OUT/'api-compat.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,ensure_ascii=False,indent=2))

if __name__=='__main__':main()
