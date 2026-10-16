#!/usr/bin/env python3
"""按精确原字节摘要整合已有源补丁；只忽略补丁载体的CRLF差异。"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]

def digest(body):return hashlib.sha256(body).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--patch',type=Path,required=True)
    parser.add_argument('--pins',type=Path,required=True)
    parser.add_argument('--pin-root',type=Path,required=True)
    parser.add_argument('--receipt',type=Path,required=True)
    args=parser.parse_args()
    base=args.pin_root.resolve(strict=True);receipt=args.receipt.resolve()
    if not base.is_relative_to(ROOT) or not receipt.is_relative_to(ROOT/'sandcore/build') or receipt.exists():
        raise ValueError('仅允许工作区原字节与build内新凭证')
    pins=json.loads(args.pins.read_text(encoding='utf-8'));original={}
    for name,wanted in pins.items():
        path=(base/name).resolve(strict=True)
        if not path.is_relative_to(ROOT):raise ValueError('原像路径越界')
        body=path.read_bytes()
        if digest(body)!=wanted:raise ValueError('原像已经改变：'+str(path))
        original[path]=body
    lines=args.patch.read_text(encoding='utf-8').splitlines();index=0;planned={}
    while index<len(lines):
        if not lines[index].startswith('--- a/'):raise ValueError('只接受已有文件修改，不允许删除/改名')
        path=(ROOT/lines[index][6:]).resolve(strict=True);index+=1
        if index>=len(lines) or lines[index]!='+++ b/'+path.relative_to(ROOT).as_posix():
            raise ValueError('补丁目标不一致')
        index+=1
        if path not in original or path in planned:raise ValueError('目标未由精确摘要授权')
        old=original[path].decode('utf-8').splitlines(keepends=True);cursor=0;output=[]
        ending='\r\n' if original[path].count(b'\r\n') else '\n'
        while index<len(lines) and lines[index].startswith('@@ '):
            match=re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@.*',lines[index])
            if not match:raise ValueError('无法解释hunk')
            start=int(match[1])-1;old_count=int(match[2] or 1);new_count=int(match[4] or 1)
            if start<cursor or start>len(old):raise ValueError('hunk顺序/范围错误')
            output.extend(old[cursor:start]);cursor=start;index+=1;consumed=produced=0
            while index<len(lines) and not lines[index].startswith(('@@ ','--- a/')):
                line=lines[index];index+=1
                if not line or line[0] not in ' +-':raise ValueError('不支持的补丁行')
                if line[0] in ' -':
                    if cursor>=len(old) or old[cursor].rstrip('\r\n')!=line[1:]:
                        raise ValueError('精确原像hunk内容不同：'+str(path))
                    if line[0]==' ':output.append(old[cursor]);produced+=1
                    cursor+=1;consumed+=1
                else:output.append(line[1:]+ending);produced+=1
            if consumed!=old_count or produced!=new_count:raise ValueError('hunk长度声明不同')
        output.extend(old[cursor:]);planned[path]=''.join(output).encode('utf-8')
    if set(planned)!=set(original):raise ValueError('摘要清单与实际补丁目标不同')
    record=dict(status='PREPARED',patch_sha256=digest(args.patch.read_bytes()),
        files=[dict(path=path.relative_to(ROOT).as_posix(),before_sha256=digest(original[path]),
            after_sha256=digest(body)) for path,body in planned.items()])
    receipt.parent.mkdir(parents=True,exist_ok=True)
    receipt.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    # 全部原像、路径和hunk已核完才进入写入；不使用模糊匹配，不触碰
    # 清单外文件，保留未改行的实际行尾，凭证先落盘便于中断后恢复。
    for path,body in planned.items():
        if path.read_bytes()!=original[path]:raise ValueError('写入前原像出现并发修改')
        path.write_bytes(body)
    record['status']='APPLIED'
    receipt.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print('PINNED_PATCH_APPLIED',len(planned),'files')

if __name__=='__main__':main()
