#!/usr/bin/env python3
"""mio：VGA短点击/按住输入的只读定位；不作为完整验收PASS。

使用同一已经生成的G2 Canvas。每步记录实际鼠标、应用布局/
输入/动作；只通过QMP按下释放，不修改ui_pressed/dirty等客体
状态。区分测试器坐标、排队边沿和应用笔画逻辑之后再决定修复。
"""
import json,struct,time
from pathlib import Path
import verify_canvas as s

ROOT=s.ROOT
OUT=ROOT/'build/m8-canvas-history/vga-input-observation'
s.OUT=OUT;s.bind();t=s.t;q=s.q;compiler=s.compiler;theme=s.theme


def main():
    native=(ROOT/'build/m8-canvas/canvas-native.scx').read_bytes()
    addresses=theme.native_symbols((ROOT/'build/m8-canvas/canvas-native.map').read_bytes())
    def prepare(disk):
        compiler.disk_put(disk,'HOME/CAN.SCX',native)
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        compiler.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('cirrus',128,'canvas-observe',prepare);observations=[]
    try:
        t.open_shell(False);s.idle();q.text('run HOME/CAN.SCX\n');w=s.wait(theme.window,'Canvas真实启动')
        def value(name):return theme.user_word(w,addresses[name])
        s.wait(lambda:value('ui_frames')>=2,'实际布局')
        def observe(label):
            with t.stable_frame():
                values={name:value(name) for name in ('ui_width','ui_height','ui_scale','UI_W','UI_H','ui_frames',
                    'ui_x','ui_y','ui_buttons','ui_pressed','ui_released','ui_focus','ui_action',
                    'menu_open','menu_x','menu_y','paper_x','paper_y','paper_w','paper_h','brush','dirty','drawing')}
                values.update(label=label,mx=t.word(q.symbols()['mx']),my=t.word(q.symbols()['my']),windows=t.windows())
            observations.append(values);(OUT/'observations.json').write_text(json.dumps(observations,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            print(json.dumps(values,ensure_ascii=False),flush=True);q.shot(label)
        def point(x,y):
            current=[win for win in t.windows() if win['handle']==w['handle']][0];scale=value('ui_scale')
            t.point(current['x']+1+x*scale//100,current['y']+current['h']-current['ch']-1+y*scale//100)
        observe('01-initial');point(210,value('tools_y')+10);t.click();s.wait(lambda:value('menu_open')==1,'工具菜单')
        observe('02-tools');point(value('menu_x')+24,value('menu_y')+8+26+10);t.click()
        s.wait(lambda:value('brush')==6 and value('menu_open')==0,'画笔半径6');observe('03-brush-selected')
        x=value('paper_x')+value('paper_w')//2;y=value('paper_y')+value('paper_h')//2
        point(x,y);observe('04-pointer-on-paper');t.click();time.sleep(1.5);observe('05-short-click')
        point(x,y);q.button(True);time.sleep(.35);observe('06-held-down');q.button(False);time.sleep(.5);observe('07-held-released')
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
