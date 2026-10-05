#!/usr/bin/env python3
"""mio：SCCC立即数/二次幂优化的真实原生语言、边界与副作用回归。

同核中运行旧/新真正G2编译器，分别编译同一探针；目标在三环实际
写出每个整数结果，Python无界整数只作为独立参考。不是宿主C结果，
不是只检查机器码是否生成，也不将本夹具耗时冒充游戏帧率。
"""
import argparse
import json
import random
import shutil
import struct
import time
import traceback
import verify_m8_phase2 as phase

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler


def fixture():
    values={0,1,2,3,0x7fffffff,0x80000000,0xffffffff}
    for shift in range(1,32):
        boundary=1<<shift
        for delta in (-1,0,1):values.add((boundary+delta)&0xffffffff);values.add((-boundary+delta)&0xffffffff)
    rng=random.Random(0x533343)
    values.update(rng.getrandbits(32) for _ in range(64))
    return sorted(values)


def source(values):
    functions=[]
    for name,kind,op,count in [('sd','int','/',31),('sm','int','%',31),
                              ('ud','u32','/',32),('um','u32','%',32)]:
        cases='\n'.join('case '+str(k)+': return n'+op+str(1<<k)+('u' if kind=='u32' else '')+';'
                        for k in range(count))
        functions.append('static '+kind+' '+name+'('+kind+' n,int shift){switch(shift){\n'+cases+'\n}return 0;}')
    # 这些边界函数不实际执行，只在产物中核对仍保留原IDIV异常路径。
    functions += ['static int runtime_zero(int n){return n/0;}',
                  'static int runtime_overflow(int n){return n/-1;}']
    entries=',\n'.join(str(value)+'u' for value in values)
    return ('''/* mio：普通三环数值夹具，输出显式记录实际机器码运行结果。 */
#include "SCAPI.H"
@FUNCTIONS@
static u32 values[@COUNT@]={@VALUES@};
static u32 result[8+@COUNT@*126+16];
static volatile int codegen_probe[8],side_count;
static int once(void){side_count++;return -513;}
int main(void){
    int win=sc_open("SCCC codegen / mio",300,160);
    if(win<0)return 1;
    char path[64];sc_args(path,sizeof(path));if(!path[0])return 2;
    u8 *bytes=(u8 *)result;char *magic="GOPT1MIO";
    for(int i=0;i<8;i++)bytes[i]=(u8)magic[i];
    result[2]=1;result[3]=@COUNT@;result[4]=126;
    int out=8,begin=sc_tick();
    for(int i=0;i<@COUNT@;i++){
        int n=(int)values[i];u32 un=values[i];
        for(int k=0;k<31;k++){result[out++]=(u32)sd(n,k);result[out++]=(u32)sm(n,k);}
        for(int k=0;k<32;k++){result[out++]=ud(un,k);result[out++]=um(un,k);}
    }
    result[5]=(u32)(sc_tick()-begin);
    int a=once()/256,b=once()%256,c=once()*3,d=once()+7,e=once()-7;
    int f=once()&255,g=once()|255,h=once()^255,j=once()<<3,k=once()>>3;
    int z=once()%1,one=once()/1,mixed=(int)((unsigned int)once()/256u);
    // 无符号右操作数沿用原后端选择，另测负值与每次调用只执行一次。
    int errors=a!=-2||b!=-1||c!=-1539||d!=-506||e!=-520||f!=255
        ||g!=-513||h!=-768||j!=-4104||k!=-65||z!=0||one!=-513
        ||mixed!=16777213||side_count!=13;
    result[6]=(u32)errors;result[7]=(u32)side_count;
    for(int i=0;i<16;i++)result[out+i]=0x534D494Fu;
    codegen_probe[1]=sc_write(path,result,sizeof(result));codegen_probe[2]=errors;
    codegen_probe[0]=1;page(win,"Native arithmetic","Esc: return");
    while(sc_key()!=27)sc_yield();return errors;
}
'''.replace('@FUNCTIONS@','\n'.join(functions)).replace('@COUNT@',str(len(values)))
        .replace('@VALUES@',entries)).encode('utf-8')


def expected(values):
    result=[]
    for value in values:
        signed=value if value<0x80000000 else value-0x100000000
        for shift in range(31):
            divisor=1<<shift;quotient=abs(signed)//divisor
            if signed<0:quotient=-quotient
            result.extend((quotient&0xffffffff,(signed-quotient*divisor)&0xffffffff))
        for shift in range(32):result.extend((value//(1<<shift),value% (1<<shift)))
    return struct.pack('<'+str(len(result))+'I',*result)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out',required=True);parser.add_argument('--old-stage',required=True)
    parser.add_argument('--new-stage',required=True);args=parser.parse_args()
    out=ROOT/args.out;out.mkdir(parents=True,exist_ok=False)
    phase.OUT=t.OUT=q.OUT=c.OUT=phase.theme.OUT=out;c.v.windows=t.windows
    compilers={}
    for label,stage in [('OLD',args.old_stage),('NEW',args.new_stage)]:
        base=ROOT/stage;report=json.loads((base/'results.json').read_text(encoding='utf-8'))
        assert report['status']=='PASS' and report['scope']=='native'
        compilers[label]=(base/'g2.scx').read_bytes()
        assert compilers[label]==(base/'g3.scx').read_bytes()
    values=fixture();code=source(values);reference=expected(values)
    (out/'CODEGEN.C').write_bytes(code)
    feature=(ROOT/'assets/home/FEATURE.C').read_bytes();(out/'FEATURE.C').write_bytes(feature)
    negatives=[b'#error intended\n',b'float x;int main(void){return 0;}\n',
               b'int main(void){return unknown_symbol;}\n',
               b'#define ONE(x) x\nint main(void){return ONE(1,2);}\n']
    shutil.copy2(ROOT/'build/sandcore.img',out/'sandcore.img')
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    report=dict(author='mio',status='RUNNING',scope='SCCC_CODEGEN_AND_LANGUAGE',
                inputs={name:phase.sha((ROOT/name).read_bytes()) for name in
                        ('build/sandcore.img','build/sanddata.img','build/kernel.elf','build/kernel.sym','user/s3c_emit.inc','user/SCAPI.H')},
                values=len(values),integer_results=len(reference)//4,source_sha256=phase.sha(code),cases={})
    def prepare(disk):
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
        c.disk_put(disk,'HOME/CODEGEN.C',code);c.disk_put(disk,'HOME/FEATURE.C',feature)
        for name,blob in compilers.items():c.disk_put(disk,'BIN/'+name+'.SCX',blob)
        for index,blob in enumerate(negatives):c.disk_put(disk,'HOME/BAD'+str(index)+'.C',blob)
        c.disk_put(disk,'HOME/FAIL.SCX',b'UNCHANGED')
    proc=t.launch('std',128,'codegen',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-codegen.img'
    try:
        phase.wait(lambda:t.word(q.symbols()['boot_stage'])==2,'welcome');t.open_shell(True);phase.idle()
        baseline=t.word(q.symbols()['pf_used'])
        for label in compilers:
            native,mapping,seconds=phase.compile_source('BIN/'+label+'.SCX','HOME/CODEGEN.C','HOME/'+label+'.SCX')
            (out/(label+'.scx')).write_bytes(native);(out/(label+'.map')).write_bytes(mapping)
            # native_symbols只投影OBJECT；异常路径需要函数地址，不能
            # 将合法函数“未出现在对象表”误判为编译器丢掉了函数。
            symbols={parts[1]:int(parts[0],16) for row in mapping.decode().splitlines()[1:]
                     if len(parts:=row.split())>=3}
            # 验证优化没有消掉运行时异常；机器码包含原IDIV ECX。
            for name in ('runtime_zero','runtime_overflow'):
                address=symbols[name];later=sorted(v for v in symbols.values() if v>address)
                end=later[0] if later else 0x400000+len(native)-36
                body=native[36+address-0x400000:36+end-0x400000]
                assert b'\xf7\xf9' in body,(label,name,'lost signed division trap')
            path='HOME/OUT-'+label+'.BIN';q.text('run HOME/'+label+'.SCX '+path+'\n')
            win=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'probe-'+label)
            pd=t.word(q.symbols()['tasks']+win['owner']*168)
            def state():return struct.unpack('<8i',q.memory(t.physical(pd,symbols['codegen_probe']),32))
            phase.wait(lambda:state()[0]==1,'arithmetic-results-'+label,120)
            observed=state();blob=c.file_content(phase.DISK,path)
            assert len(blob)==32+len(reference)+64 and blob[:8]==b'GOPT1MIO'
            assert blob[32:-64]==reference,label+' integer result mismatch'
            assert blob[-64:]==struct.pack('<I',0x534D494F)*16
            assert struct.unpack_from('<2I',blob,24)==(0,13),label+' side effect mismatch'
            assert observed[1]==len(blob) and observed[2]==0
            (out/(label+'.bin')).write_bytes(blob);q.shot(label+'-arithmetic')
            q.key('esc');phase.idle();assert t.word(q.symbols()['pf_used'])==baseline
            feature_output='HOME/F'+label+'.SCX'
            generated,feature_map,_=phase.compile_source('BIN/'+label+'.SCX','HOME/FEATURE.C',feature_output)
            (out/(label+'-feature.scx')).write_bytes(generated)
            path='HOME/F-'+label;q.text('run '+feature_output+' '+path+'\n')
            assert c.await_file(phase.DISK,path,expected=b'PASS')==b'PASS'
            phase.wait(lambda:len(t.windows())==2,'feature-result');q.shot(label+'-18-groups')
            q.key('esc');phase.idle();assert t.word(q.symbols()['pf_used'])==baseline
            report['cases'][label]=dict(compiler_sha256=phase.sha(compilers[label]),native_sha256=phase.sha(native),
                native_bytes=len(native),ticks=struct.unpack_from('<I',blob,20)[0],compile_seconds=seconds,
                integer_reference='PASS',side_effects='PASS',language_18='PASS',division_trap_bytes='PASS')
        for index in range(len(negatives)):
            phase.idle();q.text('run BIN/NEW.SCX HOME/BAD'+str(index)+'.C HOME/FAIL.SCX\n')
            log=c.await_file(phase.DISK,'HOME/FAIL.SCX.log',30)
            # 每轮日志可能尚是上轮，先等待新诊断包含当前输入名。
            phase.wait(lambda:(c.file_content(phase.DISK,'HOME/FAIL.SCX.log') or b'').find(
                ('HOME/BAD'+str(index)+'.C').encode())>=0,'diagnostic-'+str(index),30)
            phase.idle();log=c.file_content(phase.DISK,'HOME/FAIL.SCX.log')
            assert log.startswith(b'SCCC ERROR') and c.file_content(phase.DISK,'HOME/FAIL.SCX')==b'UNCHANGED'
            (out/('negative-'+str(index)+'.log')).write_bytes(log);q.shot('negative-'+str(index))
            assert t.word(q.symbols()['pf_used'])==baseline
        report.update(status='PASS',negative_preserve_output_4='PASS',strict_pages='PASS',
                      limitations='整数/副作用/语言/诊断及当前新G2产物；夹具成本单轮，非游戏帧率')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('SCCC CODEGEN PASS',json.dumps(report['cases']),flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
