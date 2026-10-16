"""仅宿主32位整数算术对照；不编译SCUI，不构建SCX/CORE或运行VM。"""
from pathlib import Path
import hashlib,json,subprocess
import prepare_patch as p
HERE=Path(__file__).resolve().parent
HARNESS=r'''
#include <stdint.h>
#include <stdio.h>
typedef uint32_t ScU32;
HELPER
static uint32_t over(uint32_t s,uint32_t d){
    uint32_t a=s>>24,b=d>>24;if(!a)return d;if(a==255 || !b)return s;
    if(b==255){uint32_t out=0xff000000u;
        for(uint32_t k=0;k<24;k+=8)out|=(((((s>>k)&255)*a+((d>>k)&255)*(255-a)+127)/255)<<k);
        return out;}
    return ui_argb_over_partial(s,d);
}
/* 参考使用独立64位有理数公式；被测生产片段只使用uint32。 */
static uint32_t reference(uint32_t s,uint32_t d){
    uint64_t a=s>>24,b=d>>24,n=a*255+b*(255-a);if(!a)return d;
    uint32_t out=(uint32_t)((n+127)/255)<<24;
    for(unsigned k=0;k<24;k+=8){uint64_t c=((s>>k)&255)*a*255+((d>>k)&255)*b*(255-a);
        out|=(uint32_t)((c+n/2)/n)<<k;}return out;
}
int main(void){
    uint32_t alpha[]={0,1,127,128,254,255},rgb[]={0,0xffffff,0xBBA15D,0x1172CE};unsigned cases=0;
    for(unsigned a=0;a<6;a++)for(unsigned b=0;b<6;b++)for(unsigned c=0;c<4;c++){
        uint32_t s=alpha[a]<<24|rgb[c],d=alpha[b]<<24|rgb[3-c];cases++;
        if(over(s,d)!=reference(s,d)){fprintf(stderr,"mismatch %08x %08x\n",s,d);return 1;}
    }
    if(over(0x01000000,0)!=0x01000000 || over(0x13BBA15D,0)!=0x13BBA15D || over(0x80ffffff,0x00112233)!=0x80ffffff || over(0x80ff0000,0x800000ff)!=0xC0AA0055)return 2;
    /* Opaque目标保留旧式逐位结果；此小循环不是UI/显示测试矩阵。 */
    for(unsigned a=0;a<256;a++)for(unsigned c=0;c<4;c++){
        uint32_t s=a<<24|rgb[c],d=0xff000000|rgb[3-c],old=0xff000000;
        for(unsigned k=0;k<24;k+=8)old|=(((((s>>k)&255)*a+((d>>k)&255)*(255-a)+127)/255)<<k);
        if(over(s,d)!=old)return 3;
        cases++;
    }
    printf("STRAIGHT_ARGB_U32_ARITHMETIC_PASS cases=%u known_regressions=4 opaque_old_exact=true\n",cases);return 0;
}
'''
def main():
    p.candidates();source=HERE/'host-arithmetic.c';binary=HERE/'host-arithmetic'
    source.write_text(HARNESS.replace('HELPER',p.SLOW_HELPER),encoding='utf-8')
    commands=[];report=dict(scope='HOST_I386_INTEGER_ARITHMETIC_ONLY_NOT_SCUI_OR_GUEST',guest=False,full_ui_compiled=False)
    try:
        for args in (['gcc','-m32','-O2','-std=c99','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],[str(binary)]):
            r=subprocess.run(args,capture_output=True,text=True);commands.append(dict(args=args,exit_code=r.returncode,stdout=r.stdout,stderr=r.stderr))
            if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        report['status']='PASS'
    except Exception as e:report.update(status='FAIL',error=str(e));raise
    finally:
        report.update(commands=commands,source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),helper_sha256=hashlib.sha256(p.SLOW_HELPER.encode()).hexdigest())
        (HERE/'arithmetic-report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(report['status'])
if __name__=='__main__':main()
