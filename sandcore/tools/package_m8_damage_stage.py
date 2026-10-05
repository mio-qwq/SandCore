#!/usr/bin/env python3
"""mio：冻结局部合成、十五显示组合及全部相关回归的精确阶段。

源码继续修改后，旧PASS只对归档内的精确输入负责。先逐项核对
核/头/应用源码、真实G2产物及上一237扇区参考，再保存源文件、
双盘、成功截图和各独立测试盘。归档写完重开CRC/SHA，不覆盖
已经存在的阶段包；这不是整个M8或正式原生运行盘的发布。
"""
import hashlib
import json
import zipfile
from pathlib import Path
from package_m8_frame_irq_stage import inputs

ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-damage'
DEST=ROOT/'build/M8-damage-evidence.zip'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))


def main():
    assert not DEST.exists(),'阶段包已存在，不覆盖'
    partial=read(STAGE/'results.json');matrix=read(STAGE/'matrix/results.json')
    regressions=read(STAGE/'regressions.json');frame=read(STAGE/'frame-regression/results.json')
    for report in (partial,matrix,regressions,frame):
        assert report['status']=='PASS';inputs(report)
    assert len(matrix['cases'])==18
    assert sha(STAGE/'damage-native.scx')==partial['native_sha256']==matrix['native_sha256']
    assert sha(STAGE/'damage-native.map')==matrix['native_map_sha256']
    assert sha(STAGE/'frame-regression/irq-native.scx')==frame['native_sha256']
    for case in matrix['cases']:
        assert case['status']=='PASS' and case['native_sha256']==partial['native_sha256']
        inputs(case)
    for name in ('integrity.json','api-compat.json'):
        report=read(STAGE/name);assert report['status']=='PASS'
        for path,digest in report.get('sha256',{}).items():assert sha(ROOT/path)==digest,path
    for tag in regressions['cases']:
        report=read(STAGE/tag/'results.json')
        if isinstance(report,list):assert len(report)==9
        else:
            assert report['status']=='PASS';inputs(report)
            for path,digest in report.get('sha256',{}).items():
                candidate=ROOT/path if '/' in path else STAGE/tag/path
                if path=='G2':candidate=STAGE/tag/'G2.scx'
                if not candidate.exists() and path.endswith('-native'):candidate=STAGE/tag/(path+'.scx')
                assert candidate.exists() and sha(candidate)==digest,path
            if 'native_sha256' in report:assert sha(STAGE/tag/'settings-native.scx')==report['native_sha256']
    latency=[]
    for tag in ('damage-1024','damage-1080'):
        directory=ROOT/'build/m8-latency'/tag;report=read(directory/'results.json')
        assert report['status']=='PASS'
        for path,digest in report['sha256'].items():assert sha(ROOT/path)==digest,path
        latency.append(directory)
    prior=ROOT/'build/M8-idle-yield-evidence.zip'
    assert sha(prior)=='9ffdc24a9da74c81cb32b205be166fed1b1a9d27581f30d68a40a28c1279a23a'
    # 同一开发盘工具是拖动对照的必要条件；不同Shell/Settings机器码
    # 不能把合成统计差异全部归于内核。旧观测下限仍需分开报告。
    with zipfile.ZipFile(prior) as archive:
        for tag in ('1024','1080'):
            old=json.loads(archive.read(f'sandcore/build/m8-latency/idle-yield-{tag}/results.json'))
            new=read(ROOT/f'build/m8-latency/damage-{tag}/results.json')
            assert old['user_sha256']==new['user_sha256'],'对照应用字节不同'
    files=[]
    for path in ROOT.rglob('*'):
        if not path.is_file():continue
        rel=path.relative_to(ROOT)
        if rel.parts[0] in ('build','legacy') or '__pycache__' in rel.parts:continue
        files.append((path,'sandcore/'+rel.as_posix()))
    for name in ('AGENTS.md','HANDOFF.md','M8_GOAL.md'):files.append((ROOT.parent/name,name))
    for name in ('sandcore.img','sanddata.img','boot.bin','kernel.bin','kernel.elf','kernel.sym'):
        files.append((ROOT/'build'/name,'sandcore/build/'+name))
    for directory in (STAGE,*latency):
        for path in directory.rglob('*'):
            if not path.is_file() or path.suffix not in ('.json','.scx','.map','.png','.img'):continue
            if 'history' in path.relative_to(directory).parts:continue
            if any(word in path.name.lower() for word in ('failure','timeout','progress')):continue
            files.append((path,'sandcore/'+path.relative_to(ROOT).as_posix()))
    snapshot=STAGE/'matrix-verifier-before-readiness.py'
    if snapshot.exists():
        assert sha(snapshot)==matrix['earlier_verifier_sha256']
        files.append((snapshot,'sandcore/'+snapshot.relative_to(ROOT).as_posix()))
    files.append((ROOT/'legacy/M7/provenance.json','sandcore/legacy/M7/provenance.json'))
    hashes={name:sha(path) for path,name in files};assert len(files)==len(hashes)
    manifest=dict(author='mio',status='PASS',scope='M8局部合成与相关回归阶段，整个M8未完成',
        partial=partial,matrix=matrix,reference_kernel=dict(archive=prior.name,sha256=sha(prior)),
        checks='真实G2同一应用，15模式缩放+3Classic，全非任务栏PCI含光标等价，按住拖动，RGB/VGA/32MB，新旧盘CPU，六页设置，19CLI，大帧，1024/1080输入延迟',
        runtime='默认双盘仍HOST开发版本；真实G2产物及独立测试盘另存，未发布完整M8原生正式盘',
        limits='模式合成等价不等于全部工具布局/现代画质游戏或全GUI60Hz；压缩图片、全组件、写实真彩游戏、实时光追和最终M8继续',
        sha256=hashes)
    with zipfile.ZipFile(DEST,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path,name in files:archive.write(path,name)
        archive.writestr('stage-manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    with zipfile.ZipFile(DEST) as archive:
        assert archive.testzip() is None
        for name,digest in hashes.items():assert hashlib.sha256(archive.read(name)).hexdigest()==digest,name
    digest=sha(DEST);DEST.with_suffix('.sha256').write_text(digest+'  '+DEST.name+'\n',encoding='ascii')
    print(f'{DEST}: {len(files)} files, SHA256 {digest}')


if __name__=='__main__':main()
