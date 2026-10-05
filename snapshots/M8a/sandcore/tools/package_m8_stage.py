#!/usr/bin/env python3
"""mio：冻结已验证的图像/主题阶段，避免后续Shell开发覆盖成功证据。

此包明确是阶段证据，不等于整个M8验收。只收编号成功截图，排除
failure/progress与测试运行临时盘；源码和双盘一起冻结，原M7包不动。
"""
import hashlib
import json
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
DEST=ROOT/'build/M8-images-theme-evidence.zip'

def main():
    assert not DEST.exists(), '已有阶段包不得静默覆盖'
    reports={}
    for phase in ('m8-images','m8-theme','m8-truecolor'):
        report=json.loads((ROOT/'build'/phase/'results.json').read_text(encoding='utf-8'))
        if isinstance(report,list):
            assert len(report)==9 and all(isinstance(row,str) for row in report),phase
            assert json.loads((ROOT/'build'/phase/'integrity.json').read_text(encoding='utf-8'))['status']=='PASS'
        else:assert report['status']=='PASS', phase
        reports[phase]=report
    image_report=reports['m8-images']
    for path,digest in image_report['sha256'].items():
        if path.startswith(('kernel/','user/','build/')):
            assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==digest,path
    files=[]
    for path in ROOT.rglob('*'):
        if not path.is_file():continue
        relative=path.relative_to(ROOT)
        if relative.parts[0] in ('build','legacy') or '__pycache__' in relative.parts:continue
        files.append((path,'sandcore/'+relative.as_posix()))
    for name in ('AGENTS.md','HANDOFF.md','M8_GOAL.md'):
        files.append((ROOT.parent/name,name))
    for name in ('sandcore.img','sanddata.img','kernel.bin','kernel.elf','kernel.sym'):
        files.append((ROOT/'build'/name,'sandcore/build/'+name))
    for phase in reports:
        for path in (ROOT/'build'/phase).iterdir():
            if path.suffix in ('.json','.scx') or (path.suffix=='.png' and path.name[0].isdigit()):
                files.append((path,'sandcore/build/'+phase+'/'+path.name))
    files.append((ROOT/'legacy/M7/provenance.json','sandcore/legacy/M7/provenance.json'))
    hashes={name:hashlib.sha256(path.read_bytes()).hexdigest() for path,name in files}
    manifest=dict(author='mio',scope='M8图像/主题阶段，完整M8未完成',status='PASS',
                  checks='SCB2/SCX三类图标/原生SCCC图标产物/Lens透明与大图/主题持久化/RGB底座/LEGACY摘要及Calc',
                  limitations='压缩格式、完整原生发布、Shell/全组件/游戏/光追/显示矩阵继续；legacy完整源来自不可变M7包',sha256=hashes)
    with zipfile.ZipFile(DEST,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path,name in files:archive.write(path,name)
        archive.writestr('stage-manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2))
    digest=hashlib.sha256(DEST.read_bytes()).hexdigest()
    DEST.with_suffix('.sha256').write_text(digest+'  '+DEST.name+'\n',encoding='ascii')
    print(f'{DEST}: {len(files)} files, SHA256 {digest}')

if __name__=='__main__':main()
