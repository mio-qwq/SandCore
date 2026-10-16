"""仅对固定发行默认文本做候选目录转换；不打开或写入磁盘镜像。"""
from pathlib import Path
import base64
import dataclasses
import hashlib
import json

def _spec():
    # 同目录固定数据同时供源码生成与离线迁移，避免两套默认表漂移。
    return json.loads(Path(__file__).with_name('m10a2_defaults.json').read_text(encoding='utf-8'))
def _fold(name):return name.translate(str.maketrans('abcdefghijklmnopqrstuvwxyz','ABCDEFGHIJKLMNOPQRSTUVWXYZ'))
def _file(records,path):
    value=records.get(_fold(path.lstrip('/')))
    return value is not None and value.payload is not None
def _modern(spec,command):
    if ' ' in command or '\t' in command:return None
    key=_fold('/'+command.lstrip('/'))
    for row in spec['apps']:
        if row['legacy'] and _fold(row['legacy'])==key:return row['modern']
    index=spec['aliases'].get(key)
    return spec['apps'][index]['modern'] if index is not None else None
def _shortcut(label,target,icon):
    # 与现有ScConfigSerialize的SCLNK2数值/转义合同相同；没有包格式。
    values=dict(arguments='',application='',icon=icon,kind=3,label=label,profile=1,target=target,version=1)
    lines=['SCLNK2','[shortcut]']
    for key,value in sorted(values.items()):
        lines.append(key+'='+(str(value)+'u' if isinstance(value,int) else json.dumps(value,ensure_ascii=False)))
    return ('\n'.join(lines)+'\n').encode('utf-8')

def upgrade_records(records,generation,archive=None):
    """records为打包器已合并发布树的Record字典；generation为其单调分配器。"""
    spec=_spec()
    baseline={_fold(row['path']):row for row in spec['files']}
    report=dict(version=1,baseline_sha256=spec['source_sha256'],upgraded=[],preserved=[])
    # 先构造全部小候选，异常不能留下半个目录转换结果。
    pending=[]
    for key,record in records.items():
        if key!='SYS/MENU.CFG' and not (key.startswith('DESK/') and key.endswith('.LNK')):continue
        row=baseline.get(key)
        if row is None:
            report['preserved'].append(dict(path=record.name,reason='CUSTOM_PATH'));continue
        original=base64.b64decode(row['data_base64'],validate=True)
        if hashlib.sha256(original).hexdigest()!=row['sha256']:raise ValueError('默认原像数据损坏')
        if record.payload is None or bytes(record.payload)!=original or (record.uid,record.gid,record.mode)!=(row['uid'],row['gid'],row['mode']):
            report['preserved'].append(dict(path=record.name,reason='CONTENT_OR_IDENTITY_CHANGED'));continue
        if key=='SYS/MENU.CFG':
            lines=[]
            for line in original.decode('utf-8').splitlines(keepends=True):
                raw=line.rstrip('\r\n');ending=line[len(raw):]
                fields=raw.split('|')
                if len(fields)==3:
                    target=_modern(spec,fields[1])
                    if target and _file(records,target):fields[1]=target;raw='|'.join(fields)
                lines.append(raw+ending)
            output=''.join(lines).encode('utf-8')
        else:
            label,command,icon=original.decode('utf-8').splitlines()
            target=_modern(spec,command)
            output=_shortcut(label,target,icon) if target and _file(records,target) else original
        if output==original:
            report['preserved'].append(dict(path=record.name,reason='NO_INSTALLED_MODERN_DEFAULT'));continue
        pending.append((key,record,output,row))
    for key,record,output,row in pending:
        revision=generation()
        if not isinstance(revision,int) or revision<=record.generation or revision>0xffffffff:raise ValueError('新对象代数必须单调')
        if archive is not None:archive(record)
        records[key]=dataclasses.replace(record,payload=output,generation=revision)
        report['upgraded'].append(dict(path=record.name,uid=record.uid,gid=record.gid,mode=record.mode,
            before_sha256=row['sha256'],after_sha256=hashlib.sha256(output).hexdigest(),
            old_generation=record.generation,new_generation=revision,bytes=len(output)))
    return report
