#!/usr/bin/env python3
"""mio：体素发光保守索引的真实G2同源前后像素/成本对照。

这里只验证着色器的确定性夹具，并明确区别于玩家完成远征流程。
修改世界由普通三环夹具按公开源码执行；没有HMP写内存/寄存器、
调用客体函数或宿主预渲图。一次编译后OLD/NEW交替三轮，保留原始
PIT耗时和完整画面，阴影缓存是否正确须以像素相等判定。
"""
import argparse
import json
import shutil
import statistics
import struct
import time
import traceback
import zipfile
import verify_m8_phase2 as phase
import verify_canvas as canvas

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler
W=128;H=96;STAGES=6
SOURCE=r'''/* mio：体素着色器夹具，不是任务完成/游戏存档。
 * 为相同初始世界施加真实材料修改和光照条件，独立原生产物实际
 * 执行原世界函数；照片由每像素DDA/遮挡/反射/透射生成。 */
#define main world_program_main
#include "@WORLD@"
#undef main
static volatile int light_probe[16];
static u32 light_blob[16+6*128*96+16];
int main(void)
{
    ui_win=sc_open_rgb("Voxel lighting / mio",408,226);
    if(ui_win<0)return 1;
    ui_pixels=(u32 *)sc_alloc(1920u*1080u*4u);if(!ui_pixels)return 2;
    ui_geometry();world_panel=0;generate();prepare_materials();
    u8 *magic=(u8 *)light_blob;char *signature="WPIX1MIO";
    for(int i=0;i<8;i++)magic[i]=(u8)signature[i];
    light_blob[2]=1;light_blob[3]=128;light_blob[4]=96;light_blob[5]=6;
    for(int i=0;i<ui_width*ui_height;i++)ui_pixels[i]=0xFF000000u|SCN_SHADE;
    for(int i=0;i<16;i++)light_blob[16+6*128*96+i]=0x534D494Fu;
    light_probe[0]=1;light_probe[2]=ui_win;
    ScnVec eye;scn_set(&eye,px,py,pz);
    for(int stage=0;stage<6;stage++){
        if(stage==1)world_daylight=48;
        if(stage==2){world_put(11,9,17,12);world_lighting_generation++;}
        if(stage==3){world_put(11,9,18,3);world_lighting_generation++;}
        if(stage==4){world_put(11,9,17,0);world_put(11,9,18,0);world_lighting_generation++;}
        if(stage==5){world_sites|=1;world_lighting_generation++;
            scn_set(&eye,site_x[0]*256+128,(site_y[0]+2)*256+128,(site_z[0]+3)*256+128);}
        int begin=sc_tick();u32 *pixels=light_blob+16+stage*128*96;
        for(int y=0;y<96;y++)for(int x=0;x<128;x++){
            ScnVec ray;scn_set(&ray,2*x+1-128,(96-2*y-1)/2-22,-96);
            pixels[y*128+x]=0xFF000000u|world_trace(&eye,&ray);
        }
        light_blob[8+stage]=(u32)(sc_tick()-begin);
        int left=(stage%3)*128,top=(stage/3)*96;
        for(int y=0;y<96;y++)for(int x=0;x<128;x++)
            ui_pixels[(top+y)*ui_width+left+x]=pixels[y*128+x];
        sc_frame32(ui_win,ui_pixels,(u32)(ui_width*ui_height*4));
        light_probe[1]=stage+1;sc_yield();
    }
    light_probe[3]=sc_write("HOME/@NAME@.BIN",light_blob,(int)sizeof(light_blob));
    light_probe[4]=(int)ui_pixels;light_probe[5]=ui_width;light_probe[6]=ui_height;
    light_probe[0]=2;
    while(sc_key()!=27)sc_yield();sc_free(ui_pixels);return 0;
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--compiler-stage',required=True)
    parser.add_argument('--baseline-world',help='相同共享库下的明确旧world.c；不传时使用原冻结输入')
    parser.add_argument('--baseline-game',help='旧主文件对应的world_game.inc，保留批量修改/索引失效前的传递输入')
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=out;c.v.windows=t.windows
    stage=ROOT/args.compiler_stage
    proof=json.loads((stage/'results.json').read_text(encoding='utf-8'))
    assert proof['status']=='PASS' and proof['scope']=='native'
    g2=(stage/'g2.scx').read_bytes();assert g2==(stage/'g3.scx').read_bytes()
    if args.baseline_world:old=(ROOT/args.baseline_world).read_bytes()
    else:
        with zipfile.ZipFile(ROOT/'build/m8-phase2-resume-20261003-01/source-inputs.zip') as archive:
            old=archive.read('sandcore/user/world.c')
    old_original=old;old_game=None
    if args.baseline_game:
        # 主文件包含私有玩法片段。若最新片段增加了新的空间索引维护，
        # 不能让旧主文件也偷偷采用新维护或因缺声明失败；冻结自己的
        # 对应片段，仅改include文件名以便在同一盘共存，语义保持旧版。
        old_game=(ROOT/args.baseline_game).read_bytes()
        assert b'#include "world_game.inc"' in old
        old=old.replace(b'#include "world_game.inc"',b'#include "WGOLD.inc"')
    new=(ROOT/'user/world.c').read_bytes()
    sources={name:SOURCE.replace('@WORLD@','W'+name+'.inc').replace('@NAME@','W'+name).encode('utf-8') for name in ('OLD','NEW')}
    for name,blob in [('WOLD.inc',old),('WNEW.inc',new)]+[(name+'.c',blob) for name,blob in sources.items()]:
        (out/name).write_bytes(blob)
    if old_game is not None:(out/'WGOLD.inc').write_bytes(old_game)
    # 拷贝包括所有传递include，证据不能只记世界主文件而漏共享库。
    includes={path.name:phase.sha(path.read_bytes()) for path in (ROOT/'user').iterdir() if path.suffix in ('.H','.inc')}
    shutil.copy2(ROOT/'build/sandcore.img',out/'sandcore.img')
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    report=dict(author='mio',status='RUNNING',scope='VOXEL_SHADER_EQUIVALENCE',
        baseline_world=args.baseline_world or 'source-inputs.zip:sandcore/user/world.c',
        inputs={name:phase.sha((ROOT/'build'/name).read_bytes()) for name in
                ('sandcore.img','sanddata.img','kernel.elf','kernel.sym')},
        old_sha256=phase.sha(old),old_original_sha256=phase.sha(old_original),
        old_game_sha256=phase.sha(old_game) if old_game is not None else None,
        new_sha256=phase.sha(new),includes=includes,g2_sha256=phase.sha(g2),rounds=[])
    def prepare(disk):
        c.disk_put(disk,'BIN/G2.SCX',g2)
        if old_game is not None:c.disk_put(disk,'SYS/SRC/WGOLD.inc',old_game)
        for name,blob in [('WOLD.inc',old),('WNEW.inc',new)]+[(name+'.c',blob) for name,blob in sources.items()]:
            c.disk_put(disk,'SYS/SRC/'+name,blob)
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'light',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-light.img'
    try:
        phase.wait(lambda:t.word(q.symbols()['boot_stage'])==2,'welcome');t.open_shell(True);phase.idle()
        baseline=t.word(q.symbols()['pf_used']);addresses={}
        for name in sources:
            native,mapping,_=phase.compile_source('BIN/G2.SCX','SYS/SRC/'+name+'.c','HOME/W'+name+'.SCX')
            (out/(name+'.scx')).write_bytes(native);(out/(name+'.map')).write_bytes(mapping)
            addresses[name]=phase.theme.native_symbols(mapping)['light_probe']
        reference=None
        for round_number in range(3):
            for name in sources:
                q.text('run HOME/W'+name+'.SCX\n')
                window=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'lighting-window')
                pd=t.word(q.symbols()['tasks']+window['owner']*168)
                def state():return struct.unpack('<16i',q.memory(t.physical(pd,addresses[name]),64))
                begin=time.monotonic();phase.wait(lambda:state()[0]==2,'lighting-'+name,240);wall=time.monotonic()-begin
                result=state();blob=c.file_content(phase.DISK,'HOME/W'+name+'.BIN')
                assert blob[:8]==b'WPIX1MIO' and len(blob)==(16+STAGES*W*H+16)*4 and result[3]==len(blob)
                assert struct.unpack_from('<4I',blob,8)==(1,W,H,STAGES)
                assert blob[-64:]==struct.pack('<I',0x534D494F)*16,'lighting crossed tail guard'
                pixels=blob[64:-64]
                if reference is None:reference=pixels
                assert pixels==reference,(name,round_number,'lighting changed full pixel bytes')
                with t.stable_frame():
                    current=t.windows()[-1];assert current['cw']==result[5] and current['ch']==result[6]
                    # 完整私有帧按PTE合并物理段只读一次，完整canvas
                    # 也只读一次。随后所有六图/每行仍逐字节比较，
                    # 避免数千次HMP往返占掉大部分验证时间。
                    full=canvas.user_bytes(current,result[4],result[5]*result[6]*4)
                    assert full==q.memory(current['canvas'],len(full)),'full frame differs from canvas'
                    for scene in range(STAGES):
                        for y in range(H):
                            offset=(((scene//3)*H+y)*result[5]+(scene%3)*W)*4
                            actual=full[offset:offset+W*4]
                            expected=pixels[(scene*W*H+y*W)*4:(scene*W*H+(y+1)*W)*4]
                            assert actual==expected,'private lighting frame differs'
                tag=str(round_number+1)+'-'+name;q.shot(tag+'-lighting');(out/(tag+'.bin')).write_bytes(blob)
                measured=dict(round=round_number+1,variant=name,ticks=list(struct.unpack_from('<6I',blob,32)),wall_seconds=round(wall,4),sha256=phase.sha(pixels))
                report['rounds'].append(measured);print(json.dumps(measured),flush=True)
                (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
                q.key('esc');phase.idle();assert t.word(q.symbols()['pf_used'])==baseline,'lighting leaked pages'
        report['median_total_ticks']={name:statistics.median(sum(item['ticks']) for item in report['rounds'] if item['variant']==name) for name in sources}
        report.update(status='PASS',limitations='六张128x96着色器夹具，三轮交替同库/核；不是真实远征通关、高清游戏或动画帧率验收')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print('VOXEL LIGHT INDEX PASS',flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
