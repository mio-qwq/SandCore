#!/usr/bin/env python3
"""只替换宿主IO；正文始终从当前SCCC源读取，不替代客体编译证据。"""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();out=args.out.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('需要build内新目录')
    old=ROOT/'build/m10a2-terminal2-agent-native/compiler.c'
    text=old.read_text(encoding='utf-8')
    adapter=text[text.index('#include <stdio.h>'):text.index('#ifdef SCCC_CLI')]
    start=adapter.index('    if(!strncmp(p,"SYS/INC/",8))');end=adapter.index('    return p;',start)
    adapter=adapter[:start]+'''    if(!strncmp(p,"SYS/INC/",8)){const char *name=p+8;struct stat st;
        snprintf(path,sizeof(path),"user/stdlib/%s",name);
        if(!stat(path,&st) && S_ISREG(st.st_mode))return path;
        if(!strcmp(name,"SCLIB.H"))snprintf(path,sizeof(path),"user/sclib/%s",name);
        else if(!strcmp(name,"SCLDR.H"))snprintf(path,sizeof(path),"user/runtime/%s",name);
        else snprintf(path,sizeof(path),"user/%s",name);
        return path;
    }
'''+adapter[end:]
    source=ROOT/'user/s3c.c';body=source.read_text(encoding='utf-8').split('int main(void)')[0]
    assert body.count('#include "SCAPI.H"')==1
    body=body.replace('#include "SCAPI.H"',adapter)
    for name in ('COMPILECLI.inc','COMPILEUI.inc'):body=body.replace('#include "'+name+'"','')
    body+='''\nint main(int argc,char **argv){
    if(argc!=4 || strcmp(argv[3],"--object"))return 2;
    copy(log_path,argv[2],64);append(log_path,".log",64);
    copy(map_path,argv[2],64);append(map_path,".map",64);
    object_output=shared_output=1;workspace_acquire();preprocess(argv[1]);compile_unit();
    finish_image(argv[2]);workspace_release();return 0;
}\n'''
    out.mkdir();candidate=out/'compiler.c';candidate.write_text(body,encoding='utf-8')
    binary=out/'sccc-host'
    command=['gcc','-O2','-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
        '-Wno-misleading-indentation','-Iuser',str(candidate),'-o',str(binary)]
    result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True)
    (out/'build.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    report=dict(scope='CURRENT_SCCC_REAL_BACKEND_HOST_IO_ADAPTER',guest_compiled=False,exit_code=result.returncode,
        driver_sha256=sha(Path(__file__)),adapter_origin=str(old.relative_to(ROOT)),adapter_origin_sha256=sha(old),
        source_sha256=sha(source),generated_source_sha256=sha(candidate),command=command)
    if binary.exists():report['binary_sha256']=sha(binary)
    (out/'build.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('HOST_ADAPTER_BUILD',result.returncode);return result.returncode
if __name__=='__main__':raise SystemExit(main())
