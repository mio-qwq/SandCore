#!/usr/bin/env python3
"""mio：预处理器的独立语义检查，直接编译真实 lex/pp 源码。

宿主适配只模拟文件读取与错误退出，不生成 SCX；这不是系统内自编译
证据。它用于在后端完成前发现宏边界错误；最终仍须 QEMU 原生全链路。
测试核对嵌套/别名/字符串原拼写/空参数拼接/条件短路/include 守卫。
"""
from pathlib import Path
import subprocess
import json

ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/pp-check'

ADAPTER=r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char u8;
typedef unsigned int u32;
static int length(const char *s) { return (int)strlen(s); }
static int equal(const char *a,const char *b) { return !strcmp(a,b); }
static void copy(char *d,const char *s,int n) { if(n) { snprintf(d,(size_t)n,"%s",s); } }
static void append(char *d,const char *s,int n) { int used=length(d); if(used<n) copy(d+used,s,n-used); }
static void decimal(char *d,int n) { sprintf(d,"%d",n); }
static void sc_exit(int n) { exit(n); }
static void sc_yield(void) {}
static void sc_puts(const char *s) { fputs(s,stderr); }
static const char *resolve(const char *p) {
    if(!strcmp(p,"SYS/INC/SCAPI.H")) return "user/SCAPI.H";
    return p;
}
static int sc_stat(const char *p,u32 *out) {
    FILE *f=fopen(resolve(p),"rb"); if(!f) return -1;
    fseek(f,0,SEEK_END); out[0]=1; out[1]=(u32)ftell(f); fclose(f); return 0;
}
static int sc_read(const char *p,void *out,int n) {
    FILE *f=fopen(resolve(p),"rb"); if(!f) return -1;
    int used=(int)fread(out,1,(size_t)n,f); fclose(f); return used;
}
static int sc_write(const char *p,const void *bytes,int n) {
    FILE *f=fopen(p,"wb"); if(!f) return -1;
    int used=(int)fwrite(bytes,1,(size_t)n,f); fclose(f); return used;
}
'''

MAIN=r'''
int main(int argc,char **argv) {
    if(argc!=2) return 2;
    copy(log_path,"build/pp-check/error.log",64);
    preprocess(argv[1]);
    for(int i=0;i<token_count;i++) {
        char text[1024]; token_text(tokens+i,text,sizeof(text));
        printf("%s\n",text);
    }
    return 0;
}
'''

def command(args):
    r=subprocess.run(['wsl','--cd',str(ROOT),'-e',*args],capture_output=True)
    if r.returncode:
        raise RuntimeError(r.stdout.decode(errors='replace')+r.stderr.decode(errors='replace'))
    return r.stdout.decode()

def run():
    OUT.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'user/s3c.c').read_text(encoding='utf-8').split('#include "s3c_parse.inc"')[0]
    source=source.replace('#include "SCAPI.H"',ADAPTER)
    source=source.replace('#include "s3c_lex.inc"',(ROOT/'user/s3c_lex.inc').read_text(encoding='utf-8'))
    source=source.replace('#include "s3c_pp.inc"',(ROOT/'user/s3c_pp.inc').read_text(encoding='utf-8'))
    (OUT/'harness.c').write_text(source+MAIN,encoding='utf-8')
    command(['gcc','-O2','-std=gnu99','build/pp-check/harness.c','-o','build/pp-check/harness'])
    fixture=r'''
#define INC(x) ((x)+1)
#define ALIAS INC
#define ID(x) x
#define TEXT(x) #x
#define CAT(x,y) x##y
#define WITH_EMPTY(x,y) prefix x##y
#define VAR(first,...) first + __VA_ARGS__
#define OBJECT (7)
#define LOOP LOOP
INC(INC(2))
ALIAS(3)
ID(INC)(4)
TEXT(a+b)
TEXT(a /* comment */ + b)
TEXT(0x10U)
CAT(he,llo)
WITH_EMPTY(,tail)
VAR(1,2,3)
OBJECT
LOOP
#if defined(INC) && !defined(NO) && (1 || 1/0)
chosen
#else
wrong
#endif
#undef OBJECT
#ifdef OBJECT
wrong2
#elif 4*3==12
elifchosen
#endif
#include "guard.inc"
#include "guard.inc"
'''
    (OUT/'guard.inc').write_text('#ifndef GUARD\n#define GUARD\nguarded\n#endif\n',encoding='utf-8')
    (OUT/'input.c').write_text(fixture,encoding='utf-8')
    tokens=command(['build/pp-check/harness','build/pp-check/input.c']).splitlines()
    expected=['(','(','(','(','2',')','+','1',')',')','+','1',')',
              '(','(','3',')','+','1',')','(','(','4',')','+','1',')',
              '"a+b"','"a + b"','"0x10U"','hello','prefix','tail','1','+','2',',','3',
              '(','7',')','LOOP','chosen','elifchosen','guarded']
    assert tokens==expected, json.dumps(dict(expected=expected,actual=tokens),ensure_ascii=False,indent=2)
    # 语法/后端源片段完成前，只核对已经存在的真实源片段；这里不会
    # 把缺少后端的源称为自编译成功，也不会给出任何 SCX 编译结果。
    self_source=(ROOT/'user/s3c.c').read_text(encoding='utf-8')
    for name in ['s3c_lex.inc','s3c_pp.inc','s3c_parse.inc','s3c_emit.inc']:
        self_source=self_source.replace(f'#include "{name}"',f'#include "../../user/{name}"' if (ROOT/'user'/name).exists() else '')
    (OUT/'self.c').write_text(self_source,encoding='utf-8')
    result=command(['build/pp-check/harness','build/pp-check/self.c']).splitlines()
    assert result and 'wrong' not in result
    (OUT/'results.json').write_text(json.dumps({'macro_cases':'PASS','available_self_source_preprocess_tokens':len(result),'scope':'host preprocessor only'},indent=2),encoding='utf-8')
    print('PASS',len(result),'self source tokens',flush=True)

if __name__=='__main__': run()
