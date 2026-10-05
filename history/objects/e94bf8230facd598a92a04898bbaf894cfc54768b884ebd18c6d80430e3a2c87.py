#!/usr/bin/env python3
"""mio：新版SC DEBUG草稿的真实目标现场/TF/断点/内存核心。

历史G2实际生成调试器与目标。HMP/QMP操作；只读核保存的目标
88B现场、目标私有代码/数据与实际窗口，不能借写内存伪造断点。
成功只证明此独立草稿核心，正式预装与十九布局另行验证。
"""
import hashlib,json,struct,sys,time,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
OUT=ROOT/'build/m8-debugger-draft'
TARGET='''/* mio：真实G2目标，pulse每次改变私有计数便于核对运行/
 * 暂停。专属调试目标不会从宿主改状态或代码，断点仅由DEBUG API。
 * main实际开窗口并写文件，证明Continue已经真正执行应用代码。 */
#include "SCAPI.H"
volatile u32 target_counter;
void pulse(void){target_counter++;}
int main(void)
{
    int w=sc_open("Debug target / mio",240,100);if(w<0)return 1;
    sc_text(w,8,12,"True CPU target / mio",PAL_UI_TEXT);
    sc_write("HOME/DEBUG.OK","LIVE",4);
    for(;;){if(sc_key()==27)return 23;pulse();sc_yield();}
}
'''.encode()


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    inputs={name:sha(ROOT/name) for name in ('build/sandcore.img','build/kernel.elf','build/kernel.sym','build/sanddata.img',
        'build/m8-next/debugger.c','user/SCAPI.H','user/NUI.inc','kernel/font16.txt')};verifier=sha(Path(__file__))
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    (OUT/'target.c').write_bytes(TARGET)
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2);compiler.disk_put(disk,'SYS/SRC/debugger.c',(ROOT/'build/m8-next/debugger.c').read_bytes())
        compiler.disk_put(disk,'SYS/SRC/NUI.inc',(ROOT/'user/NUI.inc').read_bytes());compiler.disk_put(disk,'HOME/TARGET.C',TARGET)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'debugger-core',prepare);disk=OUT/'sanddata-std-128-debugger-core.img'
    artifacts={};checks=[];contexts=[];w=None;addresses=None
    try:
        t.open_shell(True);v.idle()
        for name,source,path in (('debugger','SYS/SRC/debugger.c','HOME/DBG.SCX'),('target','HOME/TARGET.C','HOME/TARGET.SCX')):
            native=compiler.compile_native(disk,'BIN/G2.SCX',source,path,300);v.idle();mapping=compiler.await_file(disk,path+'.map')
            (OUT/(name+'-native.scx')).write_bytes(native);(OUT/(name+'-native.map')).write_bytes(mapping)
            artifacts[name]=dict(scx_sha256=hashlib.sha256(native).hexdigest(),map_sha256=hashlib.sha256(mapping).hexdigest())
        target_map=(OUT/'target-native.map').read_bytes();target_addresses=theme.native_symbols(target_map)
        pulse=next(int(p[0],16) for row in target_map.decode().splitlines()[1:] if len(p:=row.split())>=3 and p[1]=='pulse' and p[2]!='OBJECT')
        addresses=theme.native_symbols((OUT/'debugger-native.map').read_bytes());baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/DBG.SCX HOME/TARGET.SCX\n');w=seed.wait(theme.window,'真实G2图形调试器')
        def number(name):return theme.user_word(w,addresses[name])
        def text(name,n=64):return v.user_bytes(w,addresses[name],n).split(b'\0')[0]
        def target():return dict(owner=number('target_pid'))
        def context():return list(struct.unpack('<22I',v.user_bytes(w,addresses['context'],88)))
        def present():
            old=number('ui_frames');seed.wait(lambda:number('ui_frames')>old and t.word(kernel['dirty'])==0,'调试器实际完整帧',180)
        def snapshot(label):
            with t.stable_frame():
                pid=number('target_pid');assert pid<8 and compiler.task_states()[pid]==3
                saved=t.word(kernel['tasks']+pid*168+8);actual=q.memory(saved,76);shown=v.user_bytes(w,addresses['context'],88)
                assert shown[:76]==actual,'显示的并非内核保存的目标真实现场'
                values=list(struct.unpack('<22I',shown));assert values[19]==3
                contexts.append(dict(label=label,context=values,target_saved_esp=saved,actual_frame_sha256=hashlib.sha256(actual).hexdigest()))
                return values
        def operation(key,address=None,cancel=False):
            if not number('ui_focus'):
                handles=sorted(win['handle'] for win in t.windows());bw=max(40,min(140,(1024-240)//len(handles)))
                t.point(116+bw*handles.index(w['handle'])+bw//2,752);t.click();seed.wait(lambda:number('ui_focus')==1,'实际任务栏回调试器操作')
            count=number('debug_operations');q.key(key)
            if address is not None or cancel:
                seed.wait(lambda:number('ui_modal')==1,'实际地址输入框')
                if cancel:q.key('esc')
                else:q.text(address+'\n')
            seed.wait(lambda:number('debug_operations')>count,'调试动作真实完成',180);present()
        seed.wait(lambda:number('paused')==number('have_context')==1 and number('ui_frames')>=2,'目标首指令前暂停/现场')
        initial=snapshot('first-instruction');assert initial[14]==0x400000;assert compiler.file_content(disk,'HOME/DEBUG.OK') is None
        q.shot('01-first-instruction-real-88-byte-context')
        old_event=number('last_event');operation('f8');seed.wait(lambda:number('paused')==1 and number('last_event')>old_event,'真实TF单步事件')
        stepped=snapshot('single-step');assert stepped[14]!=initial[14];q.shot('02-real-cpu-single-step')
        original=v.user_bytes(target(),pulse,1);operation('b',f'{pulse:08X}')
        assert number('have_break')==1 and number('break_address')==pulse and v.user_bytes(target(),pulse,1)==b'\xcc'
        q.shot('03-breakpoint-real-code-byte');old_event=number('last_event');operation('f5')
        seed.wait(lambda:number('paused')==1 and number('last_event')>old_event and context()[14]==pulse,'实际继续/INT3断点命中',180)
        hit=snapshot('break-hit');assert v.user_bytes(target(),pulse,1)==original
        q.shot('04-int3-hit-original-byte-restored');old_event=number('last_event');operation('f8')
        seed.wait(lambda:number('paused')==1 and number('last_event')>old_event,'断点后真实单步并重装0xCC')
        assert v.user_bytes(target(),pulse,1)==b'\xcc';snapshot('break-single-step');operation('u')
        assert not number('have_break') and v.user_bytes(target(),pulse,1)==original
        q.shot('05-unbreak-restores-real-original')
        # M实际解析完整地址，再将128B缓存逐字节与目标映射相比。
        counter_address=target_addresses['target_counter'];operation('m',f'{counter_address:08X}')
        assert number('memory_mode')==1 and number('memory_address')==counter_address and number('memory_valid')==1
        assert v.user_bytes(w,addresses['memory_bytes'],128)==v.user_bytes(target(),counter_address,128)
        before=number('memory_address');operation('m',cancel=True);assert number('memory_address')==before
        operation('m','0x'+('1'*65));assert number('memory_address')==before and text('status')==b'Invalid hexadecimal address'
        operation('m','DEADBEEF');assert not number('memory_valid');q.shot('06-complete-address-reject-and-unmapped')
        operation('i');assert not number('memory_mode');operation('r');assert number('register_mode')==1;operation('i')
        # 真实继续后目标开窗口；通过实际任务栏回调试器再F4暂停。
        operation('f5');seed.wait(lambda:compiler.file_content(disk,'HOME/DEBUG.OK')==b'LIVE' and len(t.windows())==3,'实际Continue运行到目标窗口/文件')
        handles=sorted(win['handle'] for win in t.windows());bw=max(40,min(140,(1024-240)//len(handles)))
        t.point(116+bw*handles.index(w['handle'])+bw//2,752);t.click();seed.wait(lambda:number('ui_focus')==1,'实际任务栏返回调试器')
        operation('f4');seed.wait(lambda:number('paused')==number('have_context')==1,'公开DEBUG暂停真实目标');snapshot('manual-pause')
        observed=struct.unpack('<I',v.user_bytes(target(),counter_address,4))[0];assert observed>0
        time.sleep(.3);assert struct.unpack('<I',v.user_bytes(target(),counter_address,4))[0]==observed,'暂停目标仍在运行'
        q.shot('07-actual-continue-and-pause')
        # 暂停目标静止约两秒，刷新计数应保持公开100tick策略，
        # 不用宿主CPU百分比推测查询量，也不把人工暂停当优化。
        start_tick=t.word(kernel['sc_ticks']);start_count=number('debug_refreshes')
        seed.wait(lambda:t.word(kernel['sc_ticks'])-start_tick>=210,'实际客体PIT静止窗口')
        idle_ticks=t.word(kernel['sc_ticks'])-start_tick;refreshes=number('debug_refreshes')-start_count;assert 1<=refreshes<=3
        operation('f2');seed.wait(lambda:number('paused')==number('have_context')==1 and context()[14]==0x400000 and len(t.windows())==2,'真实Restart结束旧目标并首指令暂停新目标')
        snapshot('restart');q.shot('08-restart-new-real-target')
        checks.extend(('首指令88B现场与内核saved_esp原帧一致；真实TF单步/INT3命中/字节恢复/重装/撤除','完整地址/取消/超长拒绝/128B实际目标内存/未映射失败；Code/Regs保留键盘','真正Continue执行目标窗口与文件，F4暂停计数停止，Restart替换专属目标；静止PIT查询节流'))
        q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'调试器/新旧专属目标/所有私有页严格回收')
        q.shot('09-all-owned-targets-reclaimed');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,native=artifacts,contexts=contexts,checks=checks,overflow=overflow,
            idle_refreshes=dict(pit_ticks=idle_ticks,refreshes=refreshes),limits='独立新版Debugger1024/100真实核心，正式预装未替换；十九布局/鼠标/首字/未知指令/自然退出与PID复用/Studio和Files当前分派另测，不代表全M8')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:
            if w is not None and addresses is not None:
                with t.stable_frame():
                    state={name:number(name) for name in ('target_pid','target_generation','paused','have_context','have_break','last_event','memory_address','memory_valid','debug_rows','ui_modal','debug_operations')}
                    state.update(registers=q.hmp('info registers'));(OUT/'failure-state.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
