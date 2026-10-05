#!/usr/bin/env python3
"""mio：真正G2普通三环程序调用本批辅助解码器，验证长度与边界。

所测字节是数据，不执行非法指令；解码实现来自完整Debugger源。
结果通过已有WRITE写盘并正常退出，不用宿主版本函数替代客体。
每项32B文本之后另有8B哨兵，未知/短立即数不得越界或吞下一条。
"""
import hashlib,json,struct,sys,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'tools'))
import verify_files as seed
v=seed.v;t=seed.t;q=seed.q;compiler=seed.compiler
STAGE=ROOT/'build/m8-debugger-draft';OUT=STAGE/'decode'
CASES=(
 ('66 operand width',bytes.fromhex('66 B8 44 33 22 11'),6,1,'db 66'),
 ('unknown 0F',bytes.fromhex('0F 01 C1'),3,1,'db 0F'),
 ('Jcc rel32',bytes.fromhex('0F 85 44 33 22 11'),6,6,'jcc rel32'),
 ('short Jcc',bytes.fromhex('0F 85 44 33 22 11'),5,1,'db 0F'),
 ('short MOV',bytes.fromhex('B8 44 33 22 11'),4,1,'db B8'),
 ('MOV imm32',bytes.fromhex('B8 44 33 22 11'),5,5,'mov eax,11223344'),
 ('SETcc',bytes.fromhex('0F 90 C0'),3,3,'setcc'),
 ('short SIB',bytes.fromhex('FF 14 25 44 33 22 11'),6,1,'db FF'),
 ('CALL SIB',bytes.fromhex('FF 14 25 44 33 22 11'),7,7,'call'),
 ('TEST immediate',bytes.fromhex('F7 C0 44 33 22 11'),6,6,'test imm32'),
 ('short TEST',bytes.fromhex('F7 C0 44 33 22 11'),5,1,'db F7'),
 ('IDIV',bytes.fromhex('F7 F8'),2,2,'idiv'),
 ('reserved F7',bytes.fromhex('F7 C8'),2,1,'db F7'),
 ('INC',bytes.fromhex('FF C0'),2,2,'inc'),
 ('DEC',bytes.fromhex('FF C8'),2,2,'dec'),
 ('PUSH',bytes.fromhex('FF F0'),2,2,'push'),
 ('far call unsupported',bytes.fromhex('FF 18'),2,1,'db FF'),
 ('short SETcc',bytes.fromhex('0F 90'),2,1,'db 0F'),
 ('RET',bytes.fromhex('C3'),1,1,'ret'),
 ('TEST SIB immediate',bytes.fromhex('F7 04 25 44 33 22 11 88 77 66 55'),11,11,'test imm32'),
 ('zero available bytes',bytes.fromhex('B8'),0,0,''),
)


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True,exist_ok=True);assert not (OUT/'results.json').exists()
    core=json.loads((STAGE/'results.json').read_text(encoding='utf-8'));assert core['status']=='PASS'
    inputs=core['inputs_sha256'];assert inputs=={name:sha(ROOT/name) for name in inputs};verifier=sha(Path(__file__))
    data=b''.join(case[1].ljust(16,b'\xA5') for case in CASES)
    source=('''/* mio：完整调试器仅重命名入口，测试调用同一实际辅助
 * 函数；既有DEBUG/NUI代码不改。未知指令只作为私有常量数据。
 * 32B文案之外每项8B哨兵与整个源字节均从实际WRITE结果核对。 */
#define main debugger_original_main
#include "DEBUGGER.inc"
#undef main
static u8 samples[]={'''+','.join(str(byte) for byte in data)+'''};
static int limits[]={'''+','.join(str(case[2]) for case in CASES)+'''};
static u32 sizes['''+str(len(CASES))+'''];
static u8 names['''+str(len(CASES)*40)+'''];
int main(void)
{
    for(int i=0;i<sizeof(names);i++)names[i]=0xA5;
    for(int i=0;i<'''+str(len(CASES))+''';i++)sizes[i]=disassemble(samples+i*16,limits[i],0x400000,(char*)names+i*40);
    if(sc_write("HOME/LENGTHS.BIN",sizes,sizeof(sizes))<0)return 1;
    if(sc_write("HOME/TEXT.BIN",names,sizeof(names))<0)return 2;
    if(sc_write("HOME/SAMPLES.BIN",samples,sizeof(samples))<0)return 3;
    return 0;
}
''').encode();(OUT/'probe.c').write_bytes(source)
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:g2=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert hashlib.sha256(g2).hexdigest()=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    def prepare(disk):
        compiler.disk_put(disk,'BIN/G2.SCX',g2);compiler.disk_put(disk,'SYS/DECODE/P.C',source)
        compiler.disk_put(disk,'SYS/DECODE/DEBUGGER.inc',(ROOT/'build/m8-next/debugger.c').read_bytes())
        for name in ('SCAPI.H','NUI.inc'):compiler.disk_put(disk,'SYS/DECODE/'+name,(ROOT/'user'/name).read_bytes())
        compiler.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
    v.OUT=OUT;v.bind();kernel=q.symbols();proc=t.launch('std',128,'debugger-decode',prepare);disk=OUT/'sanddata-std-128-debugger-decode.img'
    try:
        t.open_shell(True);v.idle();native=compiler.compile_native(disk,'BIN/G2.SCX','SYS/DECODE/P.C','HOME/PROBE.SCX',300);v.idle()
        mapping=compiler.await_file(disk,'HOME/PROBE.SCX.map');(OUT/'probe-native.scx').write_bytes(native);(OUT/'probe-native.map').write_bytes(mapping)
        baseline=(t.word(kernel['pf_used']),t.word(kernel['desktop_pages']))
        q.text('run HOME/PROBE.SCX\n');lengths=compiler.await_file(disk,'HOME/LENGTHS.BIN');names=compiler.await_file(disk,'HOME/TEXT.BIN');samples=compiler.await_file(disk,'HOME/SAMPLES.BIN');v.idle()
        assert len(lengths)==len(CASES)*4 and len(names)==len(CASES)*40 and samples==data
        values=struct.unpack('<'+str(len(CASES))+'I',lengths);rows=[]
        for i,case in enumerate(CASES):
            actual=names[i*40:i*40+32].split(b'\0')[0].decode('ascii')
            assert values[i]==case[3] and actual==case[4],(case[0],values[i],actual)
            assert names[i*40+32:(i+1)*40]==b'\xA5'*8,'文本边界被写出'
            rows.append(dict(case=case[0],bytes=case[1].hex(),maximum=case[2],length=values[i],text=actual))
        seed.wait(lambda:t.word(kernel['pf_used'])==baseline[0]+t.word(kernel['desktop_pages'])-baseline[1],'辅助解码实际程序严格回收')
        q.shot('01-real-cli-probe-returns-after-full-output');overflow={name:t.word(kernel[name]) for name in ('keyboard_overflow','event_overflow')};assert not any(overflow.values())
        assert inputs=={name:sha(ROOT/name) for name in inputs} and sha(Path(__file__))==verifier
        report=dict(author='mio',status='PASS',inputs_sha256=inputs,verifier_sha256=verifier,probe_source_sha256=hashlib.sha256(source).hexdigest(),
            native_sha256=hashlib.sha256(native).hexdigest(),native_map_sha256=hashlib.sha256(mapping).hexdigest(),cases=rows,overflow=overflow,
            limits='真实G2普通三环辅助解码长度/文案/32B边界及输入字节不改；不是完整x86解码器，也不代替真实TF/INT3/GUI或全M8')
        (OUT/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,ensure_ascii=False),flush=True)
    except Exception:
        if proc.poll() is None:q.shot('failure')
        raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
