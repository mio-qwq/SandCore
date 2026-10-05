#!/usr/bin/env python3
"""mio：冻结整帧开IRQ及四套回归的精确源码、双盘和真实截图。

为什么另建阶段包：下一次调度修改会改变内核，旧PASS不能自动
移植到新核。这里先验证报告的所有输入，再把当前源码与运行盘
独立保存。旧IF=0对照核已经在设置阶段包冻结，只引用其摘要，
不能把当前核搭配旧样本冒充旧核。任何已存在的阶段包都不覆盖。
"""
import hashlib,json,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-frame-irq'
DEST=ROOT/'build/M8-frame-irq-evidence.zip'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))
def inputs(report):
    for name,digest in report.get('inputs_sha256',{}).items():
        assert sha(ROOT/name)==digest,name

def main():
    assert not DEST.exists(),'阶段包已存在，不得覆盖'
    irq=read(STAGE/'irq-enabled/results.json')
    regressions=read(STAGE/'regressions.json')
    assert irq['status']==regressions['status']=='PASS'
    inputs(irq);inputs(regressions)
    for name in ('integrity.json','api-compat.json'):
        report=read(STAGE/'irq-enabled'/name);assert report['status']=='PASS'
        for path,digest in report.get('sha256',{}).items():assert sha(ROOT/path)==digest,path
    assert sha(STAGE/'irq-enabled/irq-native.scx')==irq['native_sha256']
    for tag in regressions['cases']:
        report=read(STAGE/tag/'results.json')
        if isinstance(report,list):assert len(report)==9
        else:
            assert report['status']=='PASS';inputs(report)
            for path,digest in report.get('sha256',{}).items():
                # 两套脚本的键命名不同：一套写真实文件名，另一套
                # 写生成逻辑名。明确选择，不能忽略无法对应的摘要。
                candidate=(ROOT/path) if '/' in path else (STAGE/tag/path)
                if not candidate.exists() and path.endswith('-native'):candidate=STAGE/tag/(path+'.scx')
                if path=='G2':candidate=STAGE/tag/'G2.scx'
                assert candidate.exists() and sha(candidate)==digest,path
    settings=read(STAGE/'settings-regression/results.json')
    assert sha(STAGE/'settings-regression/settings-native.scx')==settings['native_sha256']
    previous=ROOT/'build/M8-settings-evidence.zip'
    assert sha(previous)=='d663e572baffb835b02b99ffa95e4545eefc1cacb7de6ec2d4905e638fd00e71'
    files=[]
    for path in ROOT.rglob('*'):
        if not path.is_file():continue
        rel=path.relative_to(ROOT)
        if rel.parts[0] in ('build','legacy') or '__pycache__' in rel.parts:continue
        files.append((path,'sandcore/'+rel.as_posix()))
    for name in ('AGENTS.md','HANDOFF.md','M8_GOAL.md'):files.append((ROOT.parent/name,name))
    for name in ('sandcore.img','sanddata.img','boot.bin','kernel.bin','kernel.elf','kernel.sym'):
        files.append((ROOT/'build'/name,'sandcore/build/'+name))
    # 成功脚本目录内保留报告、原生产物/符号、运行命令、实际测试盘
    # 和截图；内存转储只用于诊断，不是发布依赖。失败截图不计入。
    for path in STAGE.rglob('*'):
        if not path.is_file() or 'baseline' in path.relative_to(STAGE).parts:continue
        if path.suffix not in ('.json','.scx','.map','.png','.img'):continue
        assert not any(word in path.name.lower() for word in ('failure','timeout','progress'))
        files.append((path,'sandcore/build/m8-frame-irq/'+path.relative_to(STAGE).as_posix()))
    files.append((ROOT/'legacy/M7/provenance.json','sandcore/legacy/M7/provenance.json'))
    hashes={name:sha(path) for path,name in files}
    assert len(hashes)==len(files),'归档名字重复'
    manifest=dict(author='mio',status='PASS',scope='M8整帧开IRQ/四套回归阶段，完整M8未完成',
        inputs=irq['inputs_sha256'],cases=regressions['cases'],measurements=irq['cases'],
        prior_baseline=dict(archive=previous.name,sha256=sha(previous),meaning='旧IF=0核及精确源码/原样参考，未观察到该轮漏键'),
        runtime='build/sanddata.img仍为HOST开发盘；各测试目录盘独立，原生Settings/Monitor/Shell/CLI/探针来自实际G2编译',
        limits='不代表全分辨率缩放/全组件/压缩图片/全部写实真彩游戏/实时光追或最终原生发布已完成；复制时PIT采样不是精确FPS',
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
