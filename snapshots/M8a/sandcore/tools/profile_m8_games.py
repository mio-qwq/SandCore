#!/usr/bin/env python3
"""mio：当前真实G2游戏/六幕动画的完整客户区运行与性能观察。

原生SCX只从本轮编译报告取，测试副本启动新世界而不灌通关存档。
实际输入开始比赛/恢复远征/转动鼠标/暂停或关闭。完整分辨率、客体
采样与宿主CPU分列；MEASURED不等于玩法/视觉/15组合全部验收PASS。
"""
import argparse
import bisect
from collections import Counter
import json
import os
import re
import shutil
import struct
import time
import traceback
import verify_m8_phase2 as phase
import profile_m8_runtime as cost

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler


def sample_program_counter(mapping,owner,samples):
    """mio：只读PC样本定位热函数；与正式FPS计费窗口分开。

    stop/info/cont会产生观察开销，因此这些样本只能解释时间主要
    花在哪段代码，不能拿暂停后的墙钟计算帧率。只记当前owner
    真正在三环的EIP，不把内核处理或另一个任务的现场归给游戏。
    使用同一原生产物自己的map，最后函数上界取首个OBJECT地址。
    """
    functions=[];data=[]
    for line in mapping.decode('utf-8').splitlines():
        fields=line.split()
        if len(fields)<3 or not re.fullmatch('[0-9A-Fa-f]{8}',fields[0]):continue
        address=int(fields[0],16)
        if fields[2]=='OBJECT':data.append(address)
        else:functions.append((address,fields[1]))
    functions.sort();starts=[item[0] for item in functions]
    text_end=min(data) if data else 0xFFFFFFFF
    histogram=Counter();observations=[]
    for index in range(samples):
        time.sleep(.03+(index%7)*.007)
        q.hmp('stop')
        try:
            current=t.word(q.symbols()['current'])
            registers=q.hmp('info registers')
            eip=re.search(r'\bEIP=([0-9A-Fa-f]+)',registers)
            cs=re.search(r'\bCS\s*=([0-9A-Fa-f]+)',registers)
            pc=int(eip[1],16) if eip else 0
            selector=int(cs[1],16) if cs else 0
            rank=bisect.bisect_right(starts,pc)-1
            name=functions[rank][1] if current==owner and selector&3==3 and rank>=0 and pc<text_end else None
            observations.append(dict(current=current,cs=selector,eip=pc,function=name))
            if name:histogram[name]+=1
        finally:q.hmp('cont')
    return dict(scope='READ_ONLY_PC_SAMPLES_OUTSIDE_FPS_WINDOW',samples=samples,
        accepted=sum(histogram.values()),functions=dict(histogram.most_common()),observations=observations,
        limitations='少量停止式PC样本仅定位热路；非精确指令计费，不作为FPS或输入延迟依据')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True)
    origin=parser.add_mutually_exclusive_group(required=True)
    origin.add_argument('--native-stage')
    origin.add_argument('--host-bootstrap',action='store_true',help='仅诊断GCC-O2代码生成差距，不作为原生交付证据')
    parser.add_argument('--resource-floor',action='store_true',help='交替真实缓存图的搬运/FRAME/合成固定成本；不是游戏FPS上限')
    parser.add_argument('--width',type=int,default=640);parser.add_argument('--height',type=int,default=480)
    parser.add_argument('--apps',default='race,world,lumen,mines');parser.add_argument('--seconds',type=float,default=12)
    parser.add_argument('--sample-eip',type=int,default=0,help='FPS窗口结束后只读PC样本数；0保持原测量')
    parser.add_argument('--accel',choices=('tcg','whpx'),default='tcg',help='显式加速模式，失败不得回退冒充')
    parser.add_argument('--tcg-x64',action='store_true',help='TCG使用同一x86_64宿主QEMU做严格对照')
    parser.add_argument('--moving-look',action='store_true',help='World计费窗实际持续相对鼠标改变视角，另计真实合成帧')
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    os.environ['SANDCORE_QEMU_ACCEL']=args.accel
    os.environ['SANDCORE_QEMU_TCG_TARGET']='x86_64' if args.tcg_x64 else 'i386'
    if args.resource_floor:
        assert args.apps=='framebench' and args.native_stage,'固定成本只接受实际G2生成的独立framebench'
    if args.native_stage:
        stage=ROOT/args.native_stage;proof=json.loads((stage/'results.json').read_text(encoding='utf-8'))
        assert proof['status']=='PASS' and proof['scope']=='NATIVE_COMPILE_ONLY'
    else:
        # mio：相同源/内核/场景的优化GCC产物提供另一条诊断轴。
        # build/fs可能留有历史原生.map，因此绝不能把它与新GCC的
        # SCX配对；符号必须取同次ELF的nm表，并逐字节核对载荷。
        # 此分支显式标HOST，既不伪造G2报告，也不用于正式发布。
        stage=out/'host-inputs';stage.mkdir()
        proof=dict(g2_sha256=None,artifacts={},source_inputs={})
        for path in sorted((ROOT/'user').iterdir()):
            if path.is_file() and path.suffix in ('.c','.inc','.H','.h'):
                proof['source_inputs'][path.name]=phase.sha(path.read_bytes())
        for app in args.apps.split(','):
            scx=(ROOT/'build/fs/apps'/f'{app}.scx').read_bytes()
            binary=(ROOT/'build'/f'user-{app}.bin').read_bytes()
            assert scx[:8]==b'SCX1MIO\0' and scx[36:36+len(binary)]==binary
            raw=(ROOT/'build'/f'user-{app}.sym').read_bytes();rows=['SCSYM1MIO']
            for line in raw.decode('utf-8').splitlines():
                fields=line.split()
                if len(fields)==3 and fields[1] in ('b','B','d','D','t','T'):
                    rows.append(fields[0]+' '+fields[2]+' '+('OBJECT' if fields[1] in ('b','B','d','D') else 'HOST_GCC'))
            mapping=('\n'.join(rows)+'\n').encode('utf-8')
            (stage/(app+'.scx')).write_bytes(scx);(stage/(app+'.map')).write_bytes(mapping)
            for suffix in ('bin','sym','kernel.elf'):
                shutil.copy2(ROOT/'build'/f'user-{app}.{suffix}',stage/f'user-{app}.{suffix}')
            proof['artifacts'][app]=dict(sha256=phase.sha(scx),map_sha256=phase.sha(mapping),
                                        source_sha256=proof['source_inputs'][app+'.c'])
        (stage/'inputs.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
    scope=('RESOURCE_CACHE_FIXED_COST' if args.resource_floor else
           'HOST_GCC_O2_DIAGNOSTIC' if args.host_bootstrap else 'CURRENT_NATIVE_GAMES_MEASURED')
    report=dict(author='mio',status='RUNNING',scope=scope,
                width=args.width,height=args.height,scale=100,theme='Aurora',compiler_sha256=proof['g2_sha256'],
                requested_accel=args.accel,tcg_target=os.environ['SANDCORE_QEMU_TCG_TARGET'],
                source_inputs=proof['source_inputs'],
                inputs={name:phase.sha((ROOT/'build'/name).read_bytes())
                        for name in ('sandcore.img','sanddata.img','kernel.sym','kernel.elf')},apps={})
    # 后续构建不能把另一代ELF符号放进运行中的只读观察。
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    for app in args.apps.split(','):
        target=out/app;target.mkdir();phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=cost.OUT=target
        c.v.windows=t.windows
        native=(stage/(app+'.scx')).read_bytes();mapping=(stage/(app+'.map')).read_bytes()
        assert phase.sha(native)==proof['artifacts'][app]['sha256']
        symbols=phase.theme.native_symbols(mapping);kernel=q.symbols();cost.SYM=kernel
        shutil.copy2(ROOT/'build/sandcore.img',target/'sandcore.img')
        def prepare(disk):
            c.disk_put(disk,'HOME/'+app.upper()+'.SCX',native)
            c.disk_put(disk,'SYS/DISPLAY.CFG',f'SCFG1MIO\nwidth={args.width}\nheight={args.height}\nscale=100\n'.encode())
            c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
        proc=t.launch('std',128,'game',prepare,floppy=(target/'sandcore.img').as_posix());cost.PROC=proc
        phase.DISK=target/'sanddata-std-128-game.img'
        result=dict(native_sha256=phase.sha(native),map_sha256=phase.sha(mapping),samples=[],checks=[])
        try:
            phase.wait(lambda:t.word(kernel['boot_stage'])==2,'welcome',180)
            t.open_shell(True);phase.idle();baseline=t.word(kernel['pf_used'])
            start=time.monotonic();q.text('run HOME/'+app.upper()+'.SCX\n')
            window=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'game-window')
            pd=t.word(kernel['tasks']+window['owner']*168)
            def value(name):return struct.unpack('<i',q.memory(t.physical(pd,symbols[name]),4))[0]
            cost.EXTRA_COUNTERS={'app_ui_frames':t.physical(pd,symbols['ui_frames'])}
            phase.wait(lambda:value('ui_frames')>=2,'first-app-frames',120)
            result['first_frames_observed_seconds']=round(time.monotonic()-start,4)
            q.shot('01-initial');result['checks'].append('真实原生应用窗口及已提交帧')
            if app=='race':
                assert value('race_menu')==1
                result['samples'].append(cost.measure('02-static-menu',6))
                submitted=value('ui_frames');q.key('ret')
                phase.wait(lambda:value('race_menu')==0 and value('ui_frames')>submitted
                           and ('scn_render_width' not in symbols or value('scn_render_width')>0),'race-start',120)
            elif app=='world':
                assert value('world_panel')==4 and value('world_tool')==0 and value('world_sites')==0
                result['samples'].append(cost.measure('02-static-menu',6))
                submitted=value('ui_frames');q.key('p')
                phase.wait(lambda:value('world_panel')==0 and value('world_captured')==1,'world-capture',60)
                phase.wait(lambda:value('ui_frames')>submitted and
                           ('scn_render_width' not in symbols or value('scn_render_width')>0),
                           'world-first-real-frame',120)
            if args.moving_look:assert app=='world','moving-look只验证真实捕获的World'
            sample=cost.measure('03-playing-full-size',args.seconds,move=args.moving_look);result['samples'].append(sample)
            result['moving_look']=args.moving_look
            if args.resource_floor:
                # ui_present尝试次数不是显示帧率。早期夹具把整画布
                # 清掉后只显示相同背景，已保留其失败事实；新的固定
                # 成本至少要求合成器真正处理持续不同内容，禁止仅凭
                # ui_frames增长给出“60FPS已实现”的结论。
                assert sample['guest_delta']['wm_frames']>=10,'没有足够真实不同内容合成帧'
            result['application_frames_per_host_second']=round(
                sample['guest_delta']['app_ui_frames']/sample['wall_seconds'],3)
            result['geometry']={name:value(name) for name in ('ui_width','ui_height','ui_scale')}
            for name in ('scn_render_width','scn_render_height','scn_frame_ticks','road_view_height','world_view_height'):
                if name in symbols:result['geometry'][name]=value(name)
            if 'scn_active' in symbols:
                # 只读真实上下文的error，避免缓存/查询范围失败被一张
                # 看似正常的背景遮住。字段属于本库源码布局；按本批
                # map定位指针，不能拿其它SCX或旧内核虚地址读现场。
                scene=value('scn_active')&0xFFFFFFFF
                result['geometry']['scene_error']=struct.unpack('<i',q.memory(t.physical(pd,scene+12),4))[0]
                assert result['geometry']['scene_error']==0,'运行中场景几何错误'
            if args.sample_eip:result['pc_profile']=sample_program_counter(mapping,window['owner'],args.sample_eip)
            if app=='world':
                # 捕获使用原始相对包，实际桌面指针停留；不是只验证字段被设1。
                if args.moving_look:
                    # 连续输入的最后PS/2包可能还未被慢帧消费。先
                    # 只读核总量/已消费位置相等，再建立单次60像素
                    # 的yaw基线；不得清队列或写状态伪造120增量。
                    phase.wait(lambda:t.word(kernel['relative_x'])==t.word(kernel['relative_last_x'])
                        and t.word(kernel['relative_y'])==t.word(kernel['relative_last_y']),
                        'moving-look-input-drained',120)
                old_yaw=value('yaw');position=(t.word(kernel['mx']),t.word(kernel['my']))
                q.move(60,0)
                phase.wait(lambda:value('yaw')==((old_yaw+120)&1023),'raw-relative-look',120)
                assert position==(t.word(kernel['mx']),t.word(kernel['my'])),'captured desktop cursor moved'
                q.shot('04-relative-look');result['checks'].append('实际相对60像素转yaw120，桌面指针保持')
                q.key('esc');phase.wait(lambda:t.word(kernel['relative_owner'])==0,'esc-kernel-release',3)
                phase.wait(lambda:value('world_panel')==4,'world-pause',120)
                result['samples'].append(cost.measure('05-paused-menu',6))
                result['checks'].append('真实Esc释放捕获并暂停')
            elif app=='lumen':
                submitted=value('ui_frames');q.key('spc')
                phase.wait(lambda:value('paused')==1,'film-pause',120)
                if 'film_frame_valid' in symbols:
                    # 暂停标志可在最后一张暂停场景尚未完成时已经为1。
                    # 成本窗口要从已提交暂停画面开始，否则把首次定格的
                    # 必需渲染误计为“暂停后仍无限重光追”。旧程序没有
                    # 新缓存字段，保留原测量范围和失败事实。
                    phase.wait(lambda:value('ui_frames')>submitted and value('film_frame_valid')
                               and value('film_frame_local')==value('film_local')
                               and value('film_frame_chapter')==value('film_chapter'),
                               'film-paused-frame-submitted',120)
                result['samples'].append(cost.measure('05-paused-film',6))
            elif app=='race':
                q.key('m');phase.wait(lambda:value('race_menu')==1,'race-pause',120)
                result['samples'].append(cost.measure('05-paused-menu',6))
            current=t.windows()[-1]
            t.point(current['x']+current['w']-12,current['y']+12);t.click();phase.idle()
            assert t.word(kernel['pf_used'])==baseline,'native game leaked pages'
            assert t.word(kernel['keyboard_overflow'])==t.word(kernel['event_overflow'])==0
            result['checks'].append('实际标题关闭/严格页回收/零队列溢出')
            report['apps'][app]=result
            (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        except Exception as error:
            report['apps'][app]=result;report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
            if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
            (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            raise
        finally:
            if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)
    report.update(status='MEASURED',limitations=('HOST_GCC_O2仅代码生成差距诊断；' if args.host_bootstrap else '真正G2产物；')+
                  ('两幅不同缓存图交替搬运+完整客户帧提交+合成固定成本，不是游戏算法的最优FPS上限；' if args.resource_floor else '')+
                  '指定模式/尺寸的启动/暂停/捕获/成本观察；非完整玩法通关、全部显示矩阵或视觉达标')
    (out/'measurements.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(report['scope'],json.dumps({name:dict(
        fps=item['application_frames_per_host_second'],geometry=item['geometry'],
        hotspots=item.get('pc_profile',{}).get('functions',{}))
        for name,item in report['apps'].items()},ensure_ascii=True),flush=True)


if __name__=='__main__':main()
