#!/usr/bin/env python3
"""mio：Monitor草稿的真实G2算术/快照/负载与鼠标核心验收。

普通三环探针包含完整Monitor源，只重命名入口；防溢出计算、
名称边界和采样函数实际由G2执行。宿主大整数只作独立参考。
API护栏是客体自己的哨兵，输出通过WRITE落盘；没有客体内存
注入。GUI另测真实采样、Freeze/Resume、右键不穿透与页回收。
"""
import hashlib,json,struct,sys,time,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
OUT=ROOT/'build/m8-monitor-draft';SOURCE=ROOT/'build/m8-next/monitor.c'
RATIOS=((0,0),(1,0),(0,1),(1,1),(2,1),(1,3),(2,3),(99,100),(100,101),
    (0x7FFFFFFF,0xFFFFFFFF),(0xFFFFFFFE,0xFFFFFFFF),(0xFFFFFFFF,0xFFFFFFFF),
    (0x80000000,0x80000001),(0x12345678,0x87654321),(10000000,20000001))
NAMES=(
    (b'ABCDEFGHIJKL',b'ABCDEFGHIJKL'),
    ('沙核abcdef'.encode(),'沙核abcdef'.encode()),
    (b'ABCDEFGHIJK\xC2',b'ABCDEFGHIJK'),
    (b'ABC\xE0\x80\x80zzz',b'ABC'),(b'ABC\xED\xA0\x80',b'ABC'),
    (b'ABC\xF4\x90\x80\x80',b'ABC'),(b'ABC\xF0\x80\x80\x80',b'ABC'),
    (b'ABC\xC2\xFF',b'ABC'),(b'AB\xF4\x8F\xBF\xBFxyz',b'AB\xF4\x8F\xBF\xBFxyz'),
)
BUSY='''/* mio：真正普通三环算术负载，不修改计时或CPU计数。 */
#include "SCAPI.H"
static volatile u32 busy_counter;
int main(void){int w=sc_open("CPU load / mio",240,100);if(w<0)return 1;
sc_text(w,8,12,"Actual computation",PAL_UI_TEXT);
for(;;){if(sc_key()==27)return 0;for(int i=0;i<50000;i++)busy_counter=busy_counter*1664525u+1013904223u;}}
'''.encode()
GFX='''/* mio：完整普通ARGB应用连续提交，不伪造合成样本。 */
#include "SCAPI.H"
#include "NUI.inc"
int main(void){if(ui_open("Graphics load / mio")<0)return 1;
for(;;){if(!ui_frame_due())continue;ui_pointer();ui_background(SC_THEME_FACE_ALT);
ui_number(24,24,ui_frames,PAL_UI_CYAN+7);ui_present();if(sc_key()==27)return 0;}}
'''.encode()


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def probe_source():
    names=b''.join(raw.ljust(12,b'\0')+b'\xA5'*4 for raw,_ in NAMES)
    # 固定12B输入旁边再放4B哨兵；输出13B旁边再放7B。源函数
    # 只能复制完整标量，结果也核对源区，不能以视觉截断算安全。
    return ('''/* mio：完整源同一函数由G2执行；正常WRITE/EXIT回传。
 * 两段负载各取实际公开基点与终点；保留完整64字，比例参考
 * 由宿主无限精度除法计算。循环不篡改IRQ/PIT或任务状态。 */
#define main monitor_original_main
#include "MONITOR.inc"
#undef main
static u32 ratio_input[]={'''+','.join(hex(number)+'u' for pair in RATIOS for number in pair)+'''};
static u32 ratio_output['''+str(len(RATIOS))+'''];
static u8 name_input[]={'''+','.join(str(number) for number in names)+'''};
static u8 name_output['''+str(len(NAMES)*20)+'''];
static u32 api_output[150],records[288];
static volatile u32 computation;
int main(void)
{
    for(int i=0;i<'''+str(len(RATIOS))+''';i++)ratio_output[i]=(u32)percent(ratio_input[i*2],ratio_input[i*2+1]);
    for(int i=0;i<sizeof(name_output);i++)name_output[i]=0xA5;
    for(int i=0;i<'''+str(len(NAMES))+''';i++)task_name((char*)name_output+i*20,(char*)name_input+i*16);
    for(int i=0;i<150;i++)api_output[i]=0xA5C37E91u;
    if(sc_monitor(api_output+1)||sc_storage(api_output+51)||sc_cpu(api_output+85))return 1;
    snapshots();
    for(int phase=0;phase<2;phase++){
        baseline();int start=sc_tick();
        while((u32)sc_tick()-(u32)start<200){
            if(!phase)sc_yield();else for(int i=0;i<50000;i++)computation=computation*1664525u+1013904223u;
        }
        u32 *out=records+phase*144;
        for(int i=0;i<64;i++)out[i]=before[i];
        sample();for(int i=0;i<64;i++)out[64+i]=cpu[i];
        out[128]=(u32)cpu_valid;out[129]=(u32)cpu_busy;out[130]=(u32)cpu_idle;
        out[131]=(u32)cpu_kernel;out[132]=(u32)cpu_user;out[133]=(u32)cpu_graphics;
        for(int i=0;i<8;i++)out[134+i]=(u32)task_percent[i];
        out[142]=(u32)start;out[143]=(u32)last_sample;
    }
    if(sc_write("HOME/RATIO.BIN",ratio_output,sizeof(ratio_output))<0)return 2;
    if(sc_write("HOME/NAMES.BIN",name_output,sizeof(name_output))<0)return 3;
    if(sc_write("HOME/NINPUT.BIN",name_input,sizeof(name_input))<0)return 4;
    if(sc_write("HOME/API.BIN",api_output,sizeof(api_output))<0)return 5;
    if(sc_write("HOME/SAMPLES.BIN",records,sizeof(records))<0)return 6;
    return 0;
}
''').encode(),names


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    inputs={name:sha(ROOT/name) for name in ('build/sandcore.img','build/sanddata.img','build/kernel.elf','build/kernel.sym',
        'user/SCAPI.H','user/NUI.inc','kernel/font16.txt','build/m8-next/monitor.c')};verifier=sha(Path(__file__))
    source,name_input=probe_source();(OUT/'probe.c').write_bytes(source)
    for name,blob in (('busy.c',BUSY),('graphics.c',GFX)):(OUT/name).write_bytes(blob)
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2)
        for name,blob in (('monitor.c',SOURCE.read_bytes()),('MONITOR.inc',SOURCE.read_bytes()),('probe.c',source),('busy.c',BUSY),('graphics.c',GFX)):
            compiler.disk_put(disk,'SYS/MON/'+name,blob)
        for name in ('SCAPI.H','NUI.inc'):compiler.disk_put(disk,'SYS/MON/'+name,(ROOT/'user'/name).read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'monitor-core',prepare)
    disk=OUT/'sanddata-std-128-monitor-core.img';w=None;addresses=None;checks=[];artifacts={};observations=[]
    try:
        t.open_shell(True);v.idle()
        for name in ('monitor','probe','busy','graphics'):
            blob=compiler.compile_native(disk,'BIN/G2.SCX','SYS/MON/'+name+'.c','HOME/'+name.upper()+'.SCX',300);v.idle()
            mapping=compiler.await_file(disk,'HOME/'+name.upper()+'.SCX.map')
            (OUT/(name+'-native.scx')).write_bytes(blob);(OUT/(name+'-native.map')).write_bytes(mapping)
            artifacts[name]=dict(scx_sha256=hashlib.sha256(blob).hexdigest(),map_sha256=hashlib.sha256(mapping).hexdigest())
        baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/PROBE.SCX\n');outputs={name:compiler.await_file(disk,'HOME/'+name+'.BIN',120) for name in ('RATIO','NAMES','NINPUT','API','SAMPLES')};v.idle()
        assert list(struct.unpack('<'+str(len(RATIOS))+'I',outputs['RATIO']))==[0 if not total else min(100,value*100//total) for value,total in RATIOS]
        assert outputs['NINPUT']==name_input and len(outputs['NAMES'])==len(NAMES)*20
        for i,(_,expected) in enumerate(NAMES):
            item=outputs['NAMES'][i*20:(i+1)*20];assert item[:len(expected)+1]==expected+b'\0'
            assert item[len(expected)+1:]==b'\xA5'*(19-len(expected))
        api=struct.unpack('<150I',outputs['API']);assert all(api[i]==0xA5C37E91 for i in (0,49,50,83,84,149))
        assert api[1]==api[51]==api[85]==1 and api[6]==8 and api[93]==8
        assert api[88]==api[89]+api[90]+api[91] and api[92]<=api[90]
        for i in range(2):
            words=struct.unpack_from('<144I',outputs['SAMPLES'],i*576);a=words[:64];b=words[64:128];delta=lambda n:(b[n]-a[n])&0xFFFFFFFF
            total=delta(3);assert total>0 and words[128]==1 and ((words[143]-words[142])&0xFFFFFFFF)>=200
            reference=[100-min(100,delta(4)*100//total)]+[min(100,delta(j)*100//total) for j in (4,5,6,7)]
            assert list(words[129:134])==reference
            for slot in range(8):
                at=16+slot*6;ticks=((b[at+3]-a[at+3])&0xFFFFFFFF) if b[at+2]==a[at+2] else b[at+3]
                assert words[134+slot]==min(100,ticks*100//total)
            assert delta(3)==sum(delta(j) for j in (4,5,6)) and delta(7)<=delta(5)
            observations.append(dict(phase='yield-idle' if i==0 else 'actual-computation',ticks=total,metrics=reference,task_percent=list(words[134:142])))
        assert observations[0]['metrics'][1]>observations[1]['metrics'][1] and observations[1]['metrics'][3]>observations[0]['metrics'][3]
        seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'普通CLI探针严格页回收')
        q.shot('01-g2-real-arithmetic-api-and-load-probe');checks.append('15防溢出比例/9完整UTF-8名称/三种原API哨兵/两段真实PIT差分与算术负载')
        addresses=theme.native_symbols((OUT/'monitor-native.map').read_bytes())
        q.text('run HOME/MONITOR.SCX\n');w=seed.wait(theme.window,'实际G2 Monitor')
        def number(name):return theme.user_word(w,addresses[name])
        def words(name,count):return list(struct.unpack('<'+str(count)+'I',v.user_bytes(w,addresses[name],count*4)))
        def current():return next(win for win in t.windows() if win['handle']==w['handle'])
        def present():
            old=number('ui_frames');seed.wait(lambda:number('ui_frames')>old and t.word(kernel['dirty'])==0,'Monitor实际提交帧',180)
        def click(x,y,right=False):
            win=current();scale=number('ui_scale');title=win['h']-win['ch']-1;t.point(win['x']+1+x*scale//100,win['y']+title+y*scale//100)
            if right:
                q.qmp([{'type':'btn','data':{'down':True,'button':'right'}}]);time.sleep(.15)
                q.qmp([{'type':'btn','data':{'down':False,'button':'right'}}]);time.sleep(.3)
            else:t.click()
        def frame(label):
            # 正常主循环可能已开始生产下一帧。只认可同次暂停时的
            # 全幅私有页=已提交画布；短期重取不放宽任何像素边界。
            deadline=time.monotonic()+15
            while time.monotonic()<deadline:
                with t.stable_frame():
                    win=current();valid=v.user_bytes(w,number('ui_pixels'),win['cw']*win['ch']*4)==q.memory(win['canvas'],win['cw']*win['ch']*4)
                    if valid:
                        assert number('monitor_valid')==number('storage_valid')==1
                        break
                time.sleep(.12)
            else:raise AssertionError('未取得完整实际Monitor帧：'+label)
            q.shot(label)
        def menu(item):
            click(60,number('monitor_y')+10,True);seed.wait(lambda:number('monitor_menu')==1,'实际右键菜单');present()
            with t.stable_frame():
                col=number('menu_columns');x=number('menu_x')+8+(item%col)*(number('menu_button')+8);y=number('menu_y')+8+(item//col)*number('menu_step')
            click(x+number('menu_button')//2,y+10);seed.wait(lambda:number('monitor_menu')==0,'实际右键选项');present()
        def focus(slot):
            count=len(t.windows());bw=max(40,min(140,(1024-240)//count));t.point(116+slot*bw+bw//2,752);t.click()
        seed.wait(lambda:number('samples')>=2 and number('cpu_valid')==1,'真正CPU历史',180);frame('02-native-cpu-ram-history')
        click(260,82);seed.wait(lambda:number('frozen')==1,'实际鼠标冻结');present()
        frozen={name:words(name,count) for name,count in (('history',120),('memory_history',120),('memory_history_valid',120),('snapshot',48),('cpu',64))}
        ticks=t.word(kernel['sc_ticks']);seed.wait(lambda:((t.word(kernel['sc_ticks'])-ticks)&0xFFFFFFFF)>=160,'冻结时系统PIT继续',60)
        with t.stable_frame():assert frozen=={name:words(name,count) for name,count in (('history',120),('memory_history',120),('memory_history_valid',120),('snapshot',48),('cpu',64))}
        frame('03-frozen-snapshots-system-still-runs');click(260,82);seed.wait(lambda:number('frozen')==0,'真实Resume');present()
        seed.wait(lambda:number('cpu_valid')==1,'恢复基点后新样本',180);menu(2);assert number('view')==2;frame('04-mouse-all-cpu-memory-fields')
        menu(3);assert number('view')==3;frame('05-mouse-cpu-history');menu(4);assert number('view')==4;frame('06-mouse-ram-history')
        menu(1);assert number('view')==1;frame('07-mouse-real-ata-volume-fields')
        click(60,number('monitor_y')+10,True)
        seed.wait(lambda:number('monitor_menu')==1,'真实浮层');present()
        before=(number('view'),number('frozen'),number('monitor_operations'));click(16+96//2,82)
        seed.wait(lambda:number('monitor_menu')==0,'浮层外只关闭');present();assert (number('view'),number('frozen'),number('monitor_operations'))==before
        frame('08-menu-dismiss-no-overview-through');menu(0)
        for helper in ('BUSY','GRAPHICS'):
            focus(0);q.text('run HOME/'+helper+'.SCX\n');seed.wait(lambda:len(t.windows())==3,'真正独立负载窗口')
            load=next(win for win in t.windows() if win['handle'] not in (w['handle'],min(row['handle'] for row in t.windows())))
            slot=load['owner'];generation=t.word(kernel['cpu_generation']+slot*4);samples=number('samples');focus(1)
            seed.wait(lambda:number('samples')>=samples+2,'真实负载的新CPU样本',180)
            with t.stable_frame():
                values=words('cpu',64);observations.append(dict(phase=helper.lower(),pid=slot,generation=generation,metrics={name:number(name) for name in ('cpu_busy','cpu_idle','cpu_kernel','cpu_user','cpu_graphics')},task_percent=words('task_percent',8)))
                assert values[16+slot*6+2]==generation and 0<=number('cpu_graphics')<=number('cpu_kernel')<=100
                if helper=='BUSY':assert words('task_percent',8)[slot]>0 and number('cpu_user')>0
            frame('09-'+helper.lower()+'-real-load');focus(2);q.key('esc');seed.wait(lambda:len(t.windows())==2 and compiler.task_states()[slot]==0,'独立负载正常退出并释放槽');focus(1)
        load_observations=[row for row in observations if row['phase'] in ('busy','graphics')]
        assert load_observations[0]['pid']==load_observations[1]['pid'] and load_observations[1]['generation']==(load_observations[0]['generation']+1)&0xFFFFFFFF
        q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'所有Monitor/负载/私有历史/帧/页表严格回收')
        q.shot('10-all-core-pages-reclaimed');checks.append('实际CPU/RAM历史/Freeze恢复/各页右键/不穿透/真正计算和ARGB负载与严格回收')
        overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and verifier==sha(Path(__file__))
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native=artifacts,checks=checks,observations=observations,overflow=overflow,
            ratios=[dict(value=value,total=total,percent=0 if not total else min(100,value*100//total)) for value,total in RATIOS],
            helper_source_sha256={name:sha(OUT/name) for name in ('probe.c','busy.c','graphics.c')},
            limits='独立草稿真实历史G2/当前核1024×768100%核心；比例为PIT客体状态采样，图形子集不要求每次短窗口必然采中；十九布局/正式预装/旧目录盘回归及全M8另验')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:
            if w is not None and addresses is not None:
                with t.stable_frame():
                    state={name:theme.user_word(w,addresses[name]) for name in ('view','samples','cursor','frozen','cpu_valid','monitor_valid','storage_valid','monitor_operations','ui_frames','ui_focus')}
                    state.update(registers=q.hmp('info registers'));(OUT/'failure-state.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
