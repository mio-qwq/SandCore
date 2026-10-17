"""保存真实退出/注销/空输入登录，不将冷锁屏保留状态的原FAIL改写为PASS。"""
from pathlib import Path
import gzip,hashlib,json,re,shutil,struct
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'evidence/m10a2-session-return'
SRC=ROOT/'build/m10a2-p6-gui-run-13'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def main():
    r=json.loads((SRC/'p6-gui.json').read_bytes())
    assert r['status']=='FAIL_OR_INTERRUPTED' and r['current_inputs_unchanged'] and r['source_disk_unchanged']
    actions=r['p6_gui_runtime']['actions'];cases={c['name']:c for c in r['cases']}
    assert all(v['status']=='EXECUTED_EFFECT_REQUIRES_EVIDENCE' for k,v in actions.items() if k!='gui13-final-owner')
    def sessions(name):
        c=cases['p6-cli-'+name];assert c['status']=='PASS'
        return {int(m[0]):dict(uid=int(m[1]),kind=m[2],active=m[3]=='yes',tasks=int(m[4])) for m in re.findall(r'^([0-9]+)\s+(-?[0-9]+)\s+-?[0-9]+\s+(SYSTEM|LOCKED|user)\s+(yes|no)\s+([0-9]+)',c['output'],re.M)}
    leave=sessions('gui13-exit-actual-sessions');first=sessions('gui13-first-login-sessions')
    logged=sessions('gui13-after-real-logout-sessions');last=sessions('gui13-relogin-actual-sessions')
    assert leave[3]['active'] and leave[4]['tasks']==1 and not leave[4]['active']
    assert first[5]['active'] and first[5]['uid']==1000 and first[3]['tasks']==0
    assert 5 not in logged and logged[3]['active'] and logged[4]['tasks']==1
    assert last[6]['active'] and last[6]['uid']==1000 and 5 not in last and last[3]['tasks']==0
    c=cases['p6-gui-single-final-owner-account'];b=r['baseline']['coherent_snapshot'];f=r['coherent_owner']['final_observation']['snapshot']
    changed={k:v for k,v in c['actual_pool_deltas'].items() if v['objects'] or v['pages']}
    assert len(changed)==1 and next(iter(changed)).startswith('view_pool@') and next(iter(changed.values()))['objects']==1 and next(iter(changed.values()))['pages']==0
    assert c['physical_delta']==c['unexplained_delta']==8 and c['actual_font_page_delta']==c['actual_desktop_page_delta']==c['actual_user_clock_glyph_cache_pages']==0
    owners=[]
    assert [(x['pid'],x['generation'],x['pd']) for x in b['owners']]==[(x['pid'],x['generation'],x['pd']) for x in f['owners']]
    for old,new in zip(b['owners'],f['owners']):
        if old['pid']==0:continue
        for key in ('private_frames','shared_frames','page_tables'):assert old['pages'][key]==new['pages'][key]
        assert old['kernel_stack_pages']==new['kernel_stack_pages'] and old['registered_module_pages']==new['registered_module_pages']
        owners.append(dict(pid=new['pid'],generation=new['generation'],private_pages=new['pages']['private_pages'],page_tables=new['pages']['page_table_pages'],frames_equal=True))
    obj=ROOT/'build/m10a2-p6-final-12/core/desktop.o';data=obj.read_bytes();offset=struct.unpack_from('<I',data,32)[0];step,count=struct.unpack_from('<HH',data,46)
    sections=[struct.unpack_from('<10I',data,offset+i*step) for i in range(count)];sizes=[]
    for s in sections:
        if s[1]!=2:continue
        t=sections[s[6]];strings=data[t[4]:t[4]+t[5]]
        for pos in range(s[4],s[4]+s[5],s[9]):
            name,_,size,_,_,_=struct.unpack_from('<IIIBBH',data,pos)
            if strings[name:strings.find(b'\0',name)]==b'emergency':sizes.append(size)
    assert sizes==[32660] and (sizes[0]+4095)//4096==c['physical_delta']
    OUT.mkdir(exist_ok=False);files={}
    def copy(p,name,compressed=False):
        q=OUT/name;q.parent.mkdir(parents=True,exist_ok=True)
        if compressed:q.write_bytes(gzip.compress(p.read_bytes(),mtime=0))
        else:shutil.copyfile(p,q)
        files[name]=dict(source=p.relative_to(ROOT).as_posix(),sha256=sha(p),bytes=p.stat().st_size,archive_sha256=sha(q))
    copy(SRC/'p6-gui.json','gui13.json.gz',True)
    for name in ('session.json','heartbeat.jsonl'):copy(SRC/name,name)
    for name in ('exit-confirm','login-form-ready','root-empty-result','first-desktop-settled','logout-confirm','after-logout-form','relogin-result'):
        copy(SRC/('gui13-'+name+'.png'),'screenshots/'+name+'.png')
    for case in cases.values():
        if case.get('stdout_path') and ('sessions' in case['name'] or 'restore-original' in case['name']):copy(Path(case['stdout_path']),'stdout/'+case['name']+'.bin')
    for name in ('build/m10a2-p6-target-refresh-11/p6-generation.json','build/m10a2-p6-refresh-vm-11/refresh-vm.json',
        'build/m10a2-main-session-return-01/p6-unified-host-21.json','build/m10a2-p6-final-12/finalization.json'):
        copy(ROOT/name,'reports/'+Path(name).name+'.gz',True)
    for name in ('kernel/auth.c','kernel/desktop.c','kernel/session.c','kernel/wm.c','user/scshell/desktop/desktop_actions.inc'):
        copy(ROOT/name,'source/'+name)
    copy(obj,'desktop.o')
    review=dict(status='ACTUAL_EXIT_LOGOUT_LOCAL_SIGNIN_VERIFIED_RESOURCE_BOUNDARY_DECLARED',full_M10a2=False,
        raw_gui_status=r['status'],driver_exit_code=1,driver_stop='Root CtrlC after original finish predicate rejected first retained LOCKED view',qemu_pid=r['qemu_pid'],qemu_closed=True,
        source_disk_unchanged=True,current_inputs_unchanged=True,ordinary_user_uid=1000,
        gui=dict(exit_returns_fixed_login=True,previous_session_preserved=True,root_empty_password_denied=True,
            default_mio_empty_password_accepted=True,real_logout_removes_session5=True,relogin_creates_session6=True,login_process_exits=True),
        actual_sessions=dict(after_leave=leave,first_signin=first,after_logout=logged,relogin=last),
        resource=dict(raw_owner_verdict='FAIL',physical_before=c['before_physical_pages'],physical_after=c['after_physical_pages'],raw_physical_delta=8,raw_unexplained_delta=8,
            new_view_objects=1,view_pool_pages_delta=0,actual_desktop_state_bytes=sizes[0],compiled_state_pages=8,
            existing_live_owner_frames_unchanged=owners,other_pool_deltas_zero=True,font_and_active_desktop_pages_unchanged=True,
            interpretation='Matches first persistent LOCKED SID3 view and desktop_state allocation retained by session.c; source and compiled-size attribution, not captured frame-address attribution',
            retained_frame_addresses_captured=False,original_fail_not_reclassified=True,no_blanket_page_allowance=True),files=files)
    save(OUT/'review.json',review)
    (OUT/'README.md').write_text('''# 退出与本机重新登录的实际修复

Windows双盘无头QEMU、WHPX、真实PS/2输入。HOST21/native11/final12。

“退出桌面”真实确认后固定LOGIN2出现，原SID4仍存在且隐藏；root空输入显示拒绝。mio空输入进入SID5/UID1000普通桌面。真实选择注销后SID5消失，LOGIN2重新出现；再次mio空输入进入SID6，原SID4仍保留。两个登录程序均正常结束。新授权只对固定发行LOGIN2、启用且未设密码的默认mio成立；root、普通su/网络不获例外；坏账户库与已设密码的策略保留。

收尾恢复原SID1并移除全部临时普通SID。原始资源检查FAIL：13950→13958，view_pool对象+1、池页0，其他池/字体/当前桌面增量0。原进程PID/代数、全部用户物理帧和页表逐项相同；没有残留LOGIN2或临时程序。实际desktop.o的desktop_state_t全局emergency符号大小32660B，按现有prepare算法为8页；源码与实际会话流程表明首次保留LOCKED SID3的视图/状态，与观察增量相符。此为编译尺寸及生命周期归因，未直接抓取该保留状态的物理帧地址；原FAIL/未归因8仍原样归档，不改成零差PASS、不设置通用8页宽限。

Root在失败谓词后主动CtrlC结束自己的控制器，driver退出1、QEMU13536关闭，输入和原盘守恒。功能实测与此资源边界分列，不能把该轮整体称为自动化PASS。未继续扩展测试矩阵。普通原生刷新11自身的整体资源账为零差PASS。
''',encoding='utf-8')
    print(json.dumps(dict(status=review['status'],review_sha256=sha(OUT/'review.json'),files=len(files))))
if __name__=='__main__':main()
