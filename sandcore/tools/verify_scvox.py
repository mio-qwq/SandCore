#!/usr/bin/env python3
"""mio：SCVOX空块加速的独立事件参考、真实两代编译与全字段/护栏验证。

Python参考用最小堆合并三条等差事件；不复制客体的块跳跃算法。
客体同时运行未建索引/已建索引两种上下文，完整命中记录与所有
4098项工作区均上盘。编辑、材料互换、删最后实体、批量失效、
重建与非8整倍边界均有实际输入，失败保留完整副本和截图。
"""
import argparse
import heapq
import json
import math
import random
import shutil
import struct
import traceback
import zipfile
import verify_m8_phase2 as phase

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler
CASES=1024
CANARY=0x6C12A0BF
WORK_GUARD=0xA66A
SNAPSHOTS=(0,256,512,768,950)
DIMS=((64,24,64),(24,12,24),(17,9,31),(1,1,1),(128,128,128))


def trunc(n,d):
    return (-1 if (n<0)!=(d<0) else 1)*(abs(n)//abs(d))


def reference(cells,dims,case):
    """沿原192个单元合同取事件，不使用chunk信息或跳跃步数。"""
    w,h,d=dims;origin=case[:3];direction=case[3:6];distance,skip=case[6:]
    pos=[v//256 for v in origin]
    record=[0,0,-1,-1]+[CANARY]*8  # rc、found/index/previous及其余8字段。
    if any(v<0 or v>=size for v,size in zip(pos,dims)):return struct.pack('<12I',*(v&0xFFFFFFFF for v in record))
    length=math.isqrt(sum(v*v for v in direction))
    if not length:return struct.pack('<12I',*(v&0xFFFFFFFF for v in record))
    limit=distance*65536//length;sign=[-1 if v<0 else 1 for v in direction]
    delta=[16777216//abs(v) if v else 0x3FFFFFFF for v in direction]
    events=[]
    for axis,component in enumerate(direction):
        next_t=trunc(((pos[axis]+(sign[axis]>0))*256-origin[axis])*65536,component) if component else 0x3FFFFFFF
        heapq.heappush(events,(next_t,axis))
    current=0;last=-1;normal=[0,0,0]
    for _ in range(192):
        if current>limit or any(v<0 or v>=size for v,size in zip(pos,dims)):break
        index=(pos[1]*d+pos[2])*w+pos[0];kind=cells[index]
        if kind and index!=skip and current>1:
            point=[o+trunc(v*current,65536) for o,v in zip(origin,direction)]
            values=[0,1,index,last,kind,trunc(length*current,65536),*point,*normal]
            return struct.pack('<12I',*(v&0xFFFFFFFF for v in values))
        if not kind:last=index
        current,axis=heapq.heappop(events);pos[axis]+=sign[axis]
        normal=[0,0,0];normal[axis]=-sign[axis]*256
        heapq.heappush(events,(current+delta[axis],axis))
    return struct.pack('<12I',*(v&0xFFFFFFFF for v in record))


def workspace(cells,dims):
    w,h,d=dims;cw=(w+7)//8;cd=(d+7)//8;ch=(h+7)//8
    result=[WORK_GUARD]*4098
    result[1:1+cw*cd*ch]=[0]*(cw*cd*ch)
    for i,kind in enumerate(cells):
        if kind:
            x=i%w;z=(i//w)%d;y=i//(w*d)
            result[1+((y//8)*cd+z//8)*cw+x//8]+=1
    return struct.pack('<4098H',*result)


def inputs():
    rng=random.Random(0x53565831);payload=bytearray();expected=bytearray();coverage=[]
    for dims in DIMS:
        w,h,d=dims;cells=bytearray(w*h*d)
        # 空间稀疏而非全空：实际命中必须跨过空块后仍返回正确前格。
        for i in range(len(cells)):
            x=i%w;z=(i//w)%d;y=i//(w*d)
            if y==0 and (x+z)%11==0:cells[i]=3
            elif x in (7,8,w-2) and z in (7,8,d-2) and y in (h-1,h//2):cells[i]=4 if x&1 else 16
        target=((h//2)*d+d//2)*w+w//2;cells[target]=0
        queries=[]
        for number in range(CASES):
            origin=[rng.randrange(size*256) for size in dims]
            direction=[rng.randrange(-8192,8193) for _ in range(3)]
            if number%5==0:direction[rng.randrange(3)]=0
            if number%7==0:
                # 同距三轴、极小向量与整数体素边界，是块跳跃最易出错处。
                direction=[rng.choice((-256,-1,1,256))]*3
                origin=[rng.randrange(size)*256 for size in dims]
            if number%11==0:
                direction=[0,0,rng.choice((-256,256))]
                origin=[(w//2)*256+128,(h//2)*256+128,rng.randrange(d)*256+128]
            if number%17==0:origin[rng.randrange(3)]=-1
            if number%19==0:direction=[0,0,0]
            distance=rng.choice((1,255,256,8192,10000,16000))
            skip=rng.choice((-1,target,rng.randrange(len(cells))))
            queries.append((*origin,*direction,distance,skip))
        cw=(w+7)//8;ch=(h+7)//8;cd=(d+7)//8
        payload+=struct.pack('<6I',w,h,d,len(cells),CASES,target)+cells
        payload+=bytes((-len(cells))%4)
        payload+=b''.join(struct.pack('<8i',*row) for row in queries)
        for number,case in enumerate(queries):
            if number==256:cells[target]=4
            elif number==512:cells[target]=16
            elif number==768:cells[target]=0
            elif number==900:cells[target]=7
            if number in SNAPSHOTS:expected+=workspace(cells,dims)
            hit=reference(cells,dims,case);expected+=hit+hit
        coverage.append(dict(dimensions=dims,cells=len(cells),chunks=cw*ch*cd,rays=CASES))
    header=b'SVXI1MIO'+struct.pack('<6I',1,len(DIMS),len(DIMS)*CASES,0,0,0)
    return header+payload,bytes(expected),coverage


SOURCE=r'''/* mio：真正读取输入世界，逐项提交全部命中字段和整个工作区。
 * 只做查询验证；不是低清游戏画面，也不以此宣称游戏达到60FPS。 */
#include "SCENE.inc"
#include "SCVOX.inc"
static u16 fast_work[4098],slow_work[4098];
static ScvGrid fast_grid,slow_grid;
static u32 *result;
static int used=8,errors;
static volatile int vox_probe[8];
static void snapshot(void){
    u16 *out=(u16 *)(result+used);
    for(int i=0;i<4098;i++)out[i]=fast_work[i];used+=2049;
}
static void record(ScvGrid *grid,ScnVec *eye,ScnVec *ray,int distance,int skip){
    ScvHit hit;u32 *words=(u32 *)&hit;for(int i=0;i<11;i++)words[i]=0x6C12A0BFu;
    result[used++]=(u32)scv_query(grid,eye,ray,distance,skip,&hit);
    for(int i=0;i<11;i++)result[used++]=words[i];
}
int main(void){
    int win=sc_open_rgb("SCVOX query guards / mio",408,196);if(win<0)return 1;
    sc_fill_rgb(win,0,0,1920,1080,SCN_SHADE);
    sc_text_rgb(win,16,16,"SCVOX / exact voxel queries",SCN_IVORY);
    sc_text_rgb(win,16,48,"Full hit fields / guards / lifecycle",SCN_GLASS);
    u32 stat[2];if(sc_stat("HOME/VOX.IN",stat)||stat[1]<32)return 2;
    u8 *input=sc_alloc(stat[1]);if(!input)return 3;
    if(sc_read("HOME/VOX.IN",input,(int)stat[1])!=(int)stat[1])return 4;
    char *magic="SVXI1MIO";for(int i=0;i<8;i++)if(input[i]!=(u8)magic[i])return 5;
    u32 *header=(u32 *)input;int scenarios=(int)header[3],total=(int)header[4];
    if(header[2]!=1||scenarios!=5||total!=5120)return 6;
    int bytes=32+total*96+scenarios*5*8196+64;
    result=sc_alloc((u32)bytes);if(!result)return 7;
    int at=32;
    for(int scene=0;scene<scenarios;scene++){
        int *meta=(int *)(input+at);int w=meta[0],h=meta[1],d=meta[2],count=meta[3],cases=meta[4],target=meta[5];
        at+=24;u8 *cells=input+at;at+=(count+3)&~3;
        int *queries=(int *)(input+at);at+=cases*32;
        for(int i=0;i<4098;i++){fast_work[i]=0xA66A;slow_work[i]=0xA66A;}
        int capacity=((w+7)/8)*((h+7)/8)*((d+7)/8);
        if(scv_bind(&fast_grid,cells,w,h,d,fast_work+1,capacity)||scv_rebuild(&fast_grid))errors++;
        if(scv_bind(&slow_grid,cells,w,h,d,slow_work+1,capacity))errors++;
        u32 prior[12];for(int i=0;i<12;i++)prior[i]=((u32 *)&fast_grid)[i];
        if(scv_bind(&fast_grid,cells,w,h,d,fast_work+1,capacity-1)!=SCV_INVALID)errors++;
        if(scv_bind(&fast_grid,cells,129,h,d,fast_work+1,4096)!=SCV_INVALID)errors++;
        for(int i=0;i<12;i++)if(prior[i]!=((u32 *)&fast_grid)[i])errors++;
        if(scv_write(&fast_grid,-1,1)!=SCV_INVALID||scv_write(&fast_grid,count,1)!=SCV_INVALID
           ||scv_write(&fast_grid,target,256)!=SCV_INVALID||scv_write(&fast_grid,target,-1)!=SCV_INVALID)errors++;
        for(int number=0;number<cases;number++){
            if(number==256){if(scv_write(&fast_grid,target,4))errors++;}
            if(number==512){if(scv_write(&fast_grid,target,16))errors++;}
            if(number==768){if(scv_write(&fast_grid,target,0))errors++;}
            if(number==900){scv_invalidate(&fast_grid);cells[target]=7;}
            if(number==950){if(scv_rebuild(&fast_grid))errors++;}
            if(number==0||number==256||number==512||number==768||number==950)snapshot();
            int *entry=queries+number*8;ScnVec eye,ray;
            scn_set(&eye,entry[0],entry[1],entry[2]);scn_set(&ray,entry[3],entry[4],entry[5]);
            record(&slow_grid,&eye,&ray,entry[6],entry[7]);
            record(&fast_grid,&eye,&ray,entry[6],entry[7]);
        }
        vox_probe[3]=scene+1;sc_yield();
    }
    if(at!=(int)stat[1]||used*4!=bytes-64)errors++;
    for(int i=0;i<16;i++)result[used+i]=0x534D494Fu;
    char *signature="SVXQ1MIO";for(int i=0;i<8;i++)((u8 *)result)[i]=(u8)signature[i];
    result[2]=1;result[3]=(u32)scenarios;result[4]=(u32)total;result[5]=(u32)errors;
    result[6]=stat[1];result[7]=(u32)(bytes-96);
    vox_probe[1]=sc_write("HOME/VOX.OUT",result,bytes);vox_probe[2]=errors;
    sc_text_rgb(win,16,82,errors?"FAIL / inspect output":"5120 rays / full guards / done",SCN_IVORY);
    vox_probe[0]=1;while(sc_key()!=27)sc_yield();
    sc_free(result);sc_free(input);return errors?1:0;
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--compiler-stage',required=True)
    args=parser.parse_args();out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=out;c.v.windows=t.windows
    stage=ROOT/args.compiler_stage;proof=json.loads((stage/'results.json').read_text(encoding='utf-8'))
    assert proof['status']=='PASS' and proof['scope']=='native'
    g2=(stage/'g2.scx').read_bytes();assert g2==(stage/'g3.scx').read_bytes()
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:old=archive.read('sandcore/build/fs/bin/s3c.scx')
    data,expected,coverage=inputs();source=SOURCE.encode('utf-8')
    (out/'VOX.C').write_bytes(source);(out/'VOX.IN').write_bytes(data);(out/'reference.bin').write_bytes(expected)
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('sandcore.img','kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    report=dict(author='mio',status='RUNNING',scope='SCVOX_NATIVE_QUERY_GUARDS',coverage=coverage,
        rays=len(DIMS)*CASES,query_records=len(DIMS)*CASES*2,workspace_snapshots=len(DIMS)*5,
        expected_bytes=len(expected),cases={},inputs={name:phase.sha((ROOT/'build'/name).read_bytes())
            for name in ('sandcore.img','sanddata.img','kernel.elf','kernel.sym')},
        sources={name:phase.sha((ROOT/'user'/name).read_bytes()) for name in ('SCVOX.H','SCVOX.inc','SCENE.H','SCENE.inc','SCAPI.H')},
        fixture_sha256=phase.sha(source),input_sha256=phase.sha(data),reference_sha256=phase.sha(expected),g2_sha256=phase.sha(g2))
    def prepare(disk):
        c.disk_put(disk,'BIN/OLD.SCX',old);c.disk_put(disk,'BIN/NEW.SCX',g2)
        c.disk_put(disk,'SYS/SRC/VOX.C',source);c.disk_put(disk,'HOME/VOX.IN',data)
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'scvox',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-scvox.img'
    try:
        phase.wait(lambda:t.word(kernel['boot_stage'])==2,'welcome');t.open_shell(True);phase.idle()
        baseline=t.word(kernel['pf_used'])
        for name,driver in [('OLD','BIN/OLD.SCX'),('NEW','BIN/NEW.SCX')]:
            native,mapping,seconds=phase.compile_source(driver,'SYS/SRC/VOX.C','HOME/'+name+'.SCX')
            (out/(name+'.scx')).write_bytes(native);(out/(name+'.map')).write_bytes(mapping)
            address=phase.theme.native_symbols(mapping)['vox_probe'];q.text('run HOME/'+name+'.SCX\n')
            win=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'voxel-window')
            pd=t.word(kernel['tasks']+win['owner']*168)
            def state():return struct.unpack('<8i',q.memory(t.physical(pd,address),32))
            phase.wait(lambda:state()[0]==1,'voxel-results',240)
            blob=c.file_content(phase.DISK,'HOME/VOX.OUT');assert state()[1]==len(blob) and state()[2]==0
            assert blob[:8]==b'SVXQ1MIO' and len(blob)==32+len(expected)+64
            assert struct.unpack_from('<6I',blob,8)==(1,len(DIMS),len(DIMS)*CASES,0,len(data),len(expected))
            assert blob[-64:]==struct.pack('<I',0x534D494F)*16
            if blob[32:-64]!=expected:
                mismatch=next(i for i,(a,b) in enumerate(zip(blob[32:-64],expected)) if a!=b)
                (out/(name+'-mismatch.bin')).write_bytes(blob)
                raise AssertionError((name,'full query/workspace differs',mismatch))
            (out/(name+'.bin')).write_bytes(blob);q.shot(name+'-query-guards')
            q.key('esc');phase.idle();assert t.word(kernel['pf_used'])==baseline,'voxel query leaked pages'
            report['cases'][name]=dict(native_sha256=phase.sha(native),bytes=len(native),compile_seconds=seconds,
                output_sha256=phase.sha(blob),complete_queries='PASS',complete_workspace='PASS',strict_pages='PASS')
            (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        assert all(t.word(kernel[name])==0 for name in ('keyboard_overflow','event_overflow'))
        report.update(status='PASS',limitations='查询/编辑/失效/全部输出与护栏；游戏完整像素、可玩流程、性能另验')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('SCVOX PASS',report['rays'],report['query_records'],len(expected),flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
