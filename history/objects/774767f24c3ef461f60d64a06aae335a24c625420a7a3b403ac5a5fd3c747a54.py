#!/usr/bin/env python3
"""mio：SCENE整数优化的真正原生产物、完整射线画面与精确数值对照。

旧库取本轮修改前不可变源码ZIP；新库取当前正式源，二者由同一已
收敛G2在SandCore内生成。固定六个场景都实际计算每一像素的交点、
阴影和反射，再写独立带MIO魔数的观察文件；验证器不写客体状态。
96×64是数值等价夹具尺寸，绝不把此小图当作游戏/动画画质验收。
"""
import argparse
import json
import math
import os
import random
import shutil
import struct
import time
import traceback
import zipfile
from pathlib import Path
import verify_m8_phase2 as phase

ROOT=phase.ROOT
t=phase.t;q=phase.q;c=phase.compiler
WIDTH=96;HEIGHT=64;SCENES=6


def pairs():
    # 转折点覆盖可直接左移的两档边界；INT_MIN测试幅值与分母的大端。
    values=[0,1,-1,32767,-32767,32768,-32768,8388607,-8388607,
            8388608,-8388608,2147483647,-2147483647,-2147483648]
    result=[(n,d) for n in values for d in (0,1,-1,257,-513,2147483647,-2147483648)]
    rng=random.Random(0x53434e)
    result += [(rng.randrange(-2147483648,2147483648),rng.randrange(-2147483648,2147483648)) for _ in range(80)]
    return result


def products():
    # Python无限精度独立计算截零结果，覆盖快路径两端及超界拆分；
    # 限制最终Q8乘积在原场景整数合同内，不拿有符号溢出当期望值。
    bounds=(-65536,-32769,-32768,-32767,-257,-256,-1,0,1,255,256,32767,32768,65536)
    result=[(a,b) for a in bounds for b in bounds]
    rng=random.Random(0x534d4154)
    result += [(rng.randrange(-1048576,1048577),rng.randrange(-8192,8193)) for _ in range(320)]
    result += [(-2147483648,0),(-2147483648,1),(2147483647,-1)]
    return [(a,b,(-1 if (a<0)!=(b<0) else 1)*(abs(a*b)//256)) for a,b in result]


def roots():
    values={0,1,2,3,4294967295}
    for root in (2,3,7,15,16,31,63,127,255,256,511,1023,4095,8192,16384,32767,65535,65536):
        for delta in (-1,0,1):
            n=root*root+delta
            if 0<=n<=4294967295:values.add(n)
    rng=random.Random(0x53515254)
    values.update(rng.randrange(4294967296) for _ in range(512))
    return [(value,math.isqrt(value)) for value in sorted(values)]


SOURCE=r'''/* mio：普通三环射线夹具；结果不是内核API，也不是游戏存档。
 * 同一像素布局和光照顺序可比较太阳缓存失效、上下文切回及垂直相机。
 * 观察数组仅便于只读定位，完成条件还要真实IDE文件和画布逐字节相等。 */
#include "SCAPI.H"
#include "@LIB@"
#define PROBE_W 96
#define PROBE_H 64
#define PROBE_PAIRS @COUNT@
#define PROBE_HEAD (12+PROBE_PAIRS*2)
static int probe_pairs[PROBE_PAIRS][2]={@PAIRS@};
static int product_cases[@PRODUCT_COUNT@][3]={@PRODUCTS@};
static u32 root_cases[@ROOT_COUNT@][2]={@ROOTS@};
static u32 probe_blob[PROBE_HEAD+6*PROBE_W*PROBE_H+16];
static ScnScene scene_a,scene_b;
static ScnObject objects_a[16],objects_b[16];
static volatile int scene_probe[16];
#ifdef SCN_HAS_PREPARED_PRIMARY
static ScnPrepared prepared[17];
static int check_queries(void)
{
    int errors=0;
    for(int i=0;i<160;i++){
        ScnVec eye,ray;eye=scn_eye;
        if(i&1){eye.x+=37;eye.y-=19;eye.z+=11;}
        scn_set(&ray,(i*71)%1024-512,(i*43)%600-300,256+(i*29)%512);
        ScnHit cached,plain;int enabled=scn_prepared_valid;
        scn_intersect(&eye,&ray,&cached,2000000);
        if(scn_occluded(&eye,&ray,2000000)!=cached.found)errors++;
        scn_prepared_valid=0;scn_intersect(&eye,&ray,&plain,2000000);
        scn_prepared_valid=enabled;
        if(cached.found!=plain.found||cached.index!=plain.index||cached.t!=plain.t)errors++;
        if(cached.found&&(cached.p.x!=plain.p.x||cached.p.y!=plain.p.y||cached.p.z!=plain.p.z
            ||cached.n.x!=plain.n.x||cached.n.y!=plain.n.y||cached.n.z!=plain.n.z))errors++;
    }
    return errors;
}
#endif
static void prepare(ScnScene *scene,ScnObject *objects)
{
    scn_bind(scene,objects,16);
    scn_sphere(-160,150,350,140,SCN_METAL,190,1);
    scn_box(100,0,320,290,270,530,SCN_OAK,50,2);
    ScnVec a,b,c;
    scn_set(&a,-440,0,620);scn_set(&b,360,0,650);scn_set(&c,-60,430,660);
    scn_triangle(&a,&b,&c,SCN_PAINT,72);
    scn_camera(0,230,-500,0,120,410);
}
int main(void)
{
    int win=sc_open_rgb("SCENE / mio",310,170),info[5];
    if(win<0||sc_info(win,info))return 1;
    int width=info[2],height=info[3];
    u32 *frame=(u32 *)sc_alloc((u32)(width*height*4));
    if(!frame)return 2;
    for(int i=0;i<width*height;i++)frame[i]=0xFF000000u|SCN_SHADE;
    sc_frame32(win,frame,(u32)(width*height*4));
    scene_probe[0]=1;
    u8 *magic=(u8 *)probe_blob;char *signature="SRAY1MIO";
    for(int i=0;i<8;i++)magic[i]=(u8)signature[i];
    probe_blob[2]=1;probe_blob[3]=PROBE_W;probe_blob[4]=PROBE_H;
    probe_blob[5]=6;probe_blob[6]=PROBE_PAIRS;
    for(int i=0;i<@PRODUCT_COUNT@;i++)
        if(scn_product(product_cases[i][0],product_cases[i][1])!=product_cases[i][2])probe_blob[11]++;
    for(int i=0;i<@ROOT_COUNT@;i++)
        if((u32)scn_isqrt(root_cases[i][0])!=root_cases[i][1])probe_blob[11]++;
    for(int i=0;i<PROBE_PAIRS;i++){
        int n=probe_pairs[i][0],d=probe_pairs[i][1];
        probe_blob[12+2*i]=(u32)scn_ratio(n,d);
        probe_blob[13+2*i]=(u32)scn_ratio16(n,d);
        // 固定位入口同样对照Python参考；通用入口另外保持完全一致。
        if(probe_blob[12+2*i]!=(u32)scn_fraction_ratio(n,d,8))probe_blob[11]++;
        if(probe_blob[13+2*i]!=(u32)scn_fraction_ratio(n,d,16))probe_blob[11]++;
    }
    for(int i=0;i<16;i++)probe_blob[PROBE_HEAD+6*PROBE_W*PROBE_H+i]=0x534D494Fu;
    prepare(&scene_a,objects_a);prepare(&scene_b,objects_b);
    scene_b.floor_color=SCN_SAND;scene_b.sky_top=SCN_SEA;
    scn_set(&scene_b.sun,0,0,0);
    for(int stage=0;stage<6;stage++){
        scn_use(stage==2?&scene_b:&scene_a);
        if(stage==1)scn_set(&scene_a.sun,45,100,-130);
        if(stage==3)scn_set(&scene_a.sun,50000,-16384,32768);
        if(stage==4)scn_camera(0,650,350,0,0,350);
        if(stage==5){scn_camera(0,230,-500,0,120,410);scn_set(&scene_a.sun,-115,205,102);}
#ifdef SCN_HAS_PREPARED_PRIMARY
        u8 *guard=(u8 *)&prepared[16];
        for(int i=0;i<(int)sizeof(ScnPrepared);i++)guard[i]=0xA5;
        if(scn_prepare_primary(&scn_eye,prepared,16))return 5;
        probe_blob[11]+=(u32)check_queries();
        for(int i=0;i<(int)sizeof(ScnPrepared);i++)if(guard[i]!=0xA5)probe_blob[11]++;
#endif
        int begin=sc_tick();
        u32 *pixels=probe_blob+PROBE_HEAD+stage*PROBE_W*PROBE_H;
        // 两个相邻半开批次组合，检验分行时不会留下接缝或漏最后一行。
        if(scn_draw_rows(pixels,PROBE_W,PROBE_H,PROBE_W,74,0,31)
            ||scn_draw_rows(pixels,PROBE_W,PROBE_H,PROBE_W,74,31,PROBE_H))return 3;
        scene_probe[4+stage]=sc_tick()-begin;
        int left=(stage%3)*PROBE_W,top=(stage/3)*PROBE_H;
        for(int y=0;y<PROBE_H;y++)for(int x=0;x<PROBE_W;x++)
            if(left+x<width&&top+y<height)frame[(top+y)*width+left+x]=pixels[y*PROBE_W+x];
        if(sc_frame32(win,frame,(u32)(width*height*4)))return 4;
        scene_probe[1]=stage+1;sc_yield();
    }
    probe_blob[7]=(u32)scn_draw_rows(probe_blob,PROBE_W,PROBE_H,PROBE_W,74,-1,0);
    probe_blob[8]=(u32)scn_draw_rows(probe_blob,PROBE_W,PROBE_H,PROBE_W,74,0,PROBE_H+1);
    probe_blob[9]=(u32)scn_bind(&scene_b,objects_b,0);
    probe_blob[10]=(u32)scn_use(0);
#ifdef SCN_HAS_PREPARED_PRIMARY
    if(scn_prepare_primary(&scn_eye,prepared,1)!=SCN_INVALID||scn_prepared_valid)probe_blob[11]++;
    scn_prepare_primary(&scn_eye,prepared,16);
    if(scn_sphere(500,100,600,40,SCN_SAND,8,0)<0||scn_prepared_valid)probe_blob[11]++;
    probe_blob[11]+=(u32)check_queries();
    scn_prepare_primary(&scn_eye,prepared,16);scn_use(&scene_b);
    probe_blob[11]+=(u32)check_queries();
    scn_reset();if(scn_prepared_valid)probe_blob[11]++;
#endif
    scene_probe[2]=sc_write("@OUTPUT@",probe_blob,(int)sizeof(probe_blob));
    scene_probe[3]=(int)frame;scene_probe[10]=width;scene_probe[11]=height;
    scene_probe[0]=2;
    for(;;){if(sc_key()==27)break;sc_yield();}
    sc_free(frame);return 0;
}
'''


def exact_ratio(n,d,bits):
    if d==0:return -2147483647 if n<0 else 2147483647
    value=min(2147483647,(abs(n)<<bits)//abs(d))
    return -value if (n<0)!=(d<0) else value


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--compiler-stage',required=True)
    parser.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    os.environ['SANDCORE_QEMU_ACCEL']=args.accel
    phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=out;c.v.windows=t.windows
    stage=ROOT/args.compiler_stage
    assert json.loads((stage/'results.json').read_text(encoding='utf-8'))['status']=='PASS'
    g2=(stage/'g2.scx').read_bytes();assert g2==(stage/'g3.scx').read_bytes()
    fixture=pairs();multiplication=products();square_roots=roots()
    with zipfile.ZipFile(ROOT/'build/m8-phase2-resume-20261003-01/source-inputs.zip') as archive:
        old=archive.read('sandcore/user/SCENE.inc').replace(b'"SCENE.H"',b'"OLDSCENE.H"')
        old_header=archive.read('sandcore/user/SCENE.H')
    new=(ROOT/'user/SCENE.inc').read_bytes()
    # 新库可以追加独立源级依赖。全部冻结并明确放进同一私有盘，
    # 避免主文件SHA正确却在客体读到另一批头文件/整数库。
    includes={name:(ROOT/'user'/name).read_bytes() for name in ('SCENE.H','SCWIDE.H','SCWIDE.inc')}
    for name,blob in includes.items():(out/name).write_bytes(blob)
    (out/'verifier.py').write_bytes(Path(__file__).read_bytes())
    sources={}
    for name,library in [('OLD','OLDSCENE.inc'),('NEW','NEWSCENE.inc')]:
        source=SOURCE.replace('@LIB@',library).replace('@COUNT@',str(len(fixture))).replace('@OUTPUT@','HOME/SRAY-'+name+'.BIN')
        def integer(n):return '(-2147483647-1)' if n==-2147483648 else str(n)
        # SCCC有意约束一条逻辑行的token数量，夹具须与普通源码一样
        # 分行书写。不能为测试数据单行过长而偷改编译器公开容量。
        source=source.replace('@PAIRS@',',\n'.join('{'+integer(n)+','+integer(d)+'}' for n,d in fixture))
        source=source.replace('@PRODUCT_COUNT@',str(len(multiplication))).replace('@PRODUCTS@',
            ',\n'.join('{'+','.join(integer(n) for n in row)+'}' for row in multiplication))
        source=source.replace('@ROOT_COUNT@',str(len(square_roots))).replace('@ROOTS@',
            ',\n'.join('{'+str(n)+'u,'+str(expected)+'u}' for n,expected in square_roots))
        sources[name]=source.encode('utf-8');(out/(name+'.c')).write_bytes(sources[name])
    (out/'OLDSCENE.inc').write_bytes(old);(out/'OLDSCENE.H').write_bytes(old_header)
    (out/'NEWSCENE.inc').write_bytes(new)
    shutil.copy2(ROOT/'build/sandcore.img',out/'sandcore.img')
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    report=dict(author='mio',status='RUNNING',scope='SCENE_EQUIVALENCE_ONLY',
                g2_sha256=phase.sha(g2),old_sha256=phase.sha(old),new_sha256=phase.sha(new),
                requested_accel=args.accel,includes={name:phase.sha(blob) for name,blob in includes.items()},
                inputs={name:phase.sha((ROOT/'build'/name).read_bytes()) for name in
                    ('sandcore.img','sanddata.img','kernel.elf','kernel.sym')},cases={})
    def prepare(disk):
        c.disk_put(disk,'BIN/G2.SCX',g2)
        c.disk_put(disk,'HOME/OLDSCENE.inc',old);c.disk_put(disk,'HOME/OLDSCENE.H',old_header)
        c.disk_put(disk,'HOME/NEWSCENE.inc',new)
        for name,blob in includes.items():c.disk_put(disk,'SYS/SRC/'+name,blob)
        for name in sources:c.disk_put(disk,'HOME/'+name+'.c',sources[name])
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'scene',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-scene.img'
    try:
        phase.wait(lambda:t.word(q.symbols()['boot_stage'])==2,'welcome',180);t.open_shell(True);phase.idle()
        baseline=t.word(q.symbols()['pf_used']);images={};numbers={}
        for name in sources:
            native,mapping,seconds=phase.compile_source('BIN/G2.SCX','HOME/'+name+'.c','HOME/'+name+'.SCX')
            (out/(name+'.scx')).write_bytes(native);(out/(name+'.map')).write_bytes(mapping)
            address=phase.theme.native_symbols(mapping)['scene_probe']
            q.text('run HOME/'+name+'.SCX\n')
            window=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'scene-window')
            pd=t.word(q.symbols()['tasks']+window['owner']*168)
            def state():return struct.unpack('<16i',q.memory(t.physical(pd,address),64))
            phase.wait(lambda:state()[0]==2,'six-scenes-'+name,180)
            result=state();blob=c.file_content(phase.DISK,'HOME/SRAY-'+name+'.BIN')
            assert result[2]==len(blob),(name,'write length mismatch',result)
            assert blob[:8]==b'SRAY1MIO' and len(blob)==(12+len(fixture)*2+SCENES*WIDTH*HEIGHT+16)*4
            assert struct.unpack_from('<5I',blob,8)==(1,WIDTH,HEIGHT,SCENES,len(fixture))
            assert struct.unpack_from('<4i',blob,28)==(-1,-1,-1,-1)
            assert struct.unpack_from('<I',blob,44)[0]==0,(name,'prepared/occlusion boundary mismatch')
            assert blob[-64:]==struct.pack('<I',0x534D494F)*16,'render crossed tail guard'
            ratios=struct.unpack_from('<'+str(len(fixture)*2)+'i',blob,48);numbers[name]=ratios
            mismatches=[]
            for index,(n,d) in enumerate(fixture):
                for j,bits in enumerate((8,16)):
                    expected=exact_ratio(n,d,bits);actual=ratios[index*2+j]
                    if actual!=expected:mismatches.append(dict(n=n,d=d,bits=bits,expected=expected,actual=actual))
            pixels=blob[(12+len(fixture)*2)*4:-64];images[name]=pixels
            with t.stable_frame():
                current=t.windows()[-1];assert current['cw']==result[10] and current['ch']==result[11]
                # sc_alloc物理页可能不连续，按页读取完整提交帧再核对canvas。
                actual=bytearray();left=result[10]*result[11]*4;virtual=result[3]
                while left:
                    count=min(left,4096-(virtual&4095));actual+=q.memory(t.physical(pd,virtual),count);virtual+=count;left-=count
                assert bytes(actual)==q.memory(current['canvas'],len(actual))
                for stage_index in range(SCENES):
                    for row in range(HEIGHT):
                        begin=((stage_index//3*HEIGHT+row)*result[10]+stage_index%3*WIDTH)*4
                        expected=pixels[(stage_index*WIDTH*HEIGHT+row*WIDTH)*4:(stage_index*WIDTH*HEIGHT+(row+1)*WIDTH)*4]
                        assert actual[begin:begin+WIDTH*4]==expected,(name,'display differs',stage_index,row)
            q.shot(name+'-six-scenes');(out/(name+'.bin')).write_bytes(blob)
            report['cases'][name]=dict(native_sha256=phase.sha(native),compile_seconds=seconds,
                ticks=list(result[4:10]),pixel_sha256=phase.sha(pixels),ratio_mismatches=mismatches,
                product_reference_cases=len(multiplication),
                isqrt_reference_cases=len(square_roots),
                prepared_queries='PASS' if name=='NEW' and b'SCN_HAS_PREPARED_PRIMARY' in
                    (ROOT/'user/SCENE.H').read_bytes() else 'NOT_PRESENT')
            (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            q.key('esc');phase.idle();assert t.word(q.symbols()['pf_used'])==baseline,'scene leaked pages'
        assert images['OLD']==images['NEW'],'same scene changed complete pixel bytes'
        assert not report['cases']['NEW']['ratio_mismatches'],('integer reference mismatch',report['cases']['NEW']['ratio_mismatches'])
        report.update(status='PASS',limitations='六张96x64数值/整帧等价夹具；不是高分辨率游戏/电影质量或帧率验收')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('SCENE EQUIVALENCE PASS',json.dumps(report['cases']),flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
