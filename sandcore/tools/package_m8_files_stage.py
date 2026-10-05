#!/usr/bin/env python3
"""mio：冻结Files十九组合/真实512项及抓取偏移修复后的同核回归。

先核对所有输入/真实G2产物/验证器，再写不可覆盖的阶段ZIP。
默认双盘标明HOST开发产物；G2五应用及各副本盘另存。失败历史
留在工作区，不作为成功截图；下一批Lens草稿不冒充当前已发布代码。
"""
import hashlib,json,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-files';DEST=ROOT/'build/M8-files-evidence.zip'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))


def inputs(report):
    assert report['status']=='PASS'
    for name,digest in report.get('inputs_sha256',{}).items():assert sha(ROOT/name)==digest,name


def main():
    assert not DEST.exists(),'成功阶段包不可覆盖'
    core=read(STAGE/'results.json');matrix=read(STAGE/'matrix/results.json')
    capacity=read(STAGE/'capacity/results.json');regressions=read(STAGE/'regressions/regressions.json')
    integrity=read(STAGE/'integrity.json');compat=read(STAGE/'api-compat.json')
    for report in (core,matrix,capacity,regressions):inputs(report)
    assert integrity['status']==compat['status']=='PASS'
    for name,digest in integrity['sha256'].items():assert sha(ROOT/name)==digest,name
    assert sha(ROOT/'kernel/font16.txt')==integrity['font_sha256']
    assert sha(ROOT/'user/SCAPI.H')==compat['current_header_sha256']
    assert core['verifier_sha256']==sha(ROOT/'tools/verify_files.py')
    assert capacity['verifier_sha256']==sha(ROOT/'tools/verify_files_capacity.py')
    for name,proof in core['native'].items():
        assert sha(STAGE/(name+'-native.scx'))==proof['scx_sha256']
        assert sha(STAGE/(name+'-native.map'))==proof['map_sha256']
    assert len(matrix['cases'])==19
    expected={(w,h,s,'AURORA','std') for w,h in ((640,480),(800,600),(1024,768),(1280,720),(1920,1080)) for s in (100,150,200)}
    expected|={(640,480,200,'CLASSIC','std'),(1024,768,150,'CLASSIC','std'),(1920,1080,200,'CLASSIC','std'),(320,200,100,'AURORA','cirrus')}
    assert {(r['width'],r['height'],r['scale'],r['theme'],r['vga']) for r in matrix['cases']}==expected
    for case in matrix['cases']:
        inputs(case);assert case['inputs_sha256']==core['inputs_sha256']
        assert case['verifier_sha256']==sha(ROOT/'tools/verify_files_matrix.py')
        assert case['native_sha256']==core['native']['files']['scx_sha256']
        assert case['native_map_sha256']==core['native']['files']['map_sha256']
        assert not any(case['overflow'].values())
        assert '03-context-inside-client' in case['checks'] and '10-reclaimed' in case['checks']
    assert sum(len(r['checks']) for r in matrix['cases'])==222
    assert capacity['inputs_sha256']==core['inputs_sha256']
    assert capacity['native_sha256']==core['native']['files']['scx_sha256']
    assert capacity['native_map_sha256']==core['native']['files']['map_sha256']
    assert sha(STAGE/'capacity/historical-shell-native.scx')==capacity['historical_shell_sha256']
    assert regressions['cases']==['rgb-regression','cpu-regression','settings-regression','userspace-regression']
    rgb=read(STAGE/'regressions/rgb-regression/results.json');assert isinstance(rgb,list) and len(rgb)==9
    cpu=read(STAGE/'regressions/cpu-regression/results.json');settings=read(STAGE/'regressions/settings-regression/results.json')
    shell=read(STAGE/'regressions/userspace-regression/results.json')
    for report in (cpu,settings,shell):inputs(report)
    for name,digest in cpu['sha256'].items():assert sha(STAGE/'regressions/cpu-regression'/name)==digest
    assert sha(STAGE/'regressions/settings-regression/settings-native.scx')==settings['native_sha256']
    for name,digest in shell['sha256'].items():
        if (ROOT/name).is_file():assert sha(ROOT/name)==digest,name
        elif name!='G2':assert sha(STAGE/'regressions/userspace-regression'/(name+'.scx'))==digest,name
    prior=ROOT/'build/M8-canvas-evidence.zip'
    assert sha(prior)=='a1750271f80c01631693c5ea54b6342bba2462bd56504fcff226555b4dae7d87'
    with zipfile.ZipFile(prior) as archive:
        for name in ('user/SCAPI.H','user/NUI.inc','kernel/font16.txt','kernel/task.h'):
            assert hashlib.sha256(archive.read('sandcore/'+name)).hexdigest()==sha(ROOT/name),name
        # 新核只改WM拖框；旧核心回归脚本仍是同一份，不能在封存时
        # 用改过的脚本代替本轮实际执行的验证规则。
        for name in ('verify_frame_regressions.py','verify_truecolor.py','verify_cpu_storage.py','verify_settings.py','verify_userspace.py'):
            assert hashlib.sha256(archive.read('sandcore/tools/'+name)).hexdigest()==sha(ROOT/'tools'/name),name
    files=[]
    for path in ROOT.rglob('*'):
        if not path.is_file():continue
        relative=path.relative_to(ROOT)
        if relative.parts[0] in ('build','legacy') or '__pycache__' in relative.parts:continue
        files.append((path,'sandcore/'+relative.as_posix()))
    for name in ('AGENTS.md','HANDOFF.md','M8_GOAL.md'):files.append((ROOT.parent/name,name))
    for name in ('sandcore.img','sanddata.img','boot.bin','kernel.bin','kernel.elf','kernel.sym'):
        files.append((ROOT/'build'/name,'sandcore/build/'+name))
    for path in STAGE.rglob('*'):
        if not path.is_file() or path.suffix not in ('.json','.scx','.map','.png','.img','.scb','.c','.inc','.H'):continue
        if any(word in path.name.lower() for word in ('failure','timeout','progress')):continue
        files.append((path,'sandcore/'+path.relative_to(ROOT).as_posix()))
    files.append((ROOT/'legacy/M7/provenance.json','sandcore/legacy/M7/provenance.json'))
    hashes={name:sha(path) for path,name in files};assert len(hashes)==len(files)
    manifest=dict(author='mio',status='PASS',scope='M8 Files/抓取偏移阶段，完整M8未完成',
        core=core,matrix=matrix,capacity=capacity,regressions=regressions,
        previous_stage=dict(archive=prior.name,sha256=sha(prior)),
        runtime='默认HOST开发双盘；实际G2五应用/Monitor/Settings/Shell/CLI/探针及独立测试盘另存，尚非最终M8原生发布盘',
        checks='实际G2核心分派/UTF-8身份/文件修改保护；十九组合/222图/完整帧/窗口/抓取偏移；512真实直接子项/末页/空槽再用；新核RGB/VGA/32MB、CPU新旧盘、六页Settings及19CLI回归；API/字体静态合同',
        limits='Files矩阵不代替其它组件全尺寸、压缩格式、写实真彩游戏/实时光追或全GUI丝滑；失败历史不计入成功，Lens草稿未激活',
        sha256=hashes)
    with zipfile.ZipFile(DEST,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path,name in files:archive.write(path,name)
        archive.writestr('stage-manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    with zipfile.ZipFile(DEST) as archive:
        assert archive.testzip() is None
        for name,digest in hashes.items():assert hashlib.sha256(archive.read(name)).hexdigest()==digest,name
    digest=sha(DEST);DEST.with_suffix('.sha256').write_text(digest+'  '+DEST.name+'\n',encoding='ascii')
    print(f'{DEST}: {len(files)} files, SHA256 {digest}',flush=True)


if __name__=='__main__':main()
