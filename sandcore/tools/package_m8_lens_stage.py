#!/usr/bin/env python3
"""mio：完整原图Lens阶段封存，缺少任何实际PASS均不得生成包。

同一源/核/默认盘绑定核心、十九组合、真实OOM/首字及Canvas/Files
联动；真实G2产物和HOST开发盘明确区分。逐项重读CRC/SHA，既有
成功ZIP不可覆盖，失败历史留在工作区，不混入成功证据。
"""
import hashlib,json,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-lens';DEST=ROOT/'build/M8-lens-evidence.zip'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))


def inputs(report):
    assert report['status']=='PASS'
    for name,digest in report['inputs_sha256'].items():assert sha(ROOT/name)==digest,name


def main():
    assert not DEST.exists(),'阶段包不可覆盖'
    core=read(STAGE/'results.json');matrix=read(STAGE/'matrix/results.json');followups=read(STAGE/'followups.json')
    integrity=read(STAGE/'integrity.json');compat=read(STAGE/'api-compat.json')
    for report in (core,matrix,followups):inputs(report)
    assert integrity['status']==compat['status']=='PASS'
    for name,digest in integrity['sha256'].items():assert sha(ROOT/name)==digest,name
    assert sha(ROOT/'kernel/font16.txt')==integrity['font_sha256']
    assert sha(ROOT/'user/SCAPI.H')==compat['current_header_sha256']
    assert sha(STAGE/'lens-native.scx')==core['native_sha256'] and sha(STAGE/'lens-native.map')==core['native_map_sha256']
    assert core['verifier_sha256']==sha(ROOT/'tools/verify_lens.py')
    assert followups['verifier_sha256']==sha(ROOT/'tools/verify_lens_followups.py')
    assert followups['inputs_sha256']==core['inputs_sha256']
    assert followups['cases']==['memory','action-queue','canvas-regression','files-regression']
    expected={(w,h,s,'AURORA','std') for w,h in ((640,480),(800,600),(1024,768),(1280,720),(1920,1080)) for s in (100,150,200)}
    expected|={(640,480,200,'CLASSIC','std'),(1024,768,150,'CLASSIC','std'),(1920,1080,200,'CLASSIC','std'),(320,200,100,'AURORA','cirrus')}
    assert len(matrix['cases'])==19 and {(r['width'],r['height'],r['scale'],r['theme'],r['vga']) for r in matrix['cases']}==expected
    for case in matrix['cases']:
        inputs(case);assert case['verifier_sha256']==sha(ROOT/'tools/verify_lens_matrix.py')
        assert case['native_sha256']==core['native_sha256'] and case['native_map_sha256']==core['native_map_sha256']
        assert case['inputs_sha256']==matrix['inputs_sha256'] and not any(case['overflow'].values())
        assert case['one_pixel_pan']['physical_delta']==[-1,-1]
        if case['scale']==150 and case['vga']=='std':assert case['one_pixel_pan']['logical_before']==case['one_pixel_pan']['logical_after']
    assert sum(len(case['checks']) for case in matrix['cases'])==203
    memory=read(STAGE/'memory/results.json');queue=read(STAGE/'action-queue/results.json')
    canvas=read(STAGE/'canvas-regression/results.json');files=read(STAGE/'files-regression/results.json')
    for report in (memory,queue,canvas,files):inputs(report);assert not any(report['overflow'].values())
    for name,report in (('memory',memory),('action-queue',queue)):
        assert report['inputs_sha256']==core['inputs_sha256']
        assert report['native_sha256']==core['native_sha256'] and report['native_map_sha256']==core['native_map_sha256']
        assert report['verifier_sha256']==sha(ROOT/('tools/verify_lens_'+('memory' if name=='memory' else 'action_queue')+'.py'))
    assert memory['holders'][-1][3]<8*1024*1024
    for name,digest in (('reserve.c',memory['helper_source_sha256']),('reserve-native.scx',memory['helper_native_sha256']),('reserve-native.map',memory['helper_map_sha256'])):
        assert sha(STAGE/'memory'/name)==digest,name
    assert queue['observation']['ui_action']==1 and queue['observation']['ui_modal']==0 and queue['observation']['window_queue_first']==104
    for name,report in (('canvas-regression',canvas),('files-regression',files)):
        for app,proof in report['native'].items():
            assert sha(STAGE/name/(app+'-native.scx'))==proof['scx_sha256']
            assert sha(STAGE/name/(app+'-native.map'))==proof['map_sha256']
        assert report['native']['lens']['scx_sha256']==core['native_sha256']
        assert report['native']['lens']['map_sha256']==core['native_map_sha256']
    prior=ROOT/'build/M8-files-evidence.zip'
    assert sha(prior)=='0db50bbbd44ca4e642560b563523eaf7f1049cbcd172558570cf535a5794fc9d'
    with zipfile.ZipFile(prior) as archive:
        # 本批只修改Lens私有实现及验收器；Kernel/NUI/API与上一
        # 四套回归精确输入相同。逐项绑定，不说所有旧证据都当前。
        for name in ('build/sandcore.img','build/kernel.elf','build/kernel.sym','kernel/wm.c','kernel/wm_native.inc','kernel/task.h',
                     'user/SCAPI.H','user/NUI.inc','user/IMAGE.inc','kernel/font16.txt','user/canvas.c','user/files.c',
                     'user/shell.c','user/cli.c','user/settings.c','user/monitor.c'):
            assert hashlib.sha256(archive.read('sandcore/'+name)).hexdigest()==sha(ROOT/name),name
    members=[]
    for path in ROOT.rglob('*'):
        if not path.is_file():continue
        relative=path.relative_to(ROOT)
        if relative.parts[0] in ('build','legacy') or '__pycache__' in relative.parts:continue
        members.append((path,'sandcore/'+relative.as_posix()))
    for name in ('AGENTS.md','HANDOFF.md','M8_GOAL.md'):members.append((ROOT.parent/name,name))
    for name in ('sandcore.img','sanddata.img','boot.bin','kernel.bin','kernel.elf','kernel.sym'):
        members.append((ROOT/'build'/name,'sandcore/build/'+name))
    # 文档已经登记下一组件草稿。它另有独立G2核心证据但尚未
    # 发布；随包提供审阅源码与该核心快照，不计Lens的原生PASS。
    pending=[]
    for name in ('ide.c','verify_studio.py','verify_studio_matrix.py','verify_studio_pid.py','verify_studio_action_queue.py','run_studio_after_lens.py','studio-syntax.log'):
        path=ROOT/'build/m8-next'/name
        if path.is_file():
            members.append((path,'sandcore/build/m8-next/'+name));pending.append(dict(path='sandcore/build/m8-next/'+name,sha256=sha(path)))
    draft=read(ROOT/'build/m8-studio-draft/results.json');inputs(draft)
    assert draft['verifier_sha256']==sha(ROOT/'build/m8-next/verify_studio.py')
    for app,proof in draft['native'].items():
        for suffix,key in (('.scx','scx_sha256'),('.map','map_sha256')):
            assert sha(ROOT/'build/m8-studio-draft'/(app+'-native'+suffix))==proof[key]
    # 只取已经通过的核心根目录，不把并行进行中的PID/矩阵子目录
    # 收进不可变包。各文件写完后已被核心报告绑定，尚无其他写者。
    for path in (ROOT/'build/m8-studio-draft').iterdir():
        if path.is_file() and path.suffix in ('.json','.scx','.map','.png','.img'):
            assert not any(word in path.name.lower() for word in ('failure','timeout','progress'))
            members.append((path,'sandcore/'+path.relative_to(ROOT).as_posix()))
    for path in STAGE.rglob('*'):
        if not path.is_file() or path.suffix not in ('.json','.scx','.map','.png','.img','.scb','.c','.inc','.H'):continue
        if any(word in path.name.lower() for word in ('failure','timeout','progress')):continue
        members.append((path,'sandcore/'+path.relative_to(ROOT).as_posix()))
    members.append((ROOT/'legacy/M7/provenance.json','sandcore/legacy/M7/provenance.json'))
    hashes={name:sha(path) for path,name in members};assert len(hashes)==len(members)
    manifest=dict(author='mio',status='PASS',scope='M8 Lens完整原图/过滤/物理倍率阶段，完整M8未完成',core=core,matrix=matrix,followups=followups,
        previous_stage=dict(archive=prior.name,sha256=sha(prior)),next_component_draft=dict(status='G2_CORE_PASS_UNPUBLISHED',files=pending,core=draft),
        runtime='HOST默认开发双盘；实际G2 Lens/占页辅助程序、Canvas/Files联动各自产物/符号和副本盘另存，非最终M8原生发布盘',
        checks='8294400B原图/整数alpha双线性完整参考/100%完整像素；十九组合203图/物理1px/窗口/整帧；实际候选OOM/首字；Canvas作品重开和Files实际分派；严格回收/旧API/用户字体',
        limits='压缩图片/壁纸、全部其它组件/最新编译器三代、现代写实游戏/连续实时光追/所有LEGACY和最终M8仍继续；不作固定FPS或全GUI丝滑结论，失败历史不计成功',sha256=hashes)
    with zipfile.ZipFile(DEST,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path,name in members:archive.write(path,name)
        archive.writestr('stage-manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    with zipfile.ZipFile(DEST) as archive:
        assert archive.testzip() is None
        for name,digest in hashes.items():assert hashlib.sha256(archive.read(name)).hexdigest()==digest,name
    digest=sha(DEST);DEST.with_suffix('.sha256').write_text(digest+'  '+DEST.name+'\n',encoding='ascii')
    print(f'{DEST}: {len(members)} files, SHA256 {digest}',flush=True)


if __name__=='__main__':main()
