#!/usr/bin/env python3
"""mio：冻结设置阶段及相同内核的大帧参考，保护验证与源码对应。

必须在继续改内核前运行，逐项核对验证输入；已有ZIP不得覆盖。
开发默认盘为宿主启动产物，真实G2原生SCX和同盘冷启动测试副本
独立保存，不能给整个M8贴完成标签，也不触碰已验收M7包。
"""
import hashlib,json,re,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
DEST=ROOT/'build/M8-settings-evidence.zip'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    assert not DEST.exists(),'阶段包已存在，不得静默覆盖'
    report=json.loads((ROOT/'build/m8-settings/results.json').read_text(encoding='utf-8'))
    assert report['status']=='PASS'
    for name,digest in report['inputs_sha256'].items():assert sha(ROOT/name)==digest,name
    for name in ('integrity.json','api-compat.json'):
        assert json.loads((ROOT/'build/m8-settings'/name).read_text(encoding='utf-8'))['status']=='PASS'
    assert sha(ROOT/'build/m8-settings/settings-native.scx')==report['native_sha256']
    baseline=json.loads((ROOT/'build/m8-frame-irq/baseline/results.json').read_text(encoding='utf-8'))
    assert baseline['status']=='BASELINE'
    for name,digest in baseline['inputs_sha256'].items():assert sha(ROOT/name)==digest,name
    files=[]
    for path in ROOT.rglob('*'):
        if not path.is_file():continue
        relative=path.relative_to(ROOT)
        if relative.parts[0] in ('build','legacy') or '__pycache__' in relative.parts:continue
        files.append((path,'sandcore/'+relative.as_posix()))
    for name in ('AGENTS.md','HANDOFF.md','M8_GOAL.md'):files.append((ROOT.parent/name,name))
    for name in ('sandcore.img','sanddata.img','boot.bin','kernel.bin','kernel.elf','kernel.sym'):
        files.append((ROOT/'build'/name,'sandcore/build/'+name))
    for directory in ('m8-settings','m8-frame-irq/baseline'):
        for path in (ROOT/'build'/directory).iterdir():
            if path.suffix in ('.json','.scx','.map') or (path.suffix=='.png' and (re.match(r'^\d',path.name) or path.name.startswith('command-'))):
                files.append((path,'sandcore/build/'+directory+'/'+path.name))
    # 保存冷启动实际继续使用的测试盘。它含tester等测试值，与上面
    # 保留个人配置的开发默认盘分别标识，不能互相冒充或覆盖。
    files.append((ROOT/'build/m8-settings/sanddata-std-128-settings.img','sandcore/build/m8-settings/sanddata-std-128-settings.img'))
    files.append((ROOT/'legacy/M7/provenance.json','sandcore/legacy/M7/provenance.json'))
    hashes={name:sha(path) for path,name in files}
    manifest=dict(author='mio',status='PASS',scope='M8六页Settings阶段，完整M8未完成',
        runtime='build/sanddata.img为宿主开发盘；m8-settings/sanddata-std-128-settings.img为真实G2生成Settings的冷启动测试盘',
        checks=report['checks'],native_sha256=report['native_sha256'],
        baseline='同一236扇区内核的大帧输入参考，仅BASELINE记录，不宣称已优化或发生过漏键',
        limitations='五档×三档/压缩图片/全组件/写实真彩游戏/实时真彩光追/完整原生发布和最终M8包继续；旧LEGACY源码完整归档在不可变M7包',sha256=hashes)
    with zipfile.ZipFile(DEST,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path,name in files:archive.write(path,name)
        archive.writestr('stage-manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    # 写完重开、CRC和全部SHA逐项检查，不能把“zip函数没报错”称验包。
    with zipfile.ZipFile(DEST) as archive:
        assert archive.testzip() is None
        for name,digest in hashes.items():assert hashlib.sha256(archive.read(name)).hexdigest()==digest,name
    digest=sha(DEST);DEST.with_suffix('.sha256').write_text(digest+'  '+DEST.name+'\n',encoding='ascii')
    print(f'{DEST}: {len(files)} files, SHA256 {digest}')

if __name__=='__main__':main()
