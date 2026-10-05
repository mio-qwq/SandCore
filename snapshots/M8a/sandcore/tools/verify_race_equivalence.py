#!/usr/bin/env python3
"""mio：真实三环赛车的全客户区、三赛道、同画质旧新对照。

夹具include真正race.c，使用相同真实车辆/场景/路面/HUD绘制；固定
里程、方向、五对手和状态，避免帧率不同使仿真场景不同。旧新公共
库分别冻结，由同一系统内G2真正编译，三轮反向交替运行。成本仅
取draw内部PIT，文件复制/写盘/观察分开；不将固定场景视为通关。
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import struct
import time
import traceback
import zipfile

import verify_m8_phase2 as phase

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler
DIAGNOSTICS=r'''/* mio：只包装应用随后调用的真实库入口。库实现已由SCENENUI
 * 加载；包装体内先调用原入口，再定义别名，避免递归替换。把包装
 * 插在真实应用的include之后，不能为观察错误再次include SCENE，
 * 否则无实现保护的.inc会重复定义全部场景状态，编译失败。
 * 每次reset前保留上一批错误，防止后来的物件清场掩盖越界。 */
static int fixture_errors,fixture_note_count,fixture_batch;
static int fixture_notes[1024];
/* 每个物件保留首次错误的实际参数，不清除真实场景error。注入
 * 只观察应用入口，不修改库返回值/射线或画布；近地平线阴影
 * 与添加/准备错误必须区分，避免凭最终reset后零值宣称正确。 */
static void fixture_note(int kind,ScnVec *origin,ScnVec *direction,int limit){
    if(fixture_note_count>=64)return;
    int *row=fixture_notes+fixture_note_count*16;fixture_note_count++;
    row[0]=kind;row[1]=fixture_batch;row[2]=race_track;row[3]=scn_active->error;
    row[4]=origin?origin->x:0;row[5]=origin?origin->y:0;row[6]=origin?origin->z:0;
    row[7]=direction?direction->x:0;row[8]=direction?direction->y:0;row[9]=direction?direction->z:0;
    row[10]=limit;row[11]=scn_count;
}
static void fixture_reset(void){
    if(scn_active->error){fixture_errors++;fixture_note(1,0,0,0);}
    scn_reset();fixture_batch++;
}
static int fixture_prepare(ScnVec *eye,ScnExact *work,int capacity){
    int status=scn_prepare_exact(eye,work,capacity);
    if(status){fixture_errors++;fixture_note(2,eye,0,capacity);}return status;
}
static void fixture_intersect(ScnVec *eye,ScnVec *ray,ScnHit *hit,int limit){
    int before=scn_active->error;scn_intersect(eye,ray,hit,limit);
    if(!before&&scn_active->error)fixture_note(3,eye,ray,limit);
}
static int fixture_occluded(ScnVec *eye,ScnVec *ray,int limit){
    int before=scn_active->error;int hit=scn_occluded(eye,ray,limit);
    if(!before&&scn_active->error)fixture_note(4,eye,ray,limit);return hit;
}
static u32 fixture_trace(ScnVec *eye,ScnVec *ray){
    int before=scn_active->error;u32 color=scn_trace(eye,ray);
    if(!before&&scn_active->error)fixture_note(5,eye,ray,0);return color;
}
#define scn_reset fixture_reset
#define scn_prepare_exact fixture_prepare
#define scn_intersect fixture_intersect
#define scn_occluded fixture_occluded
#define scn_trace fixture_trace
'''
SOURCE=r'''/* mio：只读场景输入，真实三环渲染；不灌宿主预算像素。 */
#define main race_original_main
#include "RACE.inc"
#undef main
#undef scn_reset
#undef scn_prepare_exact
static volatile int race_probe[16];
int main(void){
    if(ui_open("RACE full pixels / mio")<0)return 1;
    int width=ui_width,height=ui_height,body=width*height*4*3,bytes=64+body+64;
    u32 *output=sc_alloc((u32)bytes);if(!output)return 2;
    for(int i=0;i<16;i++){output[i]=0;output[(64+body)/4+i]=0x534D494Fu;}
    textures();ui_x=ui_y=-100;ui_buttons=0;ui_focus=1;
    for(int scene=0;scene<3;scene++){
        race_track=scene;race_legacy=0;race_mode=2;race_menu=race_finished=0;
        race_restart();distance=12000+scene*11250;steer=scene==0?-40:scene==1?70:15;
        speed=48;lap=0;lap_start=0;race_elapsed=750;race_place=3;race_countdown=0;
        race_boost=750;race_condition=920;race_drift=race_boosting=race_pit=0;
        for(int i=0;i<RACE_RIVALS;i++)rival_distance[i]=distance+400+i*460;
        int began=sc_tick();draw();int elapsed=sc_tick()-began;
        if(scn_active->error)fixture_errors++;
        output[8+scene]=(u32)elapsed;race_probe[4+scene]=elapsed;
        for(int i=0;i<width*height;i++)output[16+scene*width*height+i]=ui_pixels[i];
        ui_present();
    }
    char *magic="RFRM1MIO";for(int i=0;i<8;i++)((u8 *)output)[i]=(u8)magic[i];
    output[2]=1;output[3]=(u32)width;output[4]=(u32)height;output[5]=3;
    output[6]=(u32)fixture_errors;output[7]=(u32)body;
    race_probe[1]=sc_write("@OUTPUT@",output,bytes);race_probe[2]=width;race_probe[3]=height;
    race_probe[7]=fixture_errors;race_probe[8]=(int)ui_pixels;
    race_probe[9]=sc_write("@NOTES@",fixture_notes,fixture_note_count*64);
    race_probe[0]=2;while(sc_key()!=27)sc_yield();sc_free(output);return fixture_errors;
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--compiler-stage',required=True)
    parser.add_argument('--baseline',required=True,help='修改前source.zip，含旧SCENE/SCWIDE和真实race.c')
    parser.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    parser.add_argument('--pixel-reference',help='已冻结真实旧画面BIN；比较当前应用修复是否改变任一客户像素')
    args=parser.parse_args();os.environ['SANDCORE_QEMU_ACCEL']=args.accel
    out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    (out/'verifier.py').write_bytes(Path(__file__).read_bytes())
    phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=out;c.v.windows=t.windows
    stage=ROOT/args.compiler_stage;proof=json.loads((stage/'results.json').read_text(encoding='utf-8'))
    assert proof['status']=='PASS' and proof['scope']=='native'
    g2=(stage/'g2.scx').read_bytes();assert g2==(stage/'g3.scx').read_bytes()
    common={path.name:path.read_bytes() for path in (ROOT/'user').iterdir()
            if path.is_file() and path.suffix in ('.inc','.H','.h')}
    current_race=(ROOT/'user/race.c').read_bytes();old={}
    with zipfile.ZipFile(ROOT/args.baseline) as archive:
        for name in ('SCENE.H','SCENE.inc','SCWIDE.H','SCWIDE.inc','race.c'):
            old[name]=archive.read('user/'+name)
    if not args.pixel_reference:
        assert old['race.c']==current_race,'应用改变须提供实际旧像素参考，不可把库对照冒充应用等价'
    # mio：唯一注入点在应用最后一个原始include之后；旧新均使用
    # 同一包装，不替换渲染/数学/像素。原race.c也单独冻结，便于
    # 审核注入仅增加错误观察而没有更换场景或隐藏失败。
    # 观察包装需要路线身份，插在原全局声明之后、真实首次函数前。
    # 库仍只加载一次；原应用的任何渲染语句都没有替换。
    include_boundary=b'static ScnExact race_prepared[32];\n'
    normalized_race=current_race.replace(b'\r\n',b'\n')
    assert normalized_race.count(include_boundary)==1,'真实应用include边界变化，应审查夹具'
    observed_race=normalized_race.replace(include_boundary,include_boundary+DIAGNOSTICS.encode('utf-8'),1)
    sources={};programs={}
    for kind in ('OLD','NEW'):
        directory=out/kind;directory.mkdir()
        includes=dict(common)
        if kind=='OLD':includes.update({name:blob for name,blob in old.items() if name!='race.c'})
        includes['RACE.inc']=observed_race
        (directory/'race-original.c').write_bytes(current_race)
        for name,blob in includes.items():(directory/name).write_bytes(blob)
        code=SOURCE.replace('@OUTPUT@','HOME/RFRAME-'+kind+'.BIN').replace('@NOTES@','HOME/RNOTE-'+kind+'.BIN').encode('utf-8')
        (directory/'FRAME.C').write_bytes(code);sources[kind]=(includes,code)
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('kernel.elf','kernel.sym','sandcore.img'):shutil.copy2(ROOT/'build'/name,out/name)
    report=dict(author='mio',status='RUNNING',scope='NATIVE_FULL_RACE_PIXEL_EQUIVALENCE',
                requested_accel=args.accel,g2_sha256=phase.sha(g2),baseline=args.baseline,
                application_sha256=phase.sha(current_race),baseline_application_sha256=phase.sha(old['race.c']),
                source_inputs={kind:{name:phase.sha(blob) for name,blob in sources[kind][0].items()} for kind in sources},
                inputs={name:phase.sha((ROOT/'build'/name).read_bytes()) for name in ('kernel.elf','kernel.sym','sandcore.img','sanddata.img')},
                runs=[],programs={},limitations='固定三赛道完整像素/夹具draw成本；非连续驾驶输入/通关/最终FPS验收')
    def prepare(disk):
        c.disk_put(disk,'BIN/G2.SCX',g2)
        for kind,(includes,code) in sources.items():
            for name,blob in includes.items():c.disk_put(disk,'SYS/SRC/'+kind+'/'+name,blob)
            c.disk_put(disk,'SYS/SRC/'+kind+'/FRAME.C',code)
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=640\nheight=480\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'race-equality',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-race-equality.img'
    reference=None
    if args.pixel_reference:
        original=(ROOT/args.pixel_reference).read_bytes()
        assert original[:8]==b'RFRM1MIO' and len(original)==64+590*375*4*3+64,'旧实际客户帧参考格式/尺寸不符'
        assert struct.unpack_from('<4I',original,8)==(1,590,375,3)
        assert original[-64:]==struct.pack('<I',0x534D494F)*16,'旧画面尾护栏损坏'
        reference=original[64:-64]
        (out/'application-reference.bin').write_bytes(original)
        (out/'reference-pixels.bin').write_bytes(reference)
        report['actual_application_reference']=dict(path=args.pixel_reference,sha256=phase.sha(original),
            pixel_sha256=phase.sha(reference),old_geometry_errors=struct.unpack_from('<I',original,24)[0],
            scope='仅旧实际像素参考；旧几何错误是已登记失败，不改称通过')
    try:
        phase.wait(lambda:t.word(kernel['boot_stage'])==2,'welcome',180);t.open_shell(True);phase.idle()
        baseline=t.word(kernel['pf_used'])
        for kind in ('OLD','NEW'):
            native,mapping,seconds=phase.compile_source('BIN/G2.SCX','SYS/SRC/'+kind+'/FRAME.C','HOME/'+kind+'.SCX',1200)
            (out/(kind+'.scx')).write_bytes(native);(out/(kind+'.map')).write_bytes(mapping)
            programs[kind]=phase.theme.native_symbols(mapping)['race_probe']
            report['programs'][kind]=dict(sha256=phase.sha(native),bytes=len(native),compile_seconds=seconds)
        for round_index in range(3):
            for kind in (('OLD','NEW') if round_index%2==0 else ('NEW','OLD')):
                q.text('run HOME/'+kind+'.SCX\n')
                win=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'race-window',120)
                pd=t.word(kernel['tasks']+win['owner']*168);address=programs[kind]
                def state():return struct.unpack('<16i',q.memory(t.physical(pd,address),64))
                phase.wait(lambda:state()[0]==2,'three-full-race-scenes',1200)
                current=state();blob=c.file_content(phase.DISK,'HOME/RFRAME-'+kind+'.BIN')
                width,height=current[2:4];assert (width,height)==(590,375),'原生客户尺寸不符合固定对照'
                body=width*height*4*3
                notes=c.file_content(phase.DISK,'HOME/RNOTE-'+kind+'.BIN')
                assert current[9]==len(notes) and len(notes)%64==0,'真实错误参数记录不完整'
                (out/('%d-%s-notes.bin'%(round_index,kind))).write_bytes(notes)
                report['geometry_notes']=[list(row) for row in struct.iter_unpack('<16i',notes)]
                (out/('%d-%s.bin'%(round_index,kind))).write_bytes(blob)
                assert current[1]==len(blob) and current[7]==0,('写入/中间几何错误',kind,current)
                assert blob[:8]==b'RFRM1MIO' and len(blob)==64+body+64
                assert struct.unpack_from('<6I',blob,8)==(1,width,height,3,0,body)
                assert blob[-64:]==struct.pack('<I',0x534D494F)*16
                pixels=blob[64:-64]
                if reference is None:reference=pixels;(out/'reference-pixels.bin').write_bytes(pixels)
                assert pixels==reference,('全客户区画面改变',round_index,kind)
                with t.stable_frame():
                    window=t.windows()[-1];actual=bytearray();remaining=width*height*4;virtual=current[8]
                    while remaining:
                        count=min(remaining,4096-(virtual&4095));actual+=q.memory(t.physical(pd,virtual),count)
                        virtual+=count;remaining-=count
                    assert bytes(actual)==pixels[-width*height*4:]==q.memory(window['canvas'],len(actual)), '最后完整实际客户帧不符'
                q.shot('%d-%s-three-tracks'%(round_index,kind));(out/('%d-%s.bin'%(round_index,kind))).write_bytes(blob)
                q.key('esc');phase.idle();assert t.word(kernel['pf_used'])==baseline,'race fixture leaked pages'
                report['runs'].append(dict(round=round_index,kind=kind,ticks=list(current[4:7]),
                                          pixel_sha256=phase.sha(pixels),strict_pages='PASS',intermediate_geometry='PASS'))
                (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                print('RACE EQUAL',round_index,kind,current[4:7],flush=True)
        assert all(t.word(kernel[name])==0 for name in ('keyboard_overflow','event_overflow'))
        totals={kind:[sum(row['ticks']) for row in report['runs'] if row['kind']==kind] for kind in ('OLD','NEW')}
        report.update(status='PASS',pixel_sha256=phase.sha(reference),pixels_per_program=590*375*3,
                      median_draw_ticks={kind:statistics.median(values) for kind,values in totals.items()})
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('FULL RACE PIXELS PASS',report['median_draw_ticks'],flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
