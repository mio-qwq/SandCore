#!/usr/bin/env python3
"""mio：同一实际G2调试器/目标的128B映射边界独立只读取证。

旧核心在数据尾部读取128B失败，先核对真实目标PDE/PTE，再用
正常M输入比较未映射尾页拒绝和完整映射窗口成功。无客体内存
写入、不修改SCX/BSS、DEBUG权限或读长度；PASS仅属于取证。
"""
import hashlib,json,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler;theme=seed.theme
HISTORY=ROOT/'build/m8-debugger-history/before-memory-range-test'
OUT=ROOT/'build/m8-debugger-draft/range-observer'


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    inputs={name:sha(ROOT/name) for name in ('build/sandcore.img','build/kernel.elf','build/kernel.sym','build/sanddata.img',
        'build/m8-next/debugger.c','user/SCAPI.H','user/NUI.inc','kernel/font16.txt')};verifier=sha(Path(__file__))
    assert sha(ROOT/'build/m8-next/debugger.c')==sha(HISTORY/'input-snapshot/debugger.c')
    native=(HISTORY/'debugger-native.scx').read_bytes();mapping=(HISTORY/'debugger-native.map').read_bytes()
    target=(HISTORY/'target-native.scx').read_bytes();target_mapping=(HISTORY/'target-native.map').read_bytes()
    counter=theme.native_symbols(target_mapping)['target_counter'];aligned=counter&~127
    def prepare(disk):
        compiler.disk_put(disk,'HOME/DBG.SCX',native);compiler.disk_put(disk,'HOME/TARGET.SCX',target)
        compiler.disk_put(disk,'HOME/TARGET.SCX.map',target_mapping)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    v.OUT=OUT;v.bind();kernel=q.symbols();addresses=theme.native_symbols(mapping);proc=t.launch('std',128,'debugger-range',prepare)
    try:
        t.open_shell(True);v.idle();baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/DBG.SCX HOME/TARGET.SCX\n');w=seed.wait(theme.window,'同一真实G2调试器与目标')
        def number(name):return theme.user_word(w,addresses[name])
        seed.wait(lambda:number('paused')==number('have_context')==1 and number('ui_frames')>=2,'首指令真实暂停')
        with t.stable_frame():
            pid=number('target_pid');pd=t.word(kernel['tasks']+pid*168);pde=t.word(pd+(counter>>22)*4)
            table=q.memory(pde&~4095,4096);pte=struct.unpack_from('<I',table,((counter>>12)&1023)*4)[0]
            end_pte=struct.unpack_from('<I',table,(((counter+127)>>12)&1023)*4)[0]
            assert pde&5==5 and pte&5==5 and not end_pte&1
            actual_counter=v.user_bytes(dict(owner=pid),counter,4)
            initial=v.user_bytes(w,addresses['context'],88);assert struct.unpack_from('<I',initial,56)[0]==0x400000
        observations=[]
        for label,address,valid in (('01-full-128-crosses-unmapped-tail',counter,0),('02-aligned-full-128-is-actually-mapped',aligned,1)):
            operations=number('debug_operations');q.key('m');seed.wait(lambda:number('ui_modal')==1,'实际Memory地址输入')
            q.text(f'{address:08X}\n');seed.wait(lambda:number('ui_modal')==0 and number('debug_operations')>operations,'正常完整地址提交')
            assert number('memory_address')==address and number('memory_valid')==valid and number('memory_mode')==1
            with t.stable_frame():
                assert v.user_bytes(w,addresses['context'],88)==initial
                if valid:
                    cached=v.user_bytes(w,addresses['memory_bytes'],128);assert cached==v.user_bytes(dict(owner=pid),address,128)
                    assert cached[counter-aligned:counter-aligned+4]==actual_counter
                observations.append(dict(address=address,length=128,valid=bool(valid)))
            old=number('ui_frames');seed.wait(lambda:number('ui_frames')>old and t.word(kernel['dirty'])==0,'真实Memory画面');q.shot(label)
        q.key('esc');v.idle();seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'独立取证全页回收')
        q.shot('03-all-owned-pages-reclaimed');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and verifier==sha(Path(__file__))
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,
            debugger_sha256=hashlib.sha256(native).hexdigest(),target_sha256=hashlib.sha256(target).hexdigest(),
            counter_address=counter,aligned_address=aligned,target_pd=pd,pde=pde,first_pte=pte,last_pte=end_pte,
            page_table_sha256=hashlib.sha256(table).hexdigest(),observations=observations,overflow=overflow,
            limits='仅证明旧128B内存测试所选范围跨真实未映射页；保持全128B映射要求，不是核心/十九布局/全M8通过')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
