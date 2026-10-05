#!/usr/bin/env python3
"""mio：仅拆开同一行的多条 C 语句，保留字符串/注释/for 头。

用于细化格式，不重写表达式或注释。源码仍须构建和原生 QEMU 验证；
脚本不会编辑用户字体，也不会把文本格式检查当作运行测试。
"""
from pathlib import Path
import sys

def format_source(s):
    out=[];state='code';paren=0;delta=0;indent='';i=0;start=True
    while i<len(s):
        c=s[i];n=s[i+1] if i+1<len(s) else ''
        if start:
            k=i
            while k<len(s) and s[k] in ' \t': k+=1
            indent=s[i:k];start=False;delta=0
        if state in ['str','char']:
            out.append(c)
            if c=='\\' and n: out.append(n);i+=2;continue
            if c==('"' if state=='str' else "'"):state='code'
        elif state=='line':
            out.append(c)
            if c=='\n':state='code'
        elif state=='block':
            out.append(c)
            if c=='*' and n=='/':out.append(n);i+=2;state='code';continue
        else:
            if c=='/' and n in '/*':state='line' if n=='/' else 'block';out.extend([c,n]);i+=2;continue
            if c=='"':state='str'
            elif c=="'":state='char'
            elif c=='(':paren+=1
            elif c==')':paren-=1
            elif c=='{':delta+=1
            elif c=='}':delta-=1
            out.append(c)
            if c==';' and paren==0:
                j=i+1
                while j<len(s) and s[j] in ' \t':j+=1
                if j<len(s) and s[j] not in '\r\n}':
                    out.append('\n'+indent+'    '*max(0,delta));i=j;continue
        if c=='\n':start=True
        i+=1
    return ''.join(out)

if __name__=='__main__':
    for name in sys.argv[1:]:
        path=Path(name);path.write_text(format_source(path.read_text(encoding='utf-8-sig')),encoding='utf-8')
