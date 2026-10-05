#!/usr/bin/env python3
"""mio：SCCC固定栈槽生成的真实原生、窄整数/别名/副作用回归。

独立Python整数参考只规定输出值。两代实际G2在同一SandCore内
读取同一源码并生成SCX，三环执行后从真正IDE文件读取每项结果。
不是宿主编译，也不从生成机器码反推期望值；页回收和尾护栏另验。
"""
import argparse
import json
import shutil
import struct
import traceback
import verify_m8_phase2 as phase
from verify_sccc_codegen import fixture

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler
MASK=0xFFFFFFFF


def signed(value,bits):
    value &= (1<<bits)-1
    return value-(1<<bits) if value&(1<<(bits-1)) else value


def reference(values):
    rows=[]
    for n in values:
        r=[(n+17)&MASK,(n+17)&MASK]
        for bits,is_signed in ((8,True),(8,False),(16,True),(16,False)):
            convert=lambda v:signed(v,bits) if is_signed else v&((1<<bits)-1)
            value=convert(n);r.extend((value,value))
            r.append(value);value=convert(value+1);r.append(value)
            value=convert(value+1);r.append(value);r.append(value)
            r.append(value);value=convert(value-1);r.append(value)
            value=convert(value-1);r.append(value);r.append(value)
        # 形参自身也在固定帧中；参数char/short按callee原转换读取。
        char_value=signed(signed(n,8)+1,8)
        short_value=signed(signed(n,16)+3,16)
        r.append((char_value*3+short_value*7+(n^0x5A5A5A5A))&MASK)
        r.extend((17,1,29,2,17,0,29,2))
        # 被取地址的local经普通函数修改后必须立即读到新值，不能
        # 以“局部变量优化”为由将其旧值跨调用缓存到寄存器里。
        r.extend((41,41,17,18,18,19))
        r.extend((n+17,signed(n,8)+17,signed(n,16)+17,n+17,58,1))
        r.extend((n^0x13579BDF,0x12345678,0x01234567,0x89ABCDEF))
        rows.append([x&MASK for x in r])
    assert len({len(row) for row in rows})==1
    return len(rows[0]),struct.pack('<'+str(sum(map(len,rows)))+'I',*(x for row in rows for x in row))


SOURCE=r'''/* mio：固定栈槽读取/写回；窄类型每一步输出真实结果。
 * 参数、前后缀、赋值表达式、volatile和地址别名均实际执行。 */
#include "SCAPI.H"
#define SAVE(value) result[out++]=(u32)(value)
static u32 values[@COUNT@]={@VALUES@};
static u32 result[8+@COUNT@*@WIDTH@+16];
static volatile int frame_probe[8],side_count;
static int frame_global;
static volatile char frame_char;
static volatile short frame_short;
static volatile u32 frame_unsigned;
static int once(void){side_count++;return 1;}
static int change(int *p,int value){*p=value;return *p;}
static int touch_globals(int n){frame_global=n;frame_char=(char)n;frame_short=(short)n;frame_unsigned=(u32)n;return 17;}
static u32 plain_call(u32 n){return n^0x13579BDFu;}
static void assembly_call(void){
    __asm__ __volatile__("xor %%ebx, %%ebx; xor %%esi, %%esi; xor %%edi, %%edi" ::: "ebx","esi","edi","cc");
}
static u32 parameters(char c,short s,u32 n){c++;s+=3;return (u32)(int)c*3u+(u32)(int)s*7u+(n^0x5A5A5A5Au);}
int main(void){
    int win=sc_open("SCCC frame code / mio",310,170);
    if(win<0)return 1;
    char path[64];sc_args(path,sizeof(path));if(!path[0])return 2;
    u8 *bytes=(u8 *)result;char *magic="FOPT1MIO";
    for(int i=0;i<8;i++)bytes[i]=(u8)magic[i];
    result[2]=1;result[3]=@COUNT@;result[4]=@WIDTH@;
    int out=8,begin=sc_tick();
    for(int i=0;i<@COUNT@;i++){
        u32 n=values[i],u=0;
        SAVE(u=n+17u);SAVE(u);
        char c=0;unsigned char uc=0;short s=0;unsigned short us=0;
        SAVE(c=(char)n);SAVE(c);
        SAVE(c++);SAVE(c);SAVE(++c);SAVE(c);SAVE(c--);SAVE(c);SAVE(--c);SAVE(c);
        SAVE(uc=(unsigned char)n);SAVE(uc);
        SAVE(uc++);SAVE(uc);SAVE(++uc);SAVE(uc);SAVE(uc--);SAVE(uc);SAVE(--uc);SAVE(uc);
        SAVE(s=(short)n);SAVE(s);
        SAVE(s++);SAVE(s);SAVE(++s);SAVE(s);SAVE(s--);SAVE(s);SAVE(--s);SAVE(s);
        SAVE(us=(unsigned short)n);SAVE(us);
        SAVE(us++);SAVE(us);SAVE(++us);SAVE(us);SAVE(us--);SAVE(us);SAVE(--us);SAVE(us);
        SAVE(parameters((char)n,(short)n,n));
        int array[4]={17,29,41,53};int *p=array;
        SAVE(*p++);SAVE(p-array);SAVE(*p);SAVE(++p-array);
        SAVE(*(p-=2));SAVE(p-array);SAVE(*(p+=1));SAVE(++p-array);
        int local=17;SAVE(change(&local,41));SAVE(local);
        volatile int v=17;SAVE(v);SAVE(v+=once());SAVE(v);SAVE(v++ + once());
        // 左侧调用先改同名全局/别名局部，右值必须之后实际读。
        // 不能为直接ECX加载预读右值，窄全局还须按自身符号扩展。
        SAVE(touch_globals((int)n)+frame_global);
        SAVE(touch_globals((int)n)+(int)frame_char);
        SAVE(touch_globals((int)n)+(int)frame_short);
        SAVE(touch_globals((int)n)+frame_unsigned);
        SAVE(change(&local,29)+local);SAVE(local<53);
        // 普通callee可省未使用寄存器的压栈，含asm的callee则必须
        // 保守保留；显式模板写b/S/D覆盖只看固定约束会漏掉的情况。
        __asm__ __volatile__("mov $305419896, %%ebx; mov $19088743, %%esi; mov $2309737967, %%edi" ::: "ebx","esi","edi","cc");
        SAVE(plain_call(n));assembly_call();
        u32 kept_b,kept_s,kept_d;
        __asm__ __volatile__("mov %%ebx, %0; mov %%esi, %1; mov %%edi, %2" : "=a"(kept_b),"=c"(kept_s),"=d"(kept_d));
        SAVE(kept_b);SAVE(kept_s);SAVE(kept_d);
    }
    result[5]=(u32)(sc_tick()-begin);result[6]=(u32)side_count;result[7]=(u32)(out-8);
    for(int i=0;i<16;i++)result[out+i]=0x534D494Fu;
    frame_probe[1]=sc_write(path,result,sizeof(result));frame_probe[0]=1;
    page(win,"Native stack scalars","Esc: return");
    while(sc_key()!=27)sc_yield();return 0;
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--old-stage',required=True)
    parser.add_argument('--new-stage',required=True);args=parser.parse_args()
    out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=out;c.v.windows=t.windows
    compilers={}
    for name,stage in [('OLD',args.old_stage),('NEW',args.new_stage)]:
        path=ROOT/stage;proof=json.loads((path/'results.json').read_text(encoding='utf-8'))
        assert proof['status']=='PASS' and proof['scope']=='native'
        blob=(path/'g2.scx').read_bytes();assert blob==(path/'g3.scx').read_bytes();compilers[name]=blob
    values=fixture();width,expected=reference(values)
    source=SOURCE.replace('@COUNT@',str(len(values))).replace('@WIDTH@',str(width)).replace(
        '@VALUES@',',\n'.join(str(n)+'u' for n in values)).encode('utf-8')
    (out/'FRAME.C').write_bytes(source)
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('sandcore.img','kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    report=dict(author='mio',status='RUNNING',scope='SCCC_FRAME_SCALAR_SEMANTICS',
        source_sha256=phase.sha(source),reference_cases=len(expected)//4,cases={},
        inputs={name:phase.sha((ROOT/'build'/name).read_bytes()) for name in
                ('sandcore.img','sanddata.img','kernel.elf','kernel.sym')})
    def prepare(disk):
        c.disk_put(disk,'HOME/FRAME.C',source)
        for name,blob in compilers.items():c.disk_put(disk,'BIN/'+name+'.SCX',blob)
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'frames',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-frames.img'
    try:
        phase.wait(lambda:t.word(kernel['boot_stage'])==2,'welcome');t.open_shell(True);phase.idle()
        baseline=t.word(kernel['pf_used'])
        for name in compilers:
            native,mapping,seconds=phase.compile_source('BIN/'+name+'.SCX','HOME/FRAME.C','HOME/'+name+'.SCX')
            (out/(name+'.scx')).write_bytes(native);(out/(name+'.map')).write_bytes(mapping)
            address=phase.theme.native_symbols(mapping)['frame_probe']
            path='HOME/F-'+name+'.BIN';q.text('run HOME/'+name+'.SCX '+path+'\n')
            window=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'frame-window')
            pd=t.word(kernel['tasks']+window['owner']*168)
            def state():return struct.unpack('<8i',q.memory(t.physical(pd,address),32))
            phase.wait(lambda:state()[0]==1,'frame-complete',90)
            blob=c.file_content(phase.DISK,path);assert blob[:8]==b'FOPT1MIO'
            assert len(blob)==32+len(expected)+64 and state()[1]==len(blob)
            assert blob[32:-64]==expected,(name,'scalar/alias output differs')
            assert blob[-64:]==struct.pack('<I',0x534D494F)*16
            assert struct.unpack_from('<2I',blob,24)==(len(values)*2,len(expected)//4)
            (out/(name+'.bin')).write_bytes(blob);q.shot(name+'-frame-scalars')
            q.key('esc');phase.idle();assert t.word(kernel['pf_used'])==baseline
            report['cases'][name]=dict(compiler_sha256=phase.sha(compilers[name]),native_sha256=phase.sha(native),
                bytes=len(native),compile_seconds=seconds,strict_pages='PASS',independent_values='PASS',
                ticks=struct.unpack_from('<I',blob,20)[0])
        report.update(status='PASS',limitations='原生固定栈标量/窄转换/形参/指针/别名/volatile/副作用；非全应用FPS验收')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('SCCC FRAME SCALARS PASS',flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
