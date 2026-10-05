#!/usr/bin/env python3
"""mio：冻结Canvas/NUI真彩、十九布局组合及共用Settings回归。

当前代码继续演进后，旧截图只证明当时那份代码。打包前逐项核对
输入、G2产物、十九组合/同一SCX、设置回归及静态合同；失败历史
单独保留，不能混入成功验收图。默认双盘仍为HOST开发盘，真正
G2产物与各副本盘明确分开，不宣称完整M8正式发布。
"""
import hashlib,json,zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-canvas'
DEST=ROOT/'build/M8-canvas-evidence.zip'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))


def check_inputs(report):
    assert report['status']=='PASS'
    for path,digest in report.get('inputs_sha256',{}).items():
        assert sha(ROOT/path)==digest,'报告输入不是当前文件：'+path


def main():
    assert not DEST.exists(),'阶段包不可覆盖'
    core=read(STAGE/'results.json');matrix=read(STAGE/'matrix/results.json')
    settings=read(STAGE/'settings-regression/results.json')
    action=read(STAGE/'action-queue/results.json');cost=read(STAGE/'row-cost/results.json')
    for report in (core,matrix,settings,action,cost):check_inputs(report)
    assert action['native_sha256']==core['native']['canvas']['scx_sha256']
    assert action['verifier_sha256']==sha(ROOT/'tools/verify_canvas_action_queue.py')
    assert cost['verifier_sha256']==sha(ROOT/'tools/verify_nui_row_cost.py')
    assert cost['current_nui_sha256']==sha(ROOT/'user/NUI.inc')
    assert cost['source_sha256']==sha(STAGE/'row-cost/source.c')
    assert cost['old_nui_sha256']==sha(STAGE/'row-cost/old-NUI.inc')
    for name,proof in cost['native'].items():
        assert sha(STAGE/'row-cost'/(name.lower()+'-native.scx'))==proof['scx_sha256']
        assert sha(STAGE/'row-cost'/(name.lower()+'-native.map'))==proof['map_sha256']
    assert len(matrix['cases'])==19
    for name,proof in core['native'].items():
        assert sha(STAGE/(name+'-native.scx'))==proof['scx_sha256']
        assert sha(STAGE/(name+'-native.map'))==proof['map_sha256']
    assert sha(STAGE/'settings-regression/settings-native.scx')==settings['native_sha256']
    matrix_verifier=sha(ROOT/'tools/verify_canvas_matrix.py')
    previous=STAGE/'matrix-verifier-before-frame-readiness.py'
    allowed_verifiers={matrix_verifier}
    if previous.exists():
        assert sha(previous)==matrix['earlier_verifier_sha256']
        allowed_verifiers.add(sha(previous))
    for case in matrix['cases']:
        check_inputs(case)
        assert case['native_sha256']==core['native']['canvas']['scx_sha256']
        assert case['native_map_sha256']==core['native']['canvas']['map_sha256']
        assert case['verifier_sha256'] in allowed_verifiers,'验证器字节不对应'
        assert '03b-color-rejected-clear-cancel-undo' in case['checks']
    integrity=read(STAGE/'integrity.json');compat=read(STAGE/'api-compat.json')
    assert integrity['status']==compat['status']=='PASS'
    for path,digest in integrity['sha256'].items():assert sha(ROOT/path)==digest,path
    assert sha(ROOT/'user/SCAPI.H')==compat['current_header_sha256']
    assert sha(ROOT/'kernel/font16.txt')==integrity['font_sha256']
    # 内核完全保持上一局部合成核；这里只引用已冻结核/ABI回归，
    # 不拿旧NUI设置图冒充新共享代码的测试。新设置产物另行验证。
    prior=ROOT/'build/M8-damage-evidence.zip'
    assert sha(prior)=='15d34f56a440bef7905eeaa673d20b96a5c9048f4fb6556d1a91d70e4a9ff390'
    assert cost['old_archive_sha256']==sha(prior)
    with zipfile.ZipFile(prior) as archive:
        assert hashlib.sha256(archive.read('sandcore/user/NUI.inc')).hexdigest()==cost['old_nui_sha256']
        for name in ('sandcore.img','kernel.bin','kernel.elf','kernel.sym'):
            assert hashlib.sha256(archive.read('sandcore/build/'+name)).hexdigest()==sha(ROOT/'build'/name),name
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
        if 'history' in path.relative_to(STAGE).parts:continue
        if any(word in path.name.lower() for word in ('failure','timeout','progress')):continue
        files.append((path,'sandcore/'+path.relative_to(ROOT).as_posix()))
    files.append((ROOT/'legacy/M7/provenance.json','sandcore/legacy/M7/provenance.json'))
    if previous.exists():files.append((previous,'sandcore/'+previous.relative_to(ROOT).as_posix()))
    hashes={name:sha(path) for path,name in files};assert len(files)==len(hashes)
    manifest=dict(author='mio',status='PASS',scope='M8 Canvas/NUI阶段，整个M8未完成',
        core=core,matrix=matrix,settings=settings,action_queue=action,row_cost=cost,
        reference_kernel=dict(archive=prior.name,sha256=sha(prior)),
        checks='真实G2 Canvas/Lens/路径；作品/Undo字节与候选回滚；SCB2保存再开；中文路径；十九显示组合/鼠标窗口/真实帧/完整余数行列；点击与排队首字专测；同G2/核/实际窗口逐行写入成本对照；新G2 Settings六页/持久化/VGA回归；API/字体/无外部库静态合同',
        runtime='默认双盘HOST开发产物；真实G2 SCX/.map及各独立测试盘另存，尚非完整M8原生正式发布盘',
        limits='Canvas十九组合不代替其它工具全布局、PNG/JPG/WebP、写实真彩游戏/实时光追或全GUI60Hz；旧失败/中断历史不计入成功',
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
