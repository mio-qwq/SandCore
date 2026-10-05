#!/usr/bin/env python3
"""mio：通用内存库的旧M7 C回退/新G2 REP真实编译与完整字节验证。

期望值由Python字节区间操作及独立半开裁剪参考规定，不从机器码
反推。客体输出包含整个目标缓冲，故未修改区域/两端护栏也会核对。
另由同一真实G2生成缓存搬运夹具，供完整窗口固定成本测量；该
夹具交替两张测试图案，不冒充游戏、宣传片或最终可达帧率。
"""
import argparse
import json
import random
import shutil
import struct
import traceback
import zipfile
import verify_m8_phase2 as phase

ROOT=phase.ROOT;t=phase.t;q=phase.q;c=phase.compiler
LENGTHS=(0,1,2,3,4,5,7,8,15,16,31,32,63,64,65,127,128,255,256,257,511,512)
WORDS=(0,1,2,3,7,31,64,127)
BUFFER=640


def rectangles():
    result=[(-3,-2,9,7,0),(0,0,37,29,0),(37,29,0,0,0),(9,7,-5,2,0),
            (-99,-99,2,2,1),(0,0,37,29,1),(9,7,-5,2,1),(28,21,20,20,1)]
    rng=random.Random(0x534D454D)
    result.extend((rng.randrange(-30,50),rng.randrange(-25,40),rng.randrange(-8,60),
                   rng.randrange(-8,45),i&1) for i in range(64))
    return result


def reference(rects):
    output=bytearray();cases=0
    source=bytes((i*37+11)&255 for i in range(BUFFER))
    fresh=lambda:bytearray((i*13+7)&255 for i in range(BUFFER))
    for n in LENGTHS:
        for a in range(4):
            for b in range(4):
                target=fresh();target[32+b:32+b+n]=source[32+a:32+a+n]
                output+=target;cases+=1
    for n in LENGTHS:
        for a in range(4):
            target=fresh();target[32+a:32+a+n]=bytes([0xA5])*n;output+=target;cases+=1
    for n in WORDS:
        for a in range(4):
            target=fresh();target[32+a:32+a+n*4]=struct.pack('<I',0x12345678)*n
            output+=target;cases+=1
    for n in LENGTHS:
        for a in range(8):
            for b in range(8):
                target=fresh();target[32+b:32+b+n]=bytes(target[32+a:32+a+n])
                output+=target;cases+=1
    for x,y,w,h,clip in rects:
        pixels=[0x534D494F]*16+[0xFF123456]*(37*29)+[0x534D494F]*16
        right=max(0,min(37,x+w));bottom=max(0,min(29,y+h))
        left=max(0,min(37,x));top=max(0,min(29,y))
        if clip:
            # NUI clip_set(2,3,13,9)，150%；坐标为非负，整除即截零。
            left=max(left,2*150//100);top=max(top,3*150//100)
            right=min(right,15*150//100);bottom=min(bottom,12*150//100)
        for row in range(top,bottom):
            for col in range(left,right):pixels[16+row*37+col]=0xFFABCDEF
        output+=struct.pack('<'+str(len(pixels))+'I',*pixels)
    return cases,bytes(output)


SOURCE=r'''/* mio：测试完整缓冲而非只测被写子区。旧M7和新G2
 * 真正生成并执行相同源码，能力宏只决定实现路径。 */
#include "SCAPI.H"
#include "NUI.inc"
static int lengths[@NL@]={@LENGTHS@},words[@NW@]={@WORDS@};
static int rects[@NR@][5]={@RECTS@};
static u8 source[640],target[640],output[@SIZE@];
static u32 rect_buffer[37*29+32];
static volatile int mem_probe[8];
static int used=32,errors;
static void reset(void){for(int i=0;i<640;i++){source[i]=(u8)(i*37+11);target[i]=(u8)(i*13+7);}}
static void append_buffer(const void *data,int count){
    const u8 *p=(const u8 *)data;for(int i=0;i<count;i++)output[used++]=p[i];
}
static void put(int at,u32 value){for(int i=0;i<4;i++)output[at+i]=(u8)(value>>(8*i));}
int main(void){
    int win=sc_open_rgb("SCMEM byte guards / mio",310,170);if(win<0)return 1;
    if(sc_mem_copy(0,0,0)||sc_mem_move(0,0,0)||sc_mem_fill(0,0,0)||sc_mem_fill32(0,0,0))errors++;
    for(int k=0;k<@NL@;k++)for(int a=0;a<4;a++)for(int b=0;b<4;b++){
        reset();if(sc_mem_copy(target+32+b,source+32+a,(u32)lengths[k])!=target+32+b)errors++;
        append_buffer(target,640);
    }
    for(int k=0;k<@NL@;k++)for(int a=0;a<4;a++){
        reset();if(sc_mem_fill(target+32+a,421,(u32)lengths[k])!=target+32+a)errors++;
        append_buffer(target,640);
    }
    for(int k=0;k<@NW@;k++)for(int a=0;a<4;a++){
        reset();if(sc_mem_fill32((u32 *)(target+32+a),0x12345678u,(u32)words[k])!=(u32 *)(target+32+a))errors++;
        append_buffer(target,640);
    }
    for(int k=0;k<@NL@;k++)for(int a=0;a<8;a++)for(int b=0;b<8;b++){
        reset();if(sc_mem_move(target+32+b,target+32+a,(u32)lengths[k])!=target+32+b)errors++;
        append_buffer(target,640);
    }
    // 裁剪参考包括负尺寸/屏外/全宽/150%逻辑裁剪；保存前后16项护栏。
    ui_width=37;ui_height=29;ui_scale=150;ui_pixels=rect_buffer+16;
    for(int k=0;k<@NR@;k++){
        for(int i=0;i<37*29+32;i++)rect_buffer[i]=(i<16||i>=16+37*29)?0x534D494Fu:0xFF123456u;
        ui_clip_clear();if(rects[k][4])ui_clip_set(2,3,13,9);
        ui_physical_rgb(rects[k][0],rects[k][1],rects[k][2],rects[k][3],0x12ABCDEFu);
        append_buffer(rect_buffer,sizeof(rect_buffer));
    }
    char *magic="SMEM1MIO";for(int i=0;i<8;i++)output[i]=(u8)magic[i];
    put(8,1);put(12,@CASES@);put(16,@NR@);put(20,(u32)(used-32));put(24,(u32)errors);
#ifdef __SCCC_REP__
    put(28,1);
#else
    put(28,0);
#endif
    for(int i=0;i<16;i++){put(used,0x534D494Fu);used+=4;}
    char path[64];sc_args(path,sizeof(path));mem_probe[1]=sc_write(path,output,(u32)used);
    mem_probe[2]=errors;mem_probe[0]=1;
    for(;;){if(sc_key()==27)return 0;sc_yield();}
}
'''

FRAME_SOURCE=r'''/* mio：预生成方案固定搬运/FRAME/合成成本夹具。
 * 两张不同测试图案载入时生成，之后全尺寸交替，不能将静止帧的
 * 相同内容检测当成传输能力。图案不作为游戏/宣传片资源发布。 */
#include "SCAPI.H"
#include "NUI.inc"
int main(void){
    if(ui_open("Resource transfer budget / mio")<0)return 1;
    ui_pointer();ui_header("RESOURCE TRANSFER","Preparing two diagnostic patterns");ui_present();
    int width=ui_width,height=ui_height;u32 bytes=(u32)(width*height*4);
    u32 *a=(u32 *)sc_alloc(bytes),*b=(u32 *)sc_alloc(bytes);if(!a||!b)return 2;
    for(int y=0;y<height;y++)for(int x=0;x<width;x++){
        u32 color=(u32)((x*255/width)<<16)|(u32)((y*255/height)<<8)|(u32)((x+y)&255);
        a[y*width+x]=0xFF000000u|color;b[y*width+x]=0xFF000000u|(color^0x00FFFFFFu);
    }
    for(;;){
        if(sc_key()==27){sc_free(a);sc_free(b);return 0;}
        ui_pointer();if(ui_width!=width||ui_height!=height)return 3;
        ui_header("RESOURCE TRANSFER","Two test patterns / not a game frame");
        // header按NUI合同会先清整画布，必须在它之后搬运，否则两图
        // 均被背景覆盖，wm相同内容检测的67次假帧不能作为传输成本。
        sc_mem_copy(ui_pixels,(ui_frames&1)?a:b,bytes);
        ui_footer("Full canvas copy + FRAME32 + compositor / mio");ui_present();sc_yield();
    }
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
    with zipfile.ZipFile(ROOT/'build/SandCore-M7-2026-10-02.zip') as archive:
        old=archive.read('sandcore/build/fs/bin/s3c.scx')
    assert phase.sha(old)=='c182fc6746520d213e3ea5f2789826afe7931ffae2264f9c5d595f1a12ad8494'
    rects=rectangles();cases,expected=reference(rects)
    source=SOURCE.replace('@NL@',str(len(LENGTHS))).replace('@NW@',str(len(WORDS))).replace('@NR@',str(len(rects)))
    source=source.replace('@LENGTHS@',','.join(map(str,LENGTHS))).replace('@WORDS@',','.join(map(str,WORDS)))
    source=source.replace('@RECTS@',',\n'.join('{'+','.join(map(str,row))+'}' for row in rects))
    source=source.replace('@SIZE@',str(32+len(expected)+64)).replace('@CASES@',str(cases)).encode('utf-8')
    (out/'MEM.C').write_bytes(source);(out/'FRAMEBENCH.C').write_text(FRAME_SOURCE,encoding='utf-8')
    kernel=q.symbols();q.symbols=lambda:kernel
    for name in ('sandcore.img','kernel.elf','kernel.sym'):shutil.copy2(ROOT/'build'/name,out/name)
    report=dict(author='mio',status='RUNNING',scope='SCMEM_NATIVE_GUARDS',cases={},memory_cases=cases,
        rectangles=len(rects),expected_bytes=len(expected),source_sha256=phase.sha(source),
        g2_sha256=phase.sha(g2),inputs={name:phase.sha((ROOT/'build'/name).read_bytes()) for name in
                                    ('sandcore.img','sanddata.img','kernel.elf','kernel.sym')},source_inputs={})
    for path in sorted((ROOT/'build/fs/SYS/SRC').iterdir()):
        if path.is_file():report['source_inputs'][path.name]=phase.sha(path.read_bytes())
    def prepare(disk):
        c.disk_put(disk,'BIN/OLD.SCX',old);c.disk_put(disk,'BIN/NEW.SCX',g2)
        c.disk_put(disk,'SYS/SRC/MEM.C',source);c.disk_put(disk,'SYS/SRC/FRAMEBENCH.C',FRAME_SOURCE.encode('utf-8'))
        c.disk_put(disk,'SYS/DISPLAY.CFG',b'SCFG1MIO\nwidth=1024\nheight=768\nscale=100\n')
        c.disk_put(disk,'SYS/THEME.CFG',(ROOT/'build/fs/SYS/THEMES/AURORA.CFG').read_bytes())
    proc=t.launch('std',128,'scmem',prepare,floppy=(out/'sandcore.img').as_posix())
    phase.DISK=out/'sanddata-std-128-scmem.img'
    try:
        phase.wait(lambda:t.word(kernel['boot_stage'])==2,'welcome');t.open_shell(True);phase.idle()
        baseline=t.word(kernel['pf_used'])
        for name,driver in [('OLD','BIN/OLD.SCX'),('NEW','BIN/NEW.SCX')]:
            native,mapping,seconds=phase.compile_source(driver,'SYS/SRC/MEM.C','HOME/'+name+'.SCX')
            (out/(name+'.scx')).write_bytes(native);(out/(name+'.map')).write_bytes(mapping)
            address=phase.theme.native_symbols(mapping)['mem_probe'];path='HOME/MEM-'+name+'.BIN'
            q.text('run HOME/'+name+'.SCX '+path+'\n')
            win=phase.wait(lambda:t.windows()[-1] if len(t.windows())==2 else None,'memory-window')
            pd=t.word(kernel['tasks']+win['owner']*168)
            def state():return struct.unpack('<8i',q.memory(t.physical(pd,address),32))
            phase.wait(lambda:state()[0]==1,'memory-results',150)
            blob=c.file_content(phase.DISK,path);assert state()[1]==len(blob) and state()[2]==0
            assert blob[:8]==b'SMEM1MIO' and len(blob)==32+len(expected)+64
            assert struct.unpack_from('<6I',blob,8)==(1,cases,len(rects),len(expected),0,1 if name=='NEW' else 0)
            assert blob[32:-64]==expected,(name,'whole memory or clipped rectangle differs')
            assert blob[-64:]==struct.pack('<I',0x534D494F)*16
            (out/(name+'.bin')).write_bytes(blob);q.shot(name+'-memory-guards')
            q.key('esc');phase.idle();assert t.word(kernel['pf_used'])==baseline
            report['cases'][name]=dict(native_sha256=phase.sha(native),bytes=len(native),compile_seconds=seconds,
                                      whole_bytes='PASS',returns='PASS',strict_pages='PASS')
        native,mapping,seconds=phase.compile_source('BIN/NEW.SCX','SYS/SRC/FRAMEBENCH.C','HOME/FRAMEBENCH.SCX')
        floor=out/'floor-native';floor.mkdir()
        (floor/'framebench.scx').write_bytes(native);(floor/'framebench.map').write_bytes(mapping)
        artifact=dict(sha256=phase.sha(native),map_sha256=phase.sha(mapping),bytes=len(native),seconds=seconds,
                      source_sha256=phase.sha(FRAME_SOURCE.encode('utf-8')))
        floor_report=dict(author='mio',status='PASS',scope='NATIVE_COMPILE_ONLY',g2_sha256=phase.sha(g2),
            artifacts={'framebench':artifact},source_inputs=report['source_inputs'],inputs=report['inputs'],
            limitations='只有真正G2编译测试图案；不是游戏/宣传片或帧率验收')
        (floor/'results.json').write_text(json.dumps(floor_report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        report.update(status='PASS',limitations='内存/裁剪与页回收；缓存帧固定成本另测，不代替可玩与视觉')
        (out/'results.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print('SCMEM PASS',cases,len(rects),len(expected),flush=True)
    except Exception as error:
        report.update(status='FAIL',error=repr(error),traceback=traceback.format_exc())
        if proc.poll() is None:q.shot('failure');report['faults']=phase.faults()
        (out/'failure.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');raise
    finally:
        if proc.poll() is None:q.hmp('quit');proc.wait(timeout=10)


if __name__=='__main__':main()
