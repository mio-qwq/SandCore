#!/usr/bin/env python3
"""安装实际客体生成的库，验证三环三窗口过程与真实Windows键鼠。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import secrets
import struct
import time
import traceback

from scserial import QemuSession, sha
from verify_m9 import Guest, png_from_ppm
from m10_guest_boot import unique_symbols, wait_first_desktop, physical_word
from verify_m10a2_scl import prepare, stable_pages
from verify_m10a2_dynamic import SOURCE_NAMES
from scl_format import validate
from scd_format import validate_scx

ROOT=Path(__file__).resolve().parents[1]
HEADER_NAMES=('version','header_bytes','status','file_bytes','pid','pid_generation',
    'snapshot_count','event_count','event_capacity','event_size','info_size','info_count',
    'windowA','windowB','windowC','alive_mask','coverage_lo','paint_count','submit_calls',
    'submitted_bytes_lo','submitted_bytes_hi','error_count','first_code','first_domain',
    'first_raw','first_operation','heap_before','heap_after','flags','last_tick')

def decode(blob):
    if len(blob)<512 or blob[:8]!=b'SC32TWIN':raise ValueError('窗口日志头不完整')
    header=dict(zip(HEADER_NAMES,struct.unpack_from('<30I',blob,8)))
    for field in ('first_code','first_raw'):
        if header[field]&0x80000000:header[field]-=0x100000000
    if (header['version'],header['header_bytes'],header['event_size'],header['info_size'],
        header['info_count'],header['event_capacity'])!=(1,128,64,96,3,512):
        raise ValueError('窗口日志版本/尺寸不匹配')
    count=header['event_count']
    if count>512 or header['file_bytes']!=512+count*64 or len(blob)!=header['file_bytes']:
        raise ValueError('窗口日志长度不匹配')
    infos=[]
    for index in range(3):
        words=struct.unpack_from('<24I',blob,128+index*96)
        infos.append(dict(size=words[0],version=words[1],window=words[2],generation=words[3],
            owner_pid=words[4],owner_generation=words[5],session=words[6],
            x=words[7],y=words[8],width=words[9],height=words[10],
            client_width=words[11],client_height=words[12],format=words[13],state=words[14],
            geometry_epoch=words[15],visibility_epoch=words[16],input_epoch=words[17],
            theme_epoch=words[18],display_epoch=words[19]))
    events=[list(struct.unpack_from('<16I',blob,512+i*64)) for i in range(count)]
    return dict(header=header,windows=infos,event_counts=list(struct.unpack_from('<24I',blob,416)),events=events)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--build-report',type=Path,required=True)
    parser.add_argument('--foundation',type=Path,required=True)
    parser.add_argument('--native-library',type=Path,required=True)
    parser.add_argument('--native-root',type=Path,required=True)
    parser.add_argument('--native-probe',type=Path,required=True)
    parser.add_argument('--data',type=Path,default=ROOT/'build/m10a1/sanddata.img')
    args=parser.parse_args();build=ROOT/'build/m10a2-sc32-bootstrap'
    proof=json.loads(args.build_report.read_text(encoding='utf-8'))
    foundation=json.loads(args.foundation.read_text(encoding='utf-8'))
    if proof['exit_code'] or proof['source_files_modified_during_build']:
        raise ValueError('窗口构建失败或输入不稳定')
    for item in proof['outputs']:
        if sha(ROOT/item['path'])!=item['sha256']:raise ValueError('当前产物与构建记录不同')
    library=args.native_library.read_bytes();tables=validate(library)
    generated=next(x for x in foundation['cases'] if x['name']=='native-library-all-declared-exports')
    library_manifest=json.loads((ROOT/'user/sc32/SC32API.json').read_text(encoding='utf-8'))
    if sha(args.native_library)!=generated['sha256'] or {x['name'] for x in tables['exports']}!={x['name'] for x in library_manifest['exports']}:
        raise ValueError('库不是首轮客体实际生成的文件')
    # R3基础库源码未变，键盘锁定重复修复由新core构建单列。
    for name,digest in foundation['input_sha256'].items():
        if name.startswith(('user/sc32/','user/guiapi/')) or name in (
            'user/SC32API.H','user/SC32GUI.H','user/SCTYPES.H'):
            if sha(ROOT/name)!=digest:raise ValueError('原生库对应源码已经改变：'+name)
    root_tables=validate(args.native_root.read_bytes())
    root_manifest=json.loads((ROOT/'user/runtime/SCLROOT.json').read_text(encoding='utf-8'))
    if root_tables['imports'] or root_tables['dependencies'] or {x['name'] for x in root_tables['exports']}!={x['name'] for x in root_manifest['exports']}:
        raise ValueError('原生根不符合固定引导合同')
    validate_scx(args.native_probe.read_bytes())
    prefix='/TMP/TWIN-'+secrets.token_hex(3);package='/HOME/M10P';compiler=prefix+'/SCCC0.SCX'
    extra={'SYS/LIB/SCLDR.SCL':args.native_root.read_bytes(),
        'SYS/LIB/SC32API.SCL':library,compiler[1:]:(build/'fs/bin/sccc.scx').read_bytes(),
        package[1:]+'/SC32PROBE.SCX':args.native_probe.read_bytes(),
        package[1:]+'/SC32GUARD.SCL':(build/'SC32GUARD.SCL').read_bytes(),
        package[1:]+'/SC32TWIN.SCX':(build/'SC32TWIN.SCX').read_bytes()}
    sources=set(SOURCE_NAMES)|{'SC32API.H','SC32GUI.H'};hashes={}
    for folder in ('sc32','guiapi'):
        for path in (ROOT/'user'/folder).glob('*'):
            if path.is_file() and path.suffix.lower() in ('.c','.h','.json'):
                sources.add(path.relative_to(ROOT/'user').as_posix())
    for name in sorted(sources):
        path=ROOT/'user'/name;hashes['user/'+name]=sha(path)
        extra[prefix[1:]+'/SRC/'+name]=path.read_bytes()
        if path.suffix.upper()=='.H':extra['SYS/INC/'+name]=path.read_bytes()
    for name in ('SC32TWIN.C','SC32TWIN.json'):
        path=ROOT/'tests/m10a2'/name;hashes['tests/m10a2/'+name]=sha(path)
        extra[prefix[1:]+'/'+name]=path.read_bytes()
    original=sha(args.data);symbols=unique_symbols(build/'core.sym')
    pool_names={'records','owners','handles','descriptions','native_pool','window_pool',
        'view_pool','capture_pool','endpoint_pool','pipe_pool','job_pool','transaction_pool'}
    pool_addresses=[]
    for row in (build/'core.sym').read_text(encoding='ascii').splitlines():
        fields=row.split()
        if len(fields)==3 and fields[2] in pool_names:
            pool_addresses.append((fields[2]+'@'+fields[0],int(fields[0],16)))
    report=dict(status='RUNNING',scope='NATIVE_SYSTEM_LIBRARIES_AND_R3_GUI_MESSAGE_FOUNDATION',
        execution_scope='EXPLICIT_EXTERNAL_SYSTEM_RING3_DESKTOP',cases=[],input_sha256=hashes,
        native_SC32API=dict(bytes=len(library),sha256=sha(args.native_library),source=str(args.native_library)),
        native_SCLDR=dict(bytes=args.native_root.stat().st_size,sha256=sha(args.native_root),
            source=str(args.native_root),compiled_current_turn=False),
        build_report_sha256=sha(args.build_report),source_disk_sha256=original,
        snapshots=[],screenshots=[],full_Phase2=False,full_M10a2=False)
    def persist():
        (args.out/'gui.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    with QemuSession(build/'sandcore.img',args.data,args.out,'tcg',True,audio=False,memory=256,
        data_prepare=lambda image:prepare(image,build,extra,runtime_fixtures=False)) as vm:
        guest=Guest(vm)
        def check(name,good,**details):
            report['cases'].append(dict(name=name,status='PASS' if good else 'FAIL',**details));persist()
            print(name+(': PASS' if good else ': FAIL'),flush=True)
            if not good:raise AssertionError(name)
        def run(name,command,contains=None,timeout=120):
            code,output,wall,cpu=guest.command(command,timeout=timeout)
            check(name,code==0 and (contains is None or contains in output),command=command,
                exit_code=code,output=output.decode('utf-8','replace'),wall_seconds=wall,qemu_cpu_seconds=cpu)
            return output
        def screen(label):
            path=vm.out/(label+'.ppm');vm.hmp('screendump "'+path.as_posix()+'"')
            report['screenshots'].append(png_from_ppm(path));persist()
        def resources():
            values={}
            for label,address in pool_addresses:
                # objpool_t 固定i386布局：objects/pages/peak/high_water偏移1096。
                reply=vm.hmp('xp /4wx 0x%08x'%(address+1096))
                words=re.findall(r'0x([0-9a-fA-F]{8})',reply)
                if len(words)!=4:raise ValueError('对象池只读计账格式改变')
                values[label]=dict(zip(('objects','pages','peak_pages','high_water'),
                    (int(word,16) for word in words)))
            for name in ('wait_pages','dir_pages','meta_pages','allocation_pages','space_pages','cache_pages','capture_pages'):
                if name in symbols:values[name]=physical_word(vm,symbols[name])
            return values
        def key(value):
            vm.hmp('sendkey '+value);time.sleep(.16)
        def move(x,y):
            for _ in range(24):
                mx=physical_word(vm,symbols['mx']);my=physical_word(vm,symbols['my'])
                if (mx,my)==(x,y):return
                dx=max(-120,min(120,x-mx));dy=max(-120,min(120,y-my))
                vm.qmp('input-send-event',{'events':[
                    {'type':'rel','data':{'axis':'x','value':dx}},
                    {'type':'rel','data':{'axis':'y','value':dy}}]});time.sleep(.06)
            raise TimeoutError('真实PS/2光标未到指定位置')
        def button(which,down):
            vm.qmp('input-send-event',{'events':[{'type':'btn','data':{'down':down,'button':which}}]})
            time.sleep(.09)
        def click(x,y,which='left'):
            move(x,y);button(which,True);button(which,False)
        def client(info,x=24,y=64):
            return info['x']+1+x,info['y']+info['height']-info['client_height']-1+y
        def close(info):
            title=info['height']-info['client_height']-1
            click(info['x']+info['width']-8,info['y']+max(3,title//2))
        def snapshot(label,previous=-1,final=False):
            if not final:key('f9')
            deadline=time.monotonic()+12;error=None
            while time.monotonic()<deadline:
                try:
                    blob=guest.get_bytes(package+'/GUI.LOG',label)
                    record=decode(blob);h=record['header']
                    if h['snapshot_count']>previous and (not final or h['flags']&2):
                        report['snapshots'].append(dict(label=label,sha256=hashlib.sha256(blob).hexdigest(),**record));persist()
                        check(label+'-no-local-error',h['error_count']==0 and not (h['flags']&28),header=h)
                        return record
                except (OSError,ValueError,RuntimeError) as failure:error=repr(failure)
                time.sleep(.12)
            raise TimeoutError('窗口快照未更新：'+label+' '+str(error))
        try:
            wait_first_desktop(guest,symbols,report);guest.connect()
            run('native-system-root-and-SC32-file-calls',package+'/SC32PROBE.SCX',b'SC32_FOUNDATION_PASS')
            run('native-GUI-output-directory','mkdir '+package+'/NATIVE')
            app=package+'/NATIVE/SC32TWIN.SCX'
            run('guest-SCCC-builds-public-GUI-app',compiler+' '+prefix+'/SC32TWIN.C '+app+
                ' --dynamic '+prefix+'/SC32TWIN.json',b'SCCC OK SCX DYNAMIC',240)
            blob=guest.get_bytes(app,'native-TWIN.scx');validate_scx(blob)
            report['native_application']=dict(bytes=len(blob),sha256=hashlib.sha256(blob).hexdigest())
            sessions=run('list-explicit-sessions','sessionctl list')
            match=re.search(rb'^([0-9]+)\s+-1\s+-1\s+SYSTEM\s',sessions,re.M)
            check('external-SYSTEM-session-found',bool(match))
            session=int(match[1]);run('show-explicit-SYSTEM-desktop','sessionctl show '+str(session))
            cold,_=stable_pages(vm,symbols);report['resources_before_shell_background']=resources()
            # 已观察到首个GUI背景作业后全局+1。sh.c::background_reserve
            # 首次&保留一页后台表；先测真实Shell成本，再以同一暖态比较GUI。
            run('warm-existing-Shell-background-table','echo SC32_BG_WARM & wait',b'SC32_BG_WARM')
            before,_=stable_pages(vm,symbols);report['resources_before_GUI']=resources()
            check('first-Shell-background-table-is-one-page',before-cold==1,before=cold,after=before,
                source='user/m9/sh.c::background_reserve; first capacity=16, 128B USERALLOC, retained by Shell')
            start=len(guest.console)
            output=run('launch-GUI-background',app+' & echo SC32_TWIN_JOB:$!')
            job=re.search(rb'SC32_TWIN_JOB:([0-9]+)',output);check('GUI-background-job',bool(job))
            guest.wait(rb'SC32_TWIN_READY\r?\n',start,45)
            first=snapshot('initial');h=first['header'];a,b,c=first['windows'];number=h['snapshot_count']
            check('three-window-modal-and-real-session',h['alive_mask']==7 and h['coverage_lo']&1
                and all(w['session']==session and w['owner_pid']==h['pid'] for w in (a,b,c)))
            screen('three-windows')
            # 点击被阻挡父窗只激活实际modal，随后的真实键落C。
            click(*client(a));key('shift-a');modal=snapshot('modal-input',number);number=modal['header']['snapshot_count']
            added=modal['events'][len(first['events']):]
            check('blocked-parent-key-targets-modal',any(e[2]==8 and e[3]==c['window'] and e[10]==65 for e in added)
                and not any(e[2] in (6,7,8) and e[3]==a['window'] for e in added))
            close(c);time.sleep(.18);click(*client(a,200,70));key('shift-a')
            # 右/中按钮和真实KEYUP与同一输入序列一起记录，不扩大VM矩阵。
            click(*client(a,200,70),'right');click(*client(a,200,70),'middle')
            focused=snapshot('A-input',number);number=focused['header']['snapshot_count']
            check('modal-close-keeps-both-independent-roots',focused['header']['alive_mask']==3)
            check('real-Shift-A-text-and-break-code',
                any(e[2]==6 and e[3]==a['window'] and e[10]==0x1e and e[13]&1 for e in focused['events'])
                and any(e[2]==8 and e[3]==a['window'] and e[10]==65 for e in focused['events'])
                and any(e[2]==7 and e[3]==a['window'] and e[10]==0x1e for e in focused['events']))
            click(*client(b,180,70));key('f5');key('a');hidden=snapshot('B-hidden',number);number=hidden['header']['snapshot_count']
            check('hidden-B-stops-paint-and-keeps-other-model',hidden['header']['coverage_lo']&((1<<14)|(1<<16))==((1<<14)|(1<<16)))
            hide_events=[i for i,e in enumerate(hidden['events']) if e[2]==16 and e[3]==b['window'] and e[10]==0]
            check('no-PAINT-after-hidden-visibility-fact',bool(hide_events)
                and not (hidden['windows'][1]['state']&1)
                and not any(e[2]==2 and e[3]==b['window'] for e in hidden['events'][hide_events[-1]+1:]))
            key('f5');key('f6');resized=snapshot('geometry-stale',number);number=resized['header']['snapshot_count']
            check('real-size-invalidates-old-paint-and-submit',resized['header']['coverage_lo']&
                ((1<<15)|(1<<17)|(1<<19)|(1<<20))==((1<<15)|(1<<17)|(1<<19)|(1<<20)))
            a,b,_=resized['windows']
            title_height=b['height']-b['client_height']-1
            move(b['x']+40,b['y']+max(3,title_height//2));button('left',True)
            move(b['x']+82,b['y']+max(3,title_height//2)+18);button('left',False)
            moved=snapshot('physical-title-drag',number);number=moved['header']['snapshot_count']
            new_b=moved['windows'][1]
            check('real-MOVE-preserves-client-geometry',moved['header']['coverage_lo']&(1<<18)
                and (new_b['x'],new_b['y'])!=(b['x'],b['y'])
                and (new_b['client_width'],new_b['client_height'],new_b['geometry_epoch'])==
                    (b['client_width'],b['client_height'],b['geometry_epoch']))
            a,b,_=moved['windows'];click(*client(a));button('left',True)
            move(*client(b,180,70));button('left',False)
            captured=snapshot('captured-cross-window',number);number=captured['header']['snapshot_count']
            check('capture-retains-A-across-B-and-real-up',captured['header']['coverage_lo']&
                ((1<<8)|(1<<9)|(1<<10))==((1<<8)|(1<<9)|(1<<10)))
            close(a);veto=snapshot('A-veto',number);number=veto['header']['snapshot_count']
            check('A-first-close-is-cancellable',veto['header']['alive_mask']==3 and veto['header']['coverage_lo']&(1<<11))
            close(b);time.sleep(.18);click(*client(a,200,70));key('a')
            independent=snapshot('B-closed-A-running',number);number=independent['header']['snapshot_count']
            check('single-close-keeps-process-and-A',independent['header']['alive_mask']==1 and independent['header']['coverage_lo']&(1<<12))
            screen('A-after-B-close');key('f10')
            run('GUI-normal-main-return','wait '+job[1].decode())
            final=snapshot('final',number,True);h=final['header']
            required=(1<<27)-1
            check('declared-input-close-damage-and-cleanup-coverage',h['coverage_lo']&required==required,
                required=required,actual=h['coverage_lo'])
            check('normal-main-releases-own-surfaces-and-windows',h['status']==2 and h['alive_mask']==0
                and h['flags']&2 and h['heap_before']==h['heap_after'],header=h)
            after,observations=stable_pages(vm,symbols)
            report['resources_after_GUI']=resources()
            check('GUI-exit-physical-page-recovery',before==after,before=before,after=after,observations=observations)
            screen('after-normal-exit');report['status']='DECLARED_NATIVE_LIBRARY_AND_GUI_CASES_PASS'
        except BaseException as error:
            report.update(status='FAIL_OR_INTERRUPTED',error=dict(type=type(error).__name__,
                message=str(error),traceback=traceback.format_exc()))
            try:screen('failure');report['failure_registers']=vm.hmp('info registers')
            except Exception as capture:report['capture_error']=repr(capture)
            raise
        finally:
            guest.close();report['source_disk_unchanged']=sha(args.data)==original
            report['current_inputs_unchanged']=all(sha(ROOT/name)==digest for name,digest in hashes.items());persist()
    print(report['status'],flush=True)

if __name__=='__main__':main()
