#!/usr/bin/env python3
"""mio：精确三角形的独立有理几何参考与真正三环完整结果验证。

参考先用无限精度平面求交，再将交点投影到面积最大的二维平面，
以三个有向面积判断闭集内外；不复制库的缓存重心平面/MUL-ADC算法。
同一输入由历史M7与当前收敛G2分别编译运行，全部命中字段写回私有
数据盘。缓存起点/不同起点分别核对，失败、护栏和回收不靠截图猜测。
影片工作进程可能同时占用宿主CPU；本项只验正确性，不报告性能收益。
"""
import argparse
from collections import Counter
from fractions import Fraction
import json
import math
import os
import random
import shutil
import struct
import traceback
import zipfile
import verify_m8_phase2 as phase

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler
COUNT=1024
SENTINEL=0x13579BDF
M32=(1<<32)-1


def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def cross(a,b):return (a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def trunc(n,d):return (-1 if (n<0)!=(d<0) else 1)*(abs(n)//abs(d))


def unit(n):
    # 法线的整数归一化是公开合同，参考仍用Python无界平方和/isqrt。
    while max(map(abs,n))>8192:n=tuple(trunc(x,2) for x in n)
    size=math.isqrt(sum(x*x for x in n))
    return tuple(trunc(x*256,size) for x in n) if size else (0,0,256)


def reference(triangles,origin,direction,limit):
    result=[0,-2,limit]+[SENTINEL]*6
    if max(map(abs,origin))>65535 or max(map(abs,direction))>8192:
        return result+[0,-1]
    for index,(a,b,cpoint) in enumerate(triangles):
        n=cross(sub(b,a),sub(cpoint,a));denominator=dot(direction,n)
        if not denominator:continue
        parameter=Fraction(dot(sub(a,origin),n),denominator)
        if parameter<=0:continue
        p=tuple(Fraction(o)+parameter*d for o,d in zip(origin,direction))
        omit=max(range(3),key=lambda k:abs(n[k]));axes=[k for k in range(3) if k!=omit]
        def area(v,w):
            return (v[axes[0]]-p[axes[0]])*(w[axes[1]]-p[axes[1]])-(v[axes[1]]-p[axes[1]])*(w[axes[0]]-p[axes[0]])
        sides=[area(a,b),area(b,cpoint),area(cpoint,a)]
        if not (all(v>=0 for v in sides) or all(v<=0 for v in sides)):continue
        fixed=min(2147483647,(parameter.numerator*65536)//parameter.denominator)
        if fixed<=2 or fixed>=result[2]:continue
        normal=unit(n)
        if denominator>0:normal=tuple(-x for x in normal)
        point=[o+trunc(fixed*d,65536) for o,d in zip(origin,direction)]
        result=[1,index,fixed]+point+list(normal)
    return result+[result[0],0]


def fixture():
    rng=random.Random(0x45584143);rows=[];expected=bytearray();coverage=Counter()
    big=((-8192,-8192,8192),(8192,-8192,-8192),(-8192,8192,-8192))
    base=((-512,-256,128),(768,-256,128),(0,768,128))
    tiny=((0,0,0),(1,0,0),(0,1,0))
    for i in range(COUNT):
        if i<64:
            a,b,cp=(big if i<16 else tiny if i<32 else base)
            targets=[a,b,cp,tuple((x+y)//2 for x,y in zip(a,b)),(0,0,128)]
            target=targets[i%len(targets)]
            direction=(0,0,256) if i%3 else (17,-29,513)
            distance=(1,2,8,32)[i%4]
            origin=tuple(x-distance*y for x,y in zip(target,direction))
            if i%13==0:direction=(256,0,0)
            if i%17==0:direction=(0,0,0)
        else:
            # 顶点/边/内部和外部点都由整系数产生，避免浮点制备误差。
            while True:
                a=tuple(rng.randrange(-600,601)*6 for _ in range(3))
                e=tuple(rng.randrange(-120,121)*6 for _ in range(3))
                f=tuple(rng.randrange(-120,121)*6 for _ in range(3))
                if any(cross(e,f)):break
            b=tuple(x+y for x,y in zip(a,e));cp=tuple(x+y for x,y in zip(a,f))
            weights=((1,0,0),(0,1,0),(0,0,1),(1,1,0),(1,1,1),(-1,1,1),(3,-1,-1))
            w=weights[i%len(weights)];total=sum(w)
            target=tuple(sum(w[k]*v[j] for k,v in enumerate((a,b,cp)))//total for j in range(3))
            direction=tuple(rng.randrange(-1024,1025) for _ in range(3))
            if not any(direction):direction=(0,0,256)
            distance=(1,2,4,16,31)[i%5]
            origin=tuple(x-distance*y for x,y in zip(target,direction))
            if i%23==0:direction=sub(b,a)
            if i%29==0:direction=tuple(-x for x in direction)
        triangles=[(a,b,cp),(a,cp,b),tuple((x,y,z+64*(1 if i&1 else -1)) for x,y,z in (a,b,cp))]
        # 极端顶点不能再移出8192，使用反绕同面保留深度同距覆盖。
        if any(max(map(abs,p))>8192 for p in triangles[2]):triangles[2]=(b,cp,a)
        limit=(2,3,65536,65537,131072,2000000,2147483647)[i%7]
        if i%47==0:origin=(65535,-65535,65535)
        if i%53==0:origin=(65536,0,0)
        if i%59==0:direction=(8193,0,0)
        prep=tuple(max(-8192,min(8192,v)) for v in origin)
        answer=reference(triangles,origin,direction,limit)
        coverage['hit' if answer[0] else 'miss']+=1
        coverage['invalid_query' if answer[-1] else 'valid_query']+=1
        coverage['primary_origin' if origin==prep else 'secondary_origin']+=1
        if answer[0]:coverage['index_'+str(answer[1])]+=1
        words=[v for tri in triangles for p in tri for v in p]+list(origin)+list(direction)+[limit]+list(prep)
        assert len(words)==37
        rows.append(struct.pack('<37i',*words));expected+=struct.pack('<22I',*(v&M32 for v in answer*2))
    assert coverage['hit']>100 and coverage['miss']>100 and coverage['invalid_query']>10
    return b'EXIN1MIO'+struct.pack('<6I',1,COUNT,37,22,0,0)+b''.join(rows),bytes(expected),dict(coverage)


SOURCE=r'''/* mio：只读取夹具输入，在真正用户态算几何并写盘。
 * 输出每射线两个完整现场，分别是准备起点与强制不同准备起点；
 * 只把失败计数放头部不足以发现误命中，所以九字段和遮挡/错误
 * 一起保存。未命中预填哨兵，确认库没有乱写未定义点/法线。 */
#include "SCAPI.H"
#include "SCENE.inc"
static ScnScene test_scene,other_scene;
static ScnObject objects[4],others[4];
static struct {u32 head[4];ScnExact data[4];u32 tail[4];} workspace;
static u8 saved_workspace[sizeof(workspace)];
static volatile int exact_probe[8];
static u32 *output;
static int used=8;
static u32 lifecycle_errors;
static void require(int condition,int bit){if(!condition)lifecycle_errors|=1u<<bit;}
static void fill_workspace(void){for(int i=0;i<(int)sizeof(workspace);i++)((u8 *)&workspace)[i]=0xA5;}
static int same_workspace(void){
    for(int i=0;i<(int)sizeof(workspace);i++)if(((u8 *)&workspace)[i]!=saved_workspace[i])return 0;
    return 1;
}
static int guard(void){
    for(int i=0;i<4;i++)if(workspace.head[i]!=0xA5A5A5A5u||workspace.tail[i]!=0xA5A5A5A5u)return 0;
    for(int i=0;i<(int)sizeof(ScnExact);i++)if(((u8 *)&workspace.data[3])[i]!=0xA5)return 0;
    return 1;
}
static void reset_face(void){
    ScnVec a,b,c;scn_bind(&test_scene,objects,4);test_scene.floor=0;
    scn_set(&a,-256,-256,256);scn_set(&b,256,-256,256);scn_set(&c,0,256,256);
    scn_triangle_exact(&a,&b,&c,SCN_GLASS,166);
}
static void lifecycle(void){
    ScnVec origin,ray,a,b,c;ScnHit hit;scn_set(&origin,0,0,0);scn_set(&ray,0,0,256);
    require(sizeof(ScnExact)==92&&sizeof(ScnPrepared)==64&&sizeof(ScnHit)==36&&sizeof(ScnObject)==56&&sizeof(ScnScene)==120,0);
    reset_face();fill_workspace();require(scn_prepare_exact(&origin,workspace.data,4)==0&&scn_exact_valid,1);
    require(scn_triangle_exact(0,&origin,&origin,SCN_GLASS,0)==-1&&test_scene.count==1,2);
    require(scn_triangle_exact(&origin,&origin,&origin,SCN_GLASS,0)==-1&&test_scene.count==1,3);
    scn_set(&a,8193,0,0);require(scn_triangle_exact(&a,&origin,&ray,SCN_GLASS,0)==-1&&test_scene.count==1,4);
    test_scene.count=4;require(scn_triangle_exact(&objects[0].low,&objects[0].high,&objects[0].third,SCN_GLASS,0)==-2&&test_scene.count==4,5);
    test_scene.count=1;
    require(scn_prepare_exact(0,workspace.data,4)==-1&&!scn_exact_valid,6);
    require(scn_prepare_exact(&origin,0,4)==-1&&!scn_exact_valid,7);
    require(scn_prepare_exact(&origin,workspace.data,0)==-1&&!scn_exact_valid,8);
    test_scene.count=3;require(scn_prepare_exact(&origin,workspace.data,2)==-1&&!scn_exact_valid,9);test_scene.count=1;
    require(scn_prepare_exact(&origin,workspace.data,4097)==-1&&!scn_exact_valid,10);
    scn_set(&a,-8193,0,0);require(scn_prepare_exact(&a,workspace.data,4)==-1&&!scn_exact_valid,11);
    // 第二个坏面必须在写第一个面之前被发现，失败保持全部工作区字节。
    objects[1]=objects[0];test_scene.count=2;fill_workspace();
    for(int i=0;i<(int)sizeof(workspace);i++)saved_workspace[i]=((u8 *)&workspace)[i];
    objects[1].third.x=8193;require(scn_prepare_exact(&origin,workspace.data,4)==-1&&same_workspace(),12);
    objects[1]=objects[0];objects[1].third=objects[1].low;
    require(scn_prepare_exact(&origin,workspace.data,4)==-1&&same_workspace()&&!scn_exact_valid,13);
    require(guard(),14);test_scene.count=1;test_scene.error=0;
    require(scn_prepare_exact(&origin,workspace.data,4)==0,15);
    a=origin;a.x=65536;scn_intersect(&a,&ray,&hit,2000000);
    require(!hit.found&&test_scene.error==-1,16);test_scene.error=0;
    // use不会重置另一上下文；缓存有场景身份，切回仍能使用原批次。
    other_scene=test_scene;other_scene.objects=others;other_scene.count=0;other_scene.floor=0;
    scn_use(&other_scene);scn_intersect(&origin,&ray,&hit,2000000);require(!hit.found,17);
    scn_use(&test_scene);scn_intersect(&origin,&ray,&hit,2000000);require(hit.found&&hit.t==65536,18);
    for(int i=0;i<1;i++){objects[i].low.z+=256;objects[i].high.z+=256;objects[i].third.z+=256;}
    require(scn_prepare_exact(&origin,workspace.data,4)==0,19);
    scn_intersect(&origin,&ray,&hit,2000000);require(hit.found&&hit.t==131072,19);
    scn_disable_exact();require(!scn_exact_valid&&!scn_exact,20);
    scn_prepare_exact(&origin,workspace.data,4);scn_box(-1,-1,-1,1,1,1,SCN_METAL,0,0);require(!scn_exact_valid,21);
    scn_prepare_exact(&origin,workspace.data,4);scn_reset();require(!scn_exact_valid&&!test_scene.count,22);
    scn_prepare_exact(&origin,workspace.data,4);scn_bind(&other_scene,others,4);require(!scn_exact_valid,23);
    reset_face();scn_set(&a,0,0,256);scn_set(&b,1,0,256);scn_set(&c,0,1,256);
    scn_reset();test_scene.floor=0;require(scn_triangle_exact(&a,&b,&c,SCN_GLASS,0)==0,24);
    scn_prepare_exact(&origin,workspace.data,4);scn_intersect(&origin,&ray,&hit,65537);
    require(hit.found&&hit.t==65536,24);scn_intersect(&origin,&ray,&hit,65536);require(!hit.found,25);
}
static void query(ScnVec *origin,ScnVec *ray,int limit){
    ScnHit hit;hit.p.x=hit.p.y=hit.p.z=0x13579BDF;hit.n=hit.p;
    test_scene.error=0;scn_intersect(origin,ray,&hit,limit);
    output[used++]=(u32)hit.found;output[used++]=(u32)hit.index;output[used++]=(u32)hit.t;
    output[used++]=(u32)hit.p.x;output[used++]=(u32)hit.p.y;output[used++]=(u32)hit.p.z;
    output[used++]=(u32)hit.n.x;output[used++]=(u32)hit.n.y;output[used++]=(u32)hit.n.z;
    output[used++]=(u32)scn_occluded(origin,ray,limit);output[used++]=(u32)test_scene.error;
}
int main(void){
    int win=sc_open("Exact triangles / mio",310,160);if(win<0)return 1;
    page(win,"Exact geometry","Planes / edges / lifecycle");
    u32 stat[2];if(sc_stat("HOME/EXACT.IN",stat)||stat[1]!=32+1024*148)return 2;
    u8 *input=sc_alloc(stat[1]);if(!input)return 3;
    if(sc_read("HOME/EXACT.IN",input,(int)stat[1])!=(int)stat[1])return 4;
    char *magic="EXIN1MIO";for(int i=0;i<8;i++)if(input[i]!=(u8)magic[i])return 5;
    u32 *head=(u32 *)input;if(head[2]!=1||head[3]!=1024||head[4]!=37||head[5]!=22)return 6;
    int bytes=32+1024*88+64;output=sc_alloc((u32)bytes);if(!output)return 7;
    lifecycle();int errors=0;
    for(int i=0;i<1024;i++){
        int *row=(int *)(input+32)+i*37;ScnVec origin,ray,prep;
        scn_bind(&test_scene,objects,4);test_scene.floor=0;
        for(int j=0;j<3;j++)if(scn_triangle_exact((ScnVec *)(row+j*9),(ScnVec *)(row+j*9+3),(ScnVec *)(row+j*9+6),SCN_GLASS,166)!=j)errors++;
        scn_set(&origin,row[27],row[28],row[29]);scn_set(&ray,row[30],row[31],row[32]);
        scn_set(&prep,row[34],row[35],row[36]);fill_workspace();
        if(scn_prepare_exact(&prep,workspace.data,4))errors++;query(&origin,&ray,row[33]);
        if(!guard())errors++;
        // 改为另一起点准备，强制验证二次射线没有误用主起点平面。
        prep.x=prep.x==8192?prep.x-1:prep.x+1;
        if(scn_prepare_exact(&prep,workspace.data,4))errors++;query(&origin,&ray,row[33]);
        if(!guard())errors++;
        if((i&63)==0)sc_yield();
    }
    for(int i=0;i<16;i++)output[used+i]=0x534D494Fu;
    magic="EXOT1MIO";for(int i=0;i<8;i++)((u8 *)output)[i]=(u8)magic[i];
    output[2]=1;output[3]=1024;output[4]=22;
#if defined(__SCCC_WIDE__)
    output[5]=1;
#else
    output[5]=0;
#endif
    output[6]=lifecycle_errors;output[7]=1024*88;
    exact_probe[4]=(int)output[5];exact_probe[2]=errors;
    exact_probe[1]=sc_write("HOME/EXACT.OUT",output,bytes);
    page(win,errors||lifecycle_errors?"FAIL / exact geometry":"1024 complete ray pairs","Esc: return");
    exact_probe[0]=1;while(sc_key()!=27)sc_yield();
    scn_disable_exact();sc_free(output);sc_free(input);return errors||lifecycle_errors;
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--compiler-stage',required=True)
    parser.add_argument('--accel',choices=('tcg','whpx'),default='tcg')
    args=parser.parse_args();os.environ['SANDCORE_QEMU_ACCEL']=args.accel
    out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=out;c.v.windows=t.windows
    stage=ROOT/args.compiler_stage;proof=json.loads((stage/'results.json').read_text(encoding='utf-8'))
    assert proof['status']=='PASS' and proof['scope']=='native'
    g2=(stage/'g2.scx').read_bytes();assert g2==(stage/'g3.scx').read_bytes()
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:old=archive.read('sandcore/build/fs/bin/s3c.scx')
    data,expected,coverage=fixture();source=SOURCE.encode('utf-8')
    (out/'EXACT.C').write_bytes(source);(out/'EXACT.IN').write_bytes(data);(out/'reference.bin').write_bytes(expected)
    names=('SCENE.H','SCENE.inc','SCWIDE.H','SCWIDE.inc')
    library={name:(ROOT/'user'/name).read_bytes() for name in names}
    for name,blob in library.items():(out/name).write_bytes(blob)
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('sandcore.img','kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    report=dict(author='mio',status='RUNNING',scope='NATIVE_EXACT_TRIANGLE_RATIONAL_REFERENCE',cases=COUNT,
        result_words=COUNT*22,coverage=coverage,compilers={},g2_sha256=phase.sha(g2),requested_accel=args.accel,
        library={name:phase.sha(blob) for name,blob in library.items()},fixture_sha256=phase.sha(source),
        input_sha256=phase.sha(data),reference_sha256=phase.sha(expected),
        inputs={name:phase.sha((ROOT/'build'/name).read_bytes()) for name in ('sandcore.img','sanddata.img','kernel.elf','kernel.sym')})
    (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    def prepare(disk):
        c.disk_put(disk,'BIN/OLD.SCX',old);c.disk_put(disk,'BIN/NEW.SCX',g2)
        c.disk_put(disk,'SYS/SRC/EXACT.C',source);c.disk_put(disk,'HOME/EXACT.IN',data)
        for name,blob in library.items():c.disk_put(disk,'SYS/SRC/'+name,blob)
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'exact',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-exact.img'
    try:
        phase.wait(lambda:t.word(kernel['boot_stage'])==2,'welcome',180);t.open_shell(True);phase.idle()
        baseline=t.word(kernel['pf_used'])
        for name,driver,capability in [('OLD','BIN/OLD.SCX',0),('NEW','BIN/NEW.SCX',1)]:
            native,mapping,seconds=phase.compile_source(driver,'SYS/SRC/EXACT.C','HOME/'+name+'.SCX',1200)
            (out/(name+'.scx')).write_bytes(native);(out/(name+'.map')).write_bytes(mapping)
            address=phase.theme.native_symbols(mapping)['exact_probe'];q.text('run HOME/'+name+'.SCX\n')
            win=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'exact-window',120)
            pd=t.word(kernel['tasks']+win['owner']*168)
            def state():return struct.unpack('<8i',q.memory(t.physical(pd,address),32))
            phase.wait(lambda:state()[0]==1,'exact-complete',600)
            blob=c.file_content(phase.DISK,'HOME/EXACT.OUT');(out/(name+'.bin')).write_bytes(blob)
            assert state()[1]==len(blob) and state()[2]==0,(name,'output/guard errors',state())
            assert state()[4]==capability and blob[:8]==b'EXOT1MIO'
            header=struct.unpack_from('<6I',blob,8)
            assert header==(1,COUNT,22,capability,0,len(expected)),(name,'lifecycle/header',header)
            assert len(blob)==32+len(expected)+64 and blob[-64:]==struct.pack('<I',0x534D494F)*16
            if blob[32:-64]!=expected:
                offset=next(i for i,(a,b) in enumerate(zip(blob[32:-64],expected)) if a!=b)
                index=offset//88;actual=struct.unpack_from('<22i',blob,32+index*88)
                wanted=struct.unpack_from('<22i',expected,index*88)
                raise AssertionError((name,'geometry mismatch',index,actual,wanted))
            q.shot(name+'-exact-values');q.key('esc');phase.idle()
            assert t.word(kernel['pf_used'])==baseline,'exact leaked pages'
            report['compilers'][name]=dict(native_sha256=phase.sha(native),bytes=len(native),compile_seconds=seconds,
                output_sha256=phase.sha(blob),capability=capability,strict_pages='PASS',full_geometry='PASS',
                lifecycle_checks=26,workspace_guards='PASS')
            (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        assert all(t.word(kernel[name])==0 for name in ('keyboard_overflow','event_overflow'))
        report.update(status='PASS',limitations='独立几何/缓存生命周期/护栏；非旧路径像素等价、赛车画质或性能达标')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('EXACT TRIANGLES PASS',COUNT,COUNT*22,flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
