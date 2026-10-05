#!/usr/bin/env python3
"""mio：Studio独立候选的真实G2全矩阵/竞态/成本阶段封存。

候选与正式HOST默认盘明确区分。只有所有报告和当前输入逐项
相符才封存，完整包重读CRC及SHA；缺项不生成、不覆盖旧成功包。
其后正式发布/其它组件另走回归，不能把此候选包称整个M8完成。
"""
import hashlib,json,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
STAGE=ROOT/'build/m8-studio-draft';DEST=ROOT/'build/M8-studio-evidence.zip'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(path):return json.loads(path.read_text(encoding='utf-8'))


def inputs(report):
    assert report['status']=='PASS'
    for name,digest in report['inputs_sha256'].items():assert sha(ROOT/name)==digest,name
    if 'overflow' in report:assert not any(report['overflow'].values())


def native(directory,artifacts):
    for name,proof in artifacts.items():
        assert sha(directory/(name+'-native.scx'))==proof['scx_sha256']
        assert sha(directory/(name+'-native.map'))==proof['map_sha256']


def main():
    assert not DEST.exists(),'成功阶段包不可覆盖'
    core=read(STAGE/'results.json');matrix=read(STAGE/'matrix/results.json');cost=read(STAGE/'cost/results.json')
    pid=read(STAGE/'pid-reuse/results.json');queue=read(STAGE/'action-queue/results.json');revision=read(STAGE/'revision/results.json')
    observer=read(STAGE/'layout-observer/results.json')
    for report in (core,matrix,cost,pid,queue,revision,observer):inputs(report)
    for name,report in (('verify_studio.py',core),('verify_studio_cost.py',cost),('verify_studio_pid.py',pid),
                        ('verify_studio_action_queue.py',queue),('verify_studio_revision.py',revision)):
        assert sha(ROOT/'build/m8-next'/name)==report['verifier_sha256'],name
    native(STAGE,core['native']);native(STAGE/'pid-reuse',pid['native'])
    native(STAGE/'cost',{name.lower():proof for name,proof in cost['native'].items()})
    for report in (queue,revision):
        assert report['native_sha256']==core['native']['ide']['scx_sha256']
        assert report['native_map_sha256']==core['native']['ide']['map_sha256']
    for name,key in (('driver.c','source_sha256'),('driver-native.scx','scx_sha256'),('driver-native.map','map_sha256')):
        assert sha(STAGE/'revision'/name)==revision['driver'][key]
    assert queue['observation']['ui_action']==1 and queue['observation']['ui_modal']==0
    assert queue['observation']['window_queue_first']==104
    assert observer['verifier_sha256']==sha(ROOT/'build/m8-next/observe_studio_layout.py')
    assert observer['full_committed_layout']['editor_rows']>0 and observer['observation']['rows']==0
    assert observer['observation']['constructed_y']<observer['actual_down_button_top']
    assert len(revision['observations'])==2 and all(row['dirty']==0 and row['document_revision']!=row['build_revision'] for row in revision['observations'])
    expected={(w,h,s,'AURORA','std') for w,h in ((640,480),(800,600),(1024,768),(1280,720),(1920,1080)) for s in (100,150,200)}
    expected|={(640,480,200,'CLASSIC','std'),(1024,768,150,'CLASSIC','std'),(1920,1080,200,'CLASSIC','std'),(320,200,100,'AURORA','cirrus')}
    assert len(matrix['cases'])==19 and {(r['width'],r['height'],r['scale'],r['theme'],r['vga']) for r in matrix['cases']}==expected
    for case in matrix['cases']:
        inputs(case);assert case['verifier_sha256']==sha(ROOT/'build/m8-next/verify_studio_matrix.py')
        assert case['native_sha256']==core['native']['ide']['scx_sha256'] and case['native_map_sha256']==core['native']['ide']['map_sha256']
        assert case['inputs_sha256']==matrix['inputs_sha256']
    assert sum(len(row['checks']) for row in matrix['cases'])==222
    assert len(cost['samples'])==12 and cost['new_source_sha256']==sha(ROOT/'build/m8-next/ide.c')
    assert {(row['version'],row['fixture'],row['round']) for row in cost['samples']}=={(version,fixture,r) for version in ('OLD','NEW') for fixture in ('ROWS.C','LONG.C') for r in (1,2,3)}
    assert all(row['pit_ticks']>=200 and row['source_bytes']==65000 and row['loops']>0 for row in cost['samples'])
    previous=ROOT/'build/M8-lens-evidence.zip';assert sha(previous)=='53ffe88731b80610615fa4fec14df1a138c0daded61ce7b6d40aae68db575970'
    # 新版Studio仍是独立候选；核/API/NUI/其余正式应用与Lens封存
    # 精确相同。这个绑定证明旧的核级证据没有被新草稿偷偷改掉，
    # 不冒充候选已经装入默认HOST盘或后续新调试器已经原生通过。
    with zipfile.ZipFile(previous) as archive:
        for name in ('build/sandcore.img','build/kernel.elf','build/kernel.sym','build/sanddata.img','kernel/task.h','kernel/wm.c',
                     'user/SCAPI.H','user/NUI.inc','user/IMAGE.inc','kernel/font16.txt','user/ide.c','user/debugger.c','user/lens.c','user/files.c'):
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
    for path in STAGE.rglob('*'):
        if not path.is_file() or path.suffix not in ('.json','.scx','.map','.png','.img','.c','.H','.inc'):continue
        assert not any(word in path.name.lower() for word in ('failure','timeout','progress')),'失败历史不得混成功包'
        members.append((path,'sandcore/'+path.relative_to(ROOT).as_posix()))
    pending=[]
    for path in (ROOT/'build/m8-next').iterdir():
        if not path.is_file():continue
        if path.name in ('ide.c','studio-syntax.log','observe_studio_layout.py') or path.name.startswith(('verify_studio','run_studio')) and path.suffix=='.py':
            members.append((path,'sandcore/'+path.relative_to(ROOT).as_posix()))
        elif path.name in ('debugger.c','debugger-syntax.log','observe_debugger_range.py','monitor.c','monitor-syntax.log') or path.name.startswith(('verify_debugger','run_debugger','verify_monitor','run_monitor')) and path.suffix=='.py':
            members.append((path,'sandcore/'+path.relative_to(ROOT).as_posix()));pending.append(dict(path=path.relative_to(ROOT).as_posix(),sha256=sha(path)))
    hashes={name:sha(path) for path,name in members};assert len(hashes)==len(members)
    manifest=dict(author='mio',status='PASS',scope='M8 Studio独立G2候选全布局/文档/编译提交资格/成本阶段；未发布、整个M8未完成',
        core=core,matrix=matrix,pid_reuse=pid,action_queue=queue,revision=revision,cost=cost,layout_observer=observer,previous_stage=dict(archive=previous.name,sha256=sha(previous)),
        runtime='根user/ide.c及默认双盘仍旧正式HOST开发实现；本批候选build/m8-next/ide.c与真实f230d4be SCX/map/全部测试副本另存。正式预装激活和当前Files联动须另测。',
        next_component=dict(status='REVIEW_DRAFT_ONLY',files=pending,limits='调试器和Monitor独立草稿供审阅，其原生进行中或待启动证据不混入Studio阶段PASS'),
        limits='原事务版本专测使用普通G2驱动快照/实际生成后YIELD延迟返回，并非编译器运行时长；成本只代表真实65000B完整draw+FRAME32负载，不代表全GUI输入或固定FPS。最新三代编译器/游戏/光追/其它组件/完整M8继续。',sha256=hashes)
    with zipfile.ZipFile(DEST,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path,name in members:archive.write(path,name)
        archive.writestr('stage-manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    with zipfile.ZipFile(DEST) as archive:
        assert archive.testzip() is None
        for name,digest in hashes.items():assert hashlib.sha256(archive.read(name)).hexdigest()==digest,name
    digest=sha(DEST);DEST.with_suffix('.sha256').write_text(digest+'  '+DEST.name+'\n',encoding='ascii')
    print(f'{DEST}: {len(members)} files, {DEST.stat().st_size} bytes, SHA256 {digest}',flush=True)


if __name__=='__main__':main()
