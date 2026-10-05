#!/usr/bin/env python3
"""mio：冻结空量子优化、同一原生产物对照与相关回归的精确阶段。

参考核来自上一不可变整帧包。当前源码只与优化阶段/回归报告
匹配，不能套到旧参考。保存两次真实测量、独立测试盘和截图，
所有文件重开验CRC/SHA；不是整个M8或正式原生运行盘的发布。
"""
import hashlib,json,zipfile
from pathlib import Path
from package_m8_frame_irq_stage import inputs

ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-idle-yield'
DEST=ROOT/'build/M8-idle-yield-evidence.zip'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))

def main():
    assert not DEST.exists(),'阶段包已存在，不覆盖'
    measurement=read(STAGE/'optimized/results.json');old=read(STAGE/'baseline/results.json')
    regressions=read(STAGE/'regressions.json');frame=read(STAGE/'frame-regression/results.json')
    assert measurement['status']==regressions['status']==frame['status']=='PASS'
    for report in (measurement,regressions,frame):inputs(report)
    assert old['status']=='BASELINE' and old['native_sha256']==measurement['native_sha256']
    for tag in ('baseline','optimized'):
        assert sha(STAGE/tag/'sched-native.scx')==measurement['native_sha256']
    assert sha(STAGE/'frame-regression/irq-native.scx')==frame['native_sha256']
    for name in ('integrity.json','api-compat.json'):
        report=read(STAGE/'optimized'/name);assert report['status']=='PASS'
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
    for tag in ('idle-yield-1024','idle-yield-1080'):
        directory=ROOT/'build/m8-latency'/tag;report=read(directory/'results.json')
        assert report['status']=='PASS'
        for path,digest in report['sha256'].items():assert sha(ROOT/path)==digest,path
        latency.append(directory)
    prior=ROOT/'build/M8-frame-irq-evidence.zip'
    assert sha(prior)=='25ad98e580302a1c6527b4aff5669e7b76bdff133e6220a72af7610cfebd3235'
    with zipfile.ZipFile(prior) as archive:
        for path,digest in old['inputs_sha256'].items():
            name='sandcore/'+path
            # 计算探针在上一包封存后新增，但旧核/头和原生生成
            # 来源必须逐项与那包相同；探针源码则本包独立保留。
            if name not in archive.namelist():
                assert path=='user/schedprobe.c' and sha(ROOT/path)==digest,path
            else:assert hashlib.sha256(archive.read(name)).hexdigest()==digest,path
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
            if any(word in path.name.lower() for word in ('failure','timeout','progress')):continue
            files.append((path,'sandcore/'+path.relative_to(ROOT).as_posix()))
    files.append((ROOT/'legacy/M7/provenance.json','sandcore/legacy/M7/provenance.json'))
    hashes={name:sha(path) for path,name in files};assert len(files)==len(hashes)
    manifest=dict(author='mio',status='PASS',scope='M8任务0交还空量子与相关回归阶段，整个M8未完成',
        measurement=measurement,reference_kernel=dict(archive=prior.name,sha256=sha(prior),baseline=old['inputs_sha256']),
        checks='同一G2计数计算/旧YIELD/双忙任务/键鼠/全页回收，RGB/VGA/32MB，CPU新旧盘，六页设置，19CLI，开IRQ整帧，1024/1080四场景延迟',
        runtime='默认双盘仍HOST开发版本；G2原生产物和各独立测试盘另存；未发布完整M8原生正式盘',
        limitations='2.74倍仅该整数负载的本机TCG中位结果，不代表所有游戏/真实硬件或全GUI60Hz；全组件/压缩图片/写实真彩游戏/实时真彩光追/最终包继续',
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
