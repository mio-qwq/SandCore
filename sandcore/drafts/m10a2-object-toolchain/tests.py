"""定向检查：真实SCCC前后端分别生成SCO，原生链接器同一C源码的宿主I/O路径。"""
from pathlib import Path
import ast
import hashlib
import json
import struct
import subprocess
import sys
import time
import prepare_compiler

ROOT = prepare_compiler.ROOT
HERE = prepare_compiler.HERE
OUT = ROOT / "build/m10a2-object-agent-tests"
VIEW = ROOT / "build/m10a2-object-agent-source"
sys.path.insert(0, str(ROOT / "tools"))
import scl_format
import scd_format


def run(args, ok=True):
    result = subprocess.run([str(a) for a in args], cwd=ROOT, capture_output=True, text=True)
    (OUT / "commands.log").open("a").write("$ " + " ".join(str(a) for a in args) + "\n" + result.stdout + result.stderr)
    if ok and result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    if not ok and not result.returncode:
        raise AssertionError("expected rejection: " + str(args))
    return result


def build():
    prepare_compiler.prepare(VIEW)
    tree = ast.parse((ROOT / "build/m10a2-sccc-host/host_check.py").read_text())
    adapter = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign)
                   and any(isinstance(t, ast.Name) and t.id == "ADAPTER" for t in n.targets))
    adapter = adapter.replace('if(!strncmp(p,"SYS/INC/",8)){snprintf(path,sizeof(path),"user/%s",p+8);return path;}',
        'if(!strncmp(p,"SYS/INC/",8)){const char *name=p+8;'
        'if(!strcmp(name,"stdio.h")||!strcmp(name,"stdlib.h")||!strcmp(name,"string.h")||!strcmp(name,"stddef.h")||!strcmp(name,"stdarg.h"))'
        'snprintf(path,sizeof(path),"user/stdlib/%s",name);else snprintf(path,sizeof(path),"user/%s",name);return path;}')
    # 适配只替换文件I/O；当前parser/真实x86发射与SCO writer不替换。
    source = (VIEW / "s3c.c").read_text().split("int main(void)")[0]
    source = source.replace('#include "SCAPI.H"', adapter)
    for name in ("COMPILECLI.inc", "COMPILEUI.inc"):
        source = source.replace(f'#include "{name}"', "")
    main = r'''
int main(int argc,char **argv){
 if(argc<4)return 2;
 copy(log_path,argv[2],64);append(log_path,".log",64);
 copy(map_path,argv[2],64);append(map_path,".map",64);
 if(!strcmp(argv[3],"--object")){shared_output=1;object_output=1;}
 else if(!strcmp(argv[3],"--dynamic"))dynamic_output=1;
 else if(!strcmp(argv[3],"--scl"))shared_output=1;
 else if(strcmp(argv[3],"--scx"))return 2;
 workspace_acquire();
 if(shared_output&&!object_output)scl_manifest_load(argv[4],argv[2]);
 if(dynamic_output)dynamic_manifest_load(argv[4],argv[2]);
 preprocess(argv[1]);compile_unit();finish_image(argv[2]);
 fprintf(stderr,"SCCC_BACKEND source=%d tokens=%d code=%d data=%d bss=%d fixes=%d\n",source_used,token_count,code_used,data_used,bss_used,fix_count);
 workspace_release();return 0;
}
'''
    (OUT / "compiler-adapter.c").write_text(source + main)
    flags = ["-O2", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
             "-Wno-unused-variable", "-Wno-misleading-indentation", "-I" + str(VIEW)]
    run(["gcc", *flags, OUT / "compiler-adapter.c", "-o", OUT / "sccc-host"])
    run(["gcc", "-O2", "-std=c99", "-Wall", "-Wextra", "-Werror", "-Wno-misleading-indentation",
         "-DSCLD_HOST", "-I" + str(HERE), "-I" + str(VIEW), HERE / "scld.c", "-o", OUT / "scld-host"])


def source(name, text):
    path = OUT / name
    path.write_text(text)
    return path.relative_to(ROOT)


def compile(name, text):
    path = source(name + ".c", text)
    obj = OUT / (name + ".SCO")
    run([OUT / "sccc-host", path, obj.relative_to(ROOT), "--object"])
    return obj


def manifest(name, value):
    return source(name + ".json", json.dumps(value))


def app(name="PROJECT.SCX", dependencies=None, imports=None):
    return dict(kind="application", name=name, dependencies=dependencies or [], imports=imports or [])


def u32(blob, off):
    return struct.unpack_from("<I", blob, off)[0]


def debug(blob, image):
    assert blob[:8] == b"SCDB1MIO"
    assert blob[32:64] == hashlib.sha256(image).digest()
    assert u32(blob, 24) == len(image)
    sy, ns, fi, nf, li, nl, st, nst = struct.unpack_from("<8I", blob, 64)
    assert (sy, fi, li, st, len(blob)) == (96, 96 + ns*24, 96 + ns*24 + nf*20,
        96 + ns*24 + nf*20 + nl*16, 96 + ns*24 + nf*20 + nl*16 + nst)
    strings = blob[st:]
    def text(off): return strings[off:strings.index(0,off)].decode()
    symbols = [dict(zip(("name","address","size","kind","binding","object"),
        struct.unpack_from("<6I",blob,sy+i*24))) for i in range(ns)]
    for s in symbols:s["name"]=text(s["name"])
    assert nl >= 2 and nf >= 2
    return symbols


def execute_i386(label, payload, base, body):
    """只运行本次生成的i386函数；Linux退出桥由测试自己提供，绕过SandCore CRT。"""
    binary=OUT/(label+".payload");binary.write_bytes(payload)
    assembly=OUT/(label+".s")
    assembly.write_text('.section .image,"awx"\n.incbin "'+str(binary)+'"\n'
        '.section .text\n.global _start\n_start:\n'+body+'\n')
    script=OUT/(label+".ld");script.write_text('ENTRY(_start)\nSECTIONS { . = '+hex(base)+'; .image : { *(.image) } . = 0x01000000; .text : { *(.text) } }\n')
    obj=OUT/(label+".o");exe=OUT/(label+".elf")
    run(["as","--32",assembly,"-o",obj]);run(["ld","-m","elf_i386","-T",script,obj,"-o",exe]);run([exe])


def main():
    OUT.mkdir(exist_ok=True)
    (OUT / "commands.log").write_text("")
    report = dict(scope="SCO_SEPARATE_TU_AND_NATIVE_LINKER_HOST_CHECK", guest_compile=False,
                  guest_runtime=False, executes_i386=False, cases=[])
    start=time.monotonic()
    try:
        build()
        a = compile("first", """
extern int add(int x); extern int shared;
static int private=3; int state[4]; int *state_pointer=state+1;
int main(void){state[1]=add(private);return state_pointer[0]-15+shared-7;}
""")
        b = compile("second", """
static int private=5; int shared=7;
int add(int x){return x+private+shared;}
""")
        spec=manifest("project",app());target=OUT/"PROJECT.SCX"
        run([OUT/"scld-host", target, "--dynamic", spec, a,b])
        parsed=scd_format.validate_scx(target.read_bytes())
        symbols=debug(target.with_suffix(".SCX.sdbg").read_bytes(),target.read_bytes())
        local=[s for s in symbols if s["name"]=="private"]
        assert len(local)==2 and all(s["binding"]==0 for s in local) and local[0]["address"]!=local[1]["address"]
        bss=next(s for s in symbols if s["name"]=="state")
        assert bss["address"]%4==0
        image=target.read_bytes();load=u32(image,12);memory=image[36:36+load]+bytes(u32(image,16))
        execute_i386("execute-scx",memory,0x400000,
            'mov $'+hex(0x400000+parsed["main_rva"])+',%eax\ncall *%eax\nmov %eax,%ebx\nmov $1,%eax\nint $0x80')
        report["executes_i386"]=True
        aligned=bytearray(a.read_bytes());struct.pack_into("<I",aligned,96+3*24+16,16)
        so,nsy=struct.unpack_from("<2I",aligned,36);strings=u32(aligned,68)
        for i in range(nsy):
            row=so+i*32;off=u32(aligned,row);name=aligned[strings+off:aligned.index(0,strings+off)]
            if name==b"state":struct.pack_into("<I",aligned,row+28,16<<8)
        align_obj=OUT/"ALIGN16.SCO";align_obj.write_bytes(aligned)
        run([OUT/"scld-host",target,"--dynamic",spec,align_obj,b])
        aligned_symbols=debug(Path(str(target)+".sdbg").read_bytes(),target.read_bytes())
        assert next(s for s in aligned_symbols if s["name"]=="state")["address"]%16==0
        assert u32(target.read_bytes(),12)%16==0
        report["cases"].append(dict(name="separate-TU-cross-call-data-private-BSS-debug",status="PASS",bytes=target.stat().st_size,format=parsed))
        unchanged=target.read_bytes()
        duplicate=compile("duplicate","int shared=9;")
        result=run([OUT/"scld-host",target,"--dynamic",spec,a,b,duplicate],False)
        assert "duplicate" in result.stderr and target.read_bytes()==unchanged
        missing=compile("missing","extern int absent(void);int main(void){return absent();}")
        result=run([OUT/"scld-host",target,"--dynamic",spec,missing],False)
        assert "absent" in result.stderr and target.read_bytes()==unchanged
        report["cases"].append(dict(name="duplicate-and-missing-preserve-output",status="PASS"))
        libmain=compile("libfirst","extern int plus(int x);int value=4;int init(void){return 0;}void fini(void){}int library(int x){return plus(x)+value;}")
        libother=compile("libsecond","static int value=2;int plus(int x){return x+value;}")
        ls=manifest("library",dict(name="PROJECT.SCL",abi_major=1,abi_minor=0,library_version=1,
            init="init",fini="fini",exports=[dict(name="library",kind="function"),dict(name="value",kind="data")],imports=[],dependencies=[]))
        lt=OUT/"PROJECT.SCL";run([OUT/"scld-host",lt,"--scl",ls,libmain,libother])
        lp=scl_format.validate(lt.read_bytes());debug(Path(str(lt)+".sdbg").read_bytes(),lt.read_bytes())
        assert lp["init_rva"]!=0xffffffff and lp["fini_rva"]!=0xffffffff
        assert len(lp["exports"])==2 and any(s["flags"]==1 for s in lp["segments"]) is False
        image=scl_format.relocated_image(lt.read_bytes(),0x500000)
        entry=0x500000+next(e["rva"] for e in lp["exports"] if e["name"]=="library")
        execute_i386("execute-scl",image,0x500000,
            'mov $'+hex(0x500000+lp["init_rva"])+',%eax\ncall *%eax\nsub $12,%esp\npush $3\nmov $'+hex(entry)+',%eax\ncall *%eax\nadd $16,%esp\n'
            'cmp $9,%eax\nsetne %bl\nmovzbl %bl,%ebx\npush %ebx\nmov $'+hex(0x500000+lp["fini_rva"])+',%eax\ncall *%eax\npop %ebx\nmov $1,%eax\nint $0x80')
        report["cases"].append(dict(name="multi-object-SCL-callbacks-and-private-exports",status="PASS",format=lp))
        ia=compile("import_a","extern int base_add(int x);extern int base_value;int use(int x){return base_add(x)+base_value;}")
        ib=compile("import_b","extern int use(int x);int main(void){return use(3);}")
        ims=manifest("imports",app(dependencies=[dict(name="BASE.SCL",abi_major=1,min_minor=0,min_version=1)],imports=[dict(name="base_add",dependency="BASE.SCL",kind="function",size=1),dict(name="base_value",dependency="BASE.SCL",kind="data",size=4)]))
        run([OUT/"scld-host",target,"--dynamic",ims,ia,ib]);ip=scd_format.validate_scx(target.read_bytes())
        assert len(ip["relocations"])==2
        report["cases"].append(dict(name="cross-object-dynamic-function-and-data-imports",status="PASS",format=ip))
        # 原单源dynamic同样修正metadata字符串变长后的BSS，旧reader继续验证。
        single=source("single.c","int state[4];int *p=state;int main(void){p[3]=7;return state[3]-7;}")
        run([OUT/"sccc-host",single,target.relative_to(ROOT),"--dynamic",spec]);sp=scd_format.validate_scx(target.read_bytes())
        assert u32(target.read_bytes(),12)%4==0
        report["cases"].append(dict(name="single-source-dynamic-BSS-alignment-compatible-reader",status="PASS",format=sp))
        # 破坏重定位补丁或对象ABI时必须在写出之前失败。
        bad=bytearray(a.read_bytes());rel=u32(bad,44);section=u32(bad,rel);offset=u32(bad,rel+4);payload=u32(bad,96+(section-1)*24+4);bad[payload+offset]=1
        badpath=OUT/"BAD.SCO";badpath.write_bytes(bad)
        preserved=target.read_bytes();run([OUT/"scld-host",target,"--dynamic",spec,badpath,b],False);assert target.read_bytes()==preserved
        wrong=compile("wrong","int add(int x,int y){return x+y;}int shared=7;")
        result=run([OUT/"scld-host",target,"--dynamic",spec,a,wrong],False);assert "ABI conflict" in result.stderr
        report["cases"].append(dict(name="malformed-relocation-and-cross-TU-ABI-reject",status="PASS"))
        aa=compile("aggregate_a","""
struct Pair{unsigned lo,hi;};extern struct Pair step(struct Pair value);
int main(void){struct Pair a={3,5};struct Pair b=step(a);return b.lo!=10 || b.hi!=7 || a.lo!=3;}
""")
        ab=compile("aggregate_b","""
struct Pair{unsigned lo,hi;};struct Pair step(struct Pair value){struct Pair out;out.lo=value.lo+7;out.hi=value.hi+2;value.lo=99;return out;}
""")
        run([OUT/"scld-host",target,"--dynamic",spec,aa,ab]);ag=target.read_bytes();ap=scd_format.validate_scx(ag)
        execute_i386("execute-aggregate",ag[36:36+u32(ag,12)]+bytes(u32(ag,16)),0x400000,
            'mov $'+hex(0x400000+ap["main_rva"])+',%eax\ncall *%eax\nmov %eax,%ebx\nmov $1,%eax\nint $0x80')
        report["cases"].append(dict(name="independent-TU-real-aggregate-by-value-hidden-sret-execution",status="PASS"))
        # 同一个真实后端生成独立原生链接器对象和编译器自身，检容量和C子集。
        native_obj=OUT/"SCLD.SCO"
        native_source=(HERE/"scld.c").relative_to(ROOT)
        run([OUT/"sccc-host",native_source,native_obj.relative_to(ROOT),"--object"])
        blob=native_obj.read_bytes();sy,nsy=struct.unpack_from("<2I",blob,36);st=u32(blob,68);decl=[]
        for i in range(nsy):
            name,binding,knd,section,value,size,abi,flags=struct.unpack_from("<8I",blob,sy+i*32)
            if not section:
                end=blob.index(0,st+name);name=blob[st+name:end].decode()
                decl.append(dict(name=name,kind="function" if knd==1 else "data",size=size,
                    dependency="SC32API.SCL" if name.startswith("Sc") else "SCCLIB.SCL"))
        native_manifest=app("SCLD.SCX",[dict(name="SC32API.SCL",abi_major=1,min_minor=3,min_version=5),dict(name="SCCLIB.SCL",abi_major=1,min_minor=0,min_version=1)],decl)
        nm=manifest("scld-native",native_manifest);nt=OUT/"SCLD.SCX"
        run([OUT/"scld-host",nt,"--dynamic",nm,native_obj]);np=scd_format.validate_scx(nt.read_bytes())
        standalone=OUT/"SCLD-DIRECT.SCX"
        # 输出basename仍必须与清单相同，单源bootstrap写另一个已建目录。
        direct_dir=OUT/"direct";direct_dir.mkdir(exist_ok=True);standalone=direct_dir/"SCLD.SCX"
        run([OUT/"sccc-host",native_source,standalone.relative_to(ROOT),"--dynamic",nm])
        scd_format.validate_scx(standalone.read_bytes())
        (HERE/"SCLD.json").write_text(json.dumps(native_manifest,indent=2)+"\n")
        compiler=OUT/"SCCC-SCO.SCX"
        result=run([OUT/"sccc-host",(VIEW/"sccc.c").relative_to(ROOT),compiler.relative_to(ROOT),"--scx"])
        cb=compiler.read_bytes();assert u32(cb,12)<=262108
        report["cases"].append(dict(name="real-backend-native-SCLD-and-compiler-budget",status="PASS",
            native_linker_bytes=nt.stat().st_size,native_linker_format=np,compiler_bytes=len(cb),compiler_load=u32(cb,12),old_load_limit=262108,
            native_linker_single_source_bytes=standalone.stat().st_size,
            old_load_remaining=262108-u32(cb,12),native_imports=decl,log=result.stderr))
        report["status"]="PASS"
    except Exception as error:
        report["status"]="FAIL";report["error"]=str(error);raise
    finally:
        report["wall_seconds"]=time.monotonic()-start
        report["inputs"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in [HERE/"scld.c",HERE/"s3c_sco.inc",HERE/"SCOFMT.H",HERE/"SCDBGFILE.H",HERE/"compiler.patch"] if p.exists()}
        (OUT/"report.json").write_text(json.dumps(report,indent=2)+"\n")
        print(json.dumps({"status":report["status"],"cases":len(report["cases"]),"out":str(OUT)}))


if __name__=="__main__":main()
