#!/usr/bin/env python3
"""mio：宿主执行真实 SCCC 前后端，用于缩短诊断迭代。

只替换系统调用适配层，词法/预处理/类型/语法/x86 生成仍取 user/ 实际
源码。生成的 SCX 随后必须在 Windows 双盘 QEMU 验证；这个脚本本身
不构成原生自编译证据。禁止将宿主 gcc 编译产物冒充 SCCC 输出。
"""
from pathlib import Path
import subprocess
from check_s3c_pp import ADAPTER,command
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'build/c-check'
MAIN=r'''
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    copy(log_path,"build/c-check/error.log",64);
    copy(map_path,"build/c-check/output.map",64);
    preprocess(argv[1]); compile_unit(); finish_image(argv[2]);
    fprintf(stderr,"stats tokens=%d types=%d symbols=%d fixes=%d source=%d pool=%d\n",
        token_count,type_count,symbol_count,fix_count,source_used,pool_used);
    return 0;
}
'''
def build():
    OUT.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'user/s3c.c').read_text(encoding='utf-8').split('int main(void)')[0]
    source=source.replace('#include "SCAPI.H"',ADAPTER)
    for name in ['s3c_lex.inc','s3c_pp.inc','s3c_parse.inc','s3c_emit.inc']:
        source=source.replace(f'#include "{name}"',(ROOT/'user'/name).read_text(encoding='utf-8'))
    (OUT/'harness.c').write_text(source+MAIN,encoding='utf-8')
    command(['gcc','-O2','-std=gnu99','build/c-check/harness.c','-o','build/c-check/harness'])
def compile(source,output):
    return command(['build/c-check/harness',source,output])
if __name__=='__main__':
    build()
    import sys
    source=sys.argv[1] if len(sys.argv)>1 else 'user/s3c.c'
    print(compile(source,'build/c-check/output.scx'))
