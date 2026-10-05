#!/usr/bin/env python3
"""mio：宽整数完整原生输出/别名/进位/饱和和callee保存的独立参考。

参考使用Python无界整数，不复制16位肢体乘法或64位试减代码。
旧M7纯C与新收敛G2的IMUL/ADC快路分别真正编译、运行、完整上盘。
此夹具证明数值/调用约定，不把宿主负载下的PIT当作游戏帧率。
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import struct
import traceback
import zipfile
import verify_m8_phase2 as phase

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler
COUNT=4096
WORDS=32
M32=(1<<32)-1;M64=(1<<64)-1


def signed(n,bits):
    return n-(1<<bits) if n&(1<<(bits-1)) else n


def fixture():
    rng=random.Random(0x53435732)
    ints=(0,1,-1,2,-2,32767,-32768,65535,-65536,2147483647,-2147483648)
    wide=(0,1,M64,1<<63,(1<<63)-1,1<<32,(1<<32)-1,(1<<32)+1,
          (1<<48)-1,1<<48,(-((1<<48)-1))&M64,(-(1<<48))&M64)
    rows=[];expected=bytearray();counters=dict(ratio_ok=0,ratio_saturated=0,ratio_range=0,ratio_zero=0)
    for i in range(COUNT):
        terms=[signed(rng.getrandbits(32),32) for _ in range(6)]
        if i<len(ints)**2:terms[0]=ints[i//len(ints)];terms[1]=ints[i%len(ints)]
        a=wide[i%len(wide)] if i%3 else rng.getrandbits(64)
        b=wide[(i//len(wide))%len(wide)] if i%5 else rng.getrandbits(64)
        if i%7==0:n=rng.getrandbits(64)
        elif i%11==0:n=wide[i%len(wide)]
        else:n=rng.randrange(-(1<<48)+1,1<<48)&M64
        d=wide[(i//7)%len(wide)] if i%3 else rng.getrandbits(64)
        if i%13==0:d=0
        if i%17==0:d=rng.choice(ints)&M64
        words=[value&M32 for value in terms]
        for value in (a,b,n,d):words.extend((value&M32,value>>32))
        rows.append(struct.pack('<14I',*words))
        result=[]
        for value in (terms[0]*terms[1],a+b,a-b,-a,a+b,a-b,-a,
                      terms[0]*terms[3]+terms[1]*terms[4]+terms[2]*terms[5]):
            value&=M64;result.extend((value&M32,value>>32))
        sa,sb=signed(a,64),signed(b,64)
        result.extend(((sa>sb)-(sa<sb),(a>b)-(a<b)))
        sn,sd=signed(n,64),signed(d,64);answer=0
        if not sd:status=-2;counters['ratio_zero']+=1
        elif abs(sn)>(1<<48)-1:status=-1;counters['ratio_range']+=1
        else:
            quotient=(abs(sn)<<16)//abs(sd)
            status=1 if quotient>2147483647 else 0
            counters['ratio_saturated' if status else 'ratio_ok']+=1
            answer=min(2147483647,quotient)*(-1 if (sn<0)!=(sd<0) else 1)
        result.extend((status,answer))
        dot=terms[0]*terms[3]+terms[1]*terms[4]+terms[2]*terms[5]
        # 向量非别名、输出覆盖a/b首两字、覆盖a/b末两字，各独立
        # 使用完整同一数学点积；最后a==b==out验证先读后写合同。
        for value in (dot,dot,dot,dot,dot,sum(v*v for v in terms[:3])):
            value&=M64;result.extend((value&M32,value>>32))
        expected+=struct.pack('<32I',*(v&M32 for v in result))
    data=b'SWIN1MIO'+struct.pack('<6I',1,COUNT,14,0,0,0)+b''.join(rows)
    return data,bytes(expected),counters


SOURCE=r'''/* mio：读取完整双字输入，普通三环执行，不拿宿主结果替代。 */
#include "SCAPI.H"
#include "SCWIDE.inc"
static volatile int wide_probe[8];
static u32 *result;
static int used=8;
static void save(ScwInt *value){result[used++]=value->low;result[used++]=(u32)value->high;}
static void vectors(int *a,int *b,u32 *entry){
    a[0]=b[0]=0x13579BDF;a[4]=b[4]=0x2468ACE0;
    for(int i=0;i<3;i++){a[i+1]=(int)entry[i];b[i+1]=(int)entry[i+3];}
}
static int guards(int *a,int *b){return a[0]!=0x13579BDF||b[0]!=0x13579BDF||a[4]!=0x2468ACE0||b[4]!=0x2468ACE0;}
static int registers(void){
    u32 b,s,d;ScwInt value;int aa[3]={-2147483647,-32768,65535},bb[3]={2147483647,65535,-65536};
    __asm__ __volatile__("mov $0x13579BDF, %%ebx; mov $0x2468ACE0, %%esi; mov $0x01234567, %%edi"
        : : : "ebx","esi","edi","cc");
    scw_dot3(&value,-2147483647,-32768,65535,2147483647,65535,-65536);
    scw_dot3v(&value,aa,bb);
    __asm__ __volatile__("mov %%ebx, %0; mov %%esi, %1; mov %%edi, %2"
        : "=a"(b),"=c"(s),"=d"(d) : : "cc");
    return b!=0x13579BDFu||s!=0x2468ACE0u||d!=0x01234567u;
}
int main(void){
    int win=sc_open("SCWIDE integers / mio",300,160);if(win<0)return 1;
    page(win,"Wide integer verification","Full values / aliases / guards");
    u32 stat[2];if(sc_stat("HOME/WIDE.IN",stat)||stat[1]!=32+4096*56)return 2;
    u8 *input=sc_alloc(stat[1]);if(!input)return 3;
    if(sc_read("HOME/WIDE.IN",input,(int)stat[1])!=(int)stat[1])return 4;
    char *magic="SWIN1MIO";for(int i=0;i<8;i++)if(input[i]!=(u8)magic[i])return 5;
    u32 *header=(u32 *)input;if(header[2]!=1||header[3]!=4096||header[4]!=14)return 6;
    int bytes=32+4096*128+64;result=sc_alloc((u32)bytes);if(!result)return 7;
    int errors=registers(),begin=sc_tick();
    for(int i=0;i<4096;i++){
        u32 *entry=(u32 *)(input+32)+i*14;ScwInt a,b,n,d,value,alias;
        a.low=entry[6];a.high=(int)entry[7];b.low=entry[8];b.high=(int)entry[9];
        n.low=entry[10];n.high=(int)entry[11];d.low=entry[12];d.high=(int)entry[13];
        scw_mul32(&value,(int)entry[0],(int)entry[1]);save(&value);
        scw_add(&value,&a,&b);save(&value);scw_sub(&value,&a,&b);save(&value);
        scw_neg(&value,&a);save(&value);
        alias=a;scw_add(&alias,&alias,&b);save(&alias);
        alias=b;scw_sub(&alias,&a,&alias);save(&alias);
        alias=a;scw_neg(&alias,&alias);save(&alias);
        scw_dot3(&value,(int)entry[0],(int)entry[1],(int)entry[2],(int)entry[3],(int)entry[4],(int)entry[5]);save(&value);
        result[used++]=(u32)scw_cmp_signed(&a,&b);result[used++]=(u32)scw_cmp_unsigned(&a,&b);
        int quotient=0;result[used++]=(u32)scw_ratio16(&quotient,&n,&d);result[used++]=(u32)quotient;
        int av[5],bv[5];vectors(av,bv,entry);
        scw_dot3v(&value,av+1,bv+1);save(&value);errors+=guards(av,bv);
        scw_dot3v((ScwInt *)(av+1),av+1,bv+1);save((ScwInt *)(av+1));
        errors+=guards(av,bv);if(av[3]!=(int)entry[2])errors++;
        vectors(av,bv,entry);
        scw_dot3v((ScwInt *)(bv+1),av+1,bv+1);save((ScwInt *)(bv+1));
        errors+=guards(av,bv);if(bv[3]!=(int)entry[5])errors++;
        vectors(av,bv,entry);
        scw_dot3v((ScwInt *)(av+2),av+1,bv+1);save((ScwInt *)(av+2));
        errors+=guards(av,bv);if(av[1]!=(int)entry[0])errors++;
        vectors(av,bv,entry);
        scw_dot3v((ScwInt *)(bv+2),av+1,bv+1);save((ScwInt *)(bv+2));
        errors+=guards(av,bv);if(bv[1]!=(int)entry[3])errors++;
        vectors(av,bv,entry);
        scw_dot3v((ScwInt *)(av+1),av+1,av+1);save((ScwInt *)(av+1));errors+=guards(av,bv);
    }
    if(used*4!=bytes-64)errors++;
    for(int i=0;i<16;i++)result[used+i]=0x534D494Fu;
    magic="SWOT1MIO";for(int i=0;i<8;i++)((u8 *)result)[i]=(u8)magic[i];
    result[2]=1;result[3]=4096;result[4]=32;
#if defined(__SCCC_WIDE__)
    result[5]=1;
#else
    result[5]=0;
#endif
    result[6]=(u32)errors;result[7]=4096*128;
    wide_probe[4]=(int)result[5];wide_probe[5]=sc_tick()-begin;
    wide_probe[1]=sc_write("HOME/WIDE.OUT",result,bytes);wide_probe[2]=errors;
    page(win,errors?"FAIL / registers":"4096 cases / complete","Esc: return");
    wide_probe[0]=1;while(sc_key()!=27)sc_yield();sc_free(result);sc_free(input);return errors;
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
    (out/'WIDE.C').write_bytes(source);(out/'WIDE.IN').write_bytes(data);(out/'reference.bin').write_bytes(expected)
    (out/'verifier.py').write_bytes(Path(__file__).read_bytes())
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('sandcore.img','kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    library={name:(ROOT/'user'/name).read_bytes() for name in ('SCWIDE.H','SCWIDE.inc')}
    for name,blob in library.items():(out/name).write_bytes(blob)
    report=dict(author='mio',status='RUNNING',scope='NATIVE_WIDE_INTEGER_FULL_VALUES',cases=COUNT,
        result_words=COUNT*WORDS,coverage=coverage,compilers={},g2_sha256=phase.sha(g2),requested_accel=args.accel,
        library={name:phase.sha(blob) for name,blob in library.items()},fixture_sha256=phase.sha(source),
        input_sha256=phase.sha(data),reference_sha256=phase.sha(expected),
        inputs={name:phase.sha((ROOT/'build'/name).read_bytes()) for name in ('sandcore.img','sanddata.img','kernel.elf','kernel.sym')})
    def prepare(disk):
        c.disk_put(disk,'BIN/OLD.SCX',old);c.disk_put(disk,'BIN/NEW.SCX',g2)
        c.disk_put(disk,'SYS/SRC/WIDE.C',source);c.disk_put(disk,'HOME/WIDE.IN',data)
        for name,blob in library.items():c.disk_put(disk,'SYS/SRC/'+name,blob)
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'wide',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-wide.img'
    try:
        phase.wait(lambda:t.word(kernel['boot_stage'])==2,'welcome',180);t.open_shell(True);phase.idle()
        baseline=t.word(kernel['pf_used'])
        for name,driver,capability in [('OLD','BIN/OLD.SCX',0),('NEW','BIN/NEW.SCX',1)]:
            native,mapping,seconds=phase.compile_source(driver,'SYS/SRC/WIDE.C','HOME/'+name+'.SCX',600)
            (out/(name+'.scx')).write_bytes(native);(out/(name+'.map')).write_bytes(mapping)
            address=phase.theme.native_symbols(mapping)['wide_probe'];q.text('run HOME/'+name+'.SCX\n')
            win=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'wide-window')
            pd=t.word(kernel['tasks']+win['owner']*168)
            def state():return struct.unpack('<8i',q.memory(t.physical(pd,address),32))
            phase.wait(lambda:state()[0]==1,'wide-complete',180)
            blob=c.file_content(phase.DISK,'HOME/WIDE.OUT');assert state()[1]==len(blob) and state()[2]==0
            assert state()[4]==capability and blob[:8]==b'SWOT1MIO'
            assert struct.unpack_from('<6I',blob,8)==(1,COUNT,WORDS,capability,0,len(expected))
            assert len(blob)==32+len(expected)+64 and blob[-64:]==struct.pack('<I',0x534D494F)*16
            (out/(name+'.bin')).write_bytes(blob)
            if blob[32:-64]!=expected:
                mismatch=next(i for i,(a,b) in enumerate(zip(blob[32:-64],expected)) if a!=b)
                raise AssertionError((name,'full wide values differ',mismatch,'case',mismatch//(WORDS*4)))
            q.shot(name+'-wide-values');q.key('esc');phase.idle()
            assert t.word(kernel['pf_used'])==baseline,'wide leaked pages'
            report['compilers'][name]=dict(native_sha256=phase.sha(native),bytes=len(native),compile_seconds=seconds,
                output_sha256=phase.sha(blob),capability=capability,strict_pages='PASS',full_values='PASS',callee_saved='PASS')
            (out/'progress.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        assert all(t.word(kernel[name])==0 for name in ('keyboard_overflow','event_overflow'))
        report.update(status='PASS',limitations='完整数值/别名/callee/护栏；非精确三角形、BVH或游戏帧率验收')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('SCWIDE PASS',COUNT,COUNT*WORDS,flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
