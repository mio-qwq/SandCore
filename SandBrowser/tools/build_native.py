"""Copyright (c) 2026 mio. 隔离的 i386 构建，所有输出均在本工程。"""
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib,json,os,subprocess,sys,struct,threading

ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
os.environ['TMPDIR']=str(ROOT/'build/tmp')
os.environ['PYTHONDONTWRITEBYTECODE']='1'
sys.dont_write_bytecode=True
SDK=ROOT.parent/'sandcore'
sys.path.insert(0,str(SDK/'tools'))
import mkdscx
from pack_native import pack

FLAGS=['-m32','-Os','-U_FORTIFY_SOURCE','-D_FORTIFY_SOURCE=0','-ffreestanding','-fno-builtin','-fno-pic','-fno-pie','-fno-stack-protector',
 '-fno-asynchronous-unwind-tables','-fno-unwind-tables','-ffunction-sections','-fdata-sections',
 '-mincoming-stack-boundary=2','-mpreferred-stack-boundary=4','-mfpmath=387','-mno-sse','-mno-mmx',
 '-fno-ident','-DNDEBUG','-Iinclude','-Ibuild/generated','-I'+str(SDK/'user'),
 '-I/usr/include/x86_64-linux-gnu','-Ivendor/stb-master','-Ivendor/stb-master/deprecated',
 '-Ibuild/sysroot/usr/include/x86_64-linux-gnu/c++/15/32']
LOCK=threading.Lock()
LOG=ROOT/'build/compile.log'

def write(path,data):
 p=Path(path);p.parent.mkdir(parents=True,exist_ok=True)
 if not p.exists() or p.read_text()!=data:p.write_text(data)

def run(cmd,fail=True):
 r=subprocess.run([str(x) for x in cmd],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
 with LOCK:
  with LOG.open('a') as f:f.write('\n$ '+' '.join(str(x) for x in cmd)+'\n'+r.stdout)
 if fail and r.returncode:raise RuntimeError(r.stdout[-7000:])
 return r

def object_file(source,extra=()):
 source=Path(source);name=source.as_posix().replace('/','_').replace('.','_')
 obj=ROOT/'build/obj'/(name+'.o');flags=FLAGS+list(extra)
 cxx=source.suffix=='.cpp'
 if cxx:flags=[f for f in flags if f!='-ffreestanding']
 cmd=['g++' if cxx else 'gcc',*flags,*(['-std=c++17','-fno-exceptions','-fno-rtti','-fno-threadsafe-statics','-fno-gnu-unique','-include','include/cxx_port.h'] if cxx else ['-std=c11']),'-MMD','-MP','-c',source,'-o',obj]
 stamp=hashlib.sha256(' '.join(str(x) for x in cmd).encode()).hexdigest()
 stampfile=obj.with_suffix('.key')
 if obj.exists() and stampfile.exists() and stampfile.read_text()==stamp:
  dep=obj.with_suffix('.d')
  paths=dep.read_text().replace('\\\n',' ').split(':',1)[1].split() if dep.exists() else [str(source)]
  valid=[Path(p) for p in paths if not p.endswith(':')]
  if valid and all(p.exists() and p.stat().st_mtime_ns<=obj.stat().st_mtime_ns for p in valid):return obj
 run(cmd);stampfile.write_text(stamp);return obj

def objects(sources,extra=()):
 sources=list(sources);out={}
 with ThreadPoolExecutor(max_workers=6) as pool:
  tasks={pool.submit(object_file,s,extra):s for s in sources}
  for i,f in enumerate(as_completed(tasks),1):
   out[tasks[f]]=f.result()
   if i%20==0:print('Compiled',i,'/',len(tasks),flush=True)
 return [out[s] for s in sources]

def prepare_config():
 # musl 的内部三角函数与 glibc 的私有别名重名，只给移植的 libm 使用其原生公有声明。
 math_header=(ROOT/'vendor/musl-1.2.5/include/math.h').read_text().replace('#include <bits/alltypes.h>','typedef long double float_t;\ntypedef long double double_t;')
 write('build/musl/math.h',math_header)
 write('build/generated/miniz_export.h','#define MINIZ_EXPORT\n')
 p=ROOT/'vendor/duktape-2.7.0/src/duk_config.h'
 config=p.read_text()
 config=config.replace('#undef DUK_USE_INTERRUPT_COUNTER','#define DUK_USE_INTERRUPT_COUNTER')
 config=config.replace('#undef DUK_USE_EXEC_TIMEOUT_CHECK','#define DUK_USE_EXEC_TIMEOUT_CHECK(udata) sb_js_timeout(udata)')
 config=config.replace('#undef DUK_USE_DATE_GET_NOW','#define DUK_USE_DATE_GET_NOW(ctx) sb_date_now()')
 config=config.replace('#undef DUK_USE_DATE_GET_LOCAL_TZOFFSET','#define DUK_USE_DATE_GET_LOCAL_TZOFFSET(d) sb_date_tz(d)')
 config=config.replace('#define DUK_USE_CALLSTACK_LIMIT 10000','#define DUK_USE_CALLSTACK_LIMIT 256')
 config=config.replace('#define DUK_USE_COMPILER_RECLIMIT 2500','#define DUK_USE_COMPILER_RECLIMIT 256')
 config=config.replace('#define DUK_USE_JSON_DEC_RECLIMIT 1000','#define DUK_USE_JSON_DEC_RECLIMIT 128')
 for flag in ('DATE_PRS_STRPTIME','DATE_FMT_STRFTIME','DATE_TZO_GMTIME_R','DATE_TZO_GMTIME','DATE_NOW_GETTIMEOFDAY','JX','JC'):
  config=config.replace('#define DUK_USE_'+flag+'\n','#undef DUK_USE_'+flag+'\n')
 config='#ifdef __cplusplus\nextern "C" {\n#endif\nint sb_js_timeout(void *);\ndouble sb_date_now(void);\nint sb_date_tz(double);\n#ifdef __cplusplus\n}\n#endif\n'+config
 write('build/generated/duk_config.h',config)
 write('build/generated/duktape.h',(ROOT/'vendor/duktape-2.7.0/src/duktape.h').read_text())
 # 源文件的引号 include 会先找本目录，使用工作副本保证私有配置生效。
 write('build/generated/duktape.c',(ROOT/'vendor/duktape-2.7.0/src/duktape.c').read_text())

def provider_symbols():
 paths={
 'SC32API.SCL':SDK/'user/sc32/SC32API.NATIVE.JSON',
 'SCCLIB.SCL':SDK/'user/stdlib/SCCLIB.NATIVE.JSON'}
 providers={}
 for name,path in paths.items():
  if not path.exists():path=path.with_name(path.name.replace('.NATIVE',''))
  m=json.loads(path.read_text())
  for entry in m['exports']:
   if entry['kind']=='function':providers[entry['name']]=name
 return providers

def undefined(path):
 out=run(['nm','-u',path]).stdout
 return sorted({line.split()[-1] for line in out.splitlines() if line.strip()})

def make_stubs(names,providers,tag):
 body=['.text']
 imports=[]
 for name in names:
  if name not in providers:continue
  imports.append(dict(name=name,dependency=providers[name]))
  body.extend([f'.globl {name}',f'.type {name},@function',f'{name}:',f'jmp *sb_slot_{name}',f'.size {name},.-{name}'])
 body+=['.data']
 for item in imports:body.extend([f'.globl sb_slot_{item["name"]}',f'sb_slot_{item["name"]}: .long 0'])
 source=ROOT/'build/generated'/(tag+'-imports.S');write(source,'\n'.join(body)+'\n')
 obj=object_file(source)
 return obj,imports

def module(name,entry,objs,cxx=False,additional=(),exports=None):
 exports=exports or [entry]
 keep=[v for n in exports for v in ('-u',n)]
 tag=name.split('.')[0].lower();base=ROOT/'build'/tag
 libs=[]
 if cxx:
  archive=ROOT/'build/sysroot/usr/lib/gcc/x86_64-linux-gnu/15/32/libstdc++.a'
  members=run(['ar','t',archive]).stdout.splitlines()
  for member in ('tree.o','list.o','hashtable_c++0x.o','hash_bytes.o'):
   if member in members:
    target=ROOT/'build'/member
    target.write_bytes(subprocess.check_output(['ar','p',str(archive),member]));libs.append(target)
 libs.append(ROOT/'build/sysroot/usr/lib/gcc/x86_64-linux-gnu/15/32/libgcc.a')
 # 先作归并，精确取得需要动态导入的标准库/SC32 符号。
 partial=base.with_suffix('.partial.o')
 run(['ld','-m','elf_i386','-r','--gc-sections',*keep,'-u','SbRuntimeInit','-u','SbRuntimeFini','-T','src/module.ld','-o',partial,*objs,*additional,'--start-group',*libs,'--end-group'])
 unresolved=undefined(partial);providers=provider_symbols()
 missing=[n for n in unresolved if n not in providers and n!='_GLOBAL_OFFSET_TABLE_']
 if missing:raise RuntimeError(name+' needs native implementations: '+', '.join(missing))
 stubs,imports=make_stubs(unresolved,providers,tag)
 elf=base.with_suffix('.elf')
 run(['ld','-m','elf_i386','--no-relax','--gc-sections','--emit-relocs','-e',entry,*keep,'-u','SbRuntimeInit','-u','SbRuntimeFini','-T','src/native.ld','-Map='+str(base.with_suffix('.map')),'-o',elf,*objs,stubs,*additional,'--start-group',*libs,'--end-group'])
 payload=pack(elf,name,exports,imports)
 out=ROOT/'build/payload'/name;out.parent.mkdir(parents=True,exist_ok=True);out.write_bytes(payload)
 report=dict(name=name,bytes=len(payload),sha256=hashlib.sha256(payload).hexdigest(),imports=imports,entry=entry,exports=exports)
 write(base.with_suffix('.json'),json.dumps(report,indent=2))
 print(name,len(payload),'bytes;',len(imports),'imports',flush=True)
 return report

def installer(common):
 common=[p for p in common if p.name!='src_runtime_c.o']
 # 同一源码的独立对象名，避免覆盖三套 SCL 共用的带构造表对象。
 write('build/generated/setup_runtime.c','#define SB_NO_CTORS\n#include "../../src/runtime.c"\n')
 common.append(object_file(Path('build/generated/setup_runtime.c')))
 inflate=objects([Path('vendor/miniz-3.0.2/miniz_tinfl.c')],['-DMINIZ_NO_ARCHIVE_APIS','-DMINIZ_NO_ZLIB_APIS','-DMINIZ_NO_STDIO'])
 app=objects([Path('src/installer.c')],['-Ivendor/miniz-3.0.2'])
 elf=ROOT/'build/setup.o'
 run(['ld','-m','elf_i386','-r','--gc-sections','-u','main','-T','src/application.ld','-o',elf,*common,*inflate,*app,ROOT/'build/sysroot/usr/lib/gcc/x86_64-linux-gnu/15/32/libgcc.a'])
 run(['objcopy','--strip-unneeded',elf])
 providers=provider_symbols();names=undefined(elf);missing=[n for n in names if n not in providers]
 if missing:raise RuntimeError('SETUP.SCX needs '+', '.join(missing))
 deps=sorted({providers[n] for n in names})
 manifest=dict(kind='application',name='SETUP.SCX',dependencies=[dict(name=n,abi_major=1,min_minor=0,min_version=1) for n in deps],imports=[dict(name=n,kind='function',dependency=providers[n],size=1) for n in names])
 blob=mkdscx.pack_elf(elf.read_bytes(),manifest);(ROOT/'dist/SETUP.SCX').write_bytes(blob)
 write('build/setup.json',json.dumps(dict(bytes=len(blob),sha256=hashlib.sha256(blob).hexdigest(),**manifest),indent=2))
 print('SETUP.SCX',len(blob),'bytes',flush=True)

def main():
 for d in ('build/tmp','build/obj','build/generated','build/payload'):Path(d).mkdir(parents=True,exist_ok=True)
 prepare_config();LOG.touch(exist_ok=True)
 if len(sys.argv)<2:raise SystemExit('build_native.py page|net|ui|all')
 mode=sys.argv[1]
 common=objects([Path('src')/n for n in ('common.c','entropy.c','raw.c','url.c','draw.c','window.c','runtime.c','jump.S')])
 math_names=['sin','cos','tan','asin','acos','atan','atan2','sinh','cosh','tanh','asinh','acosh','atanh','exp','expm1','exp2','exp_data','log','log1p','log_data','log2','log2_data','log10','pow','pow_data','sqrt','sqrt_data','cbrt','hypot','fmod','fabs','floor','ceil','trunc','round','copysign','scalbn','scalbnl','frexp','modf','nextafter','ldexp','__sin','__cos','__tan','__expo2','__rem_pio2','__rem_pio2_large','__math_invalid','__math_divzero','__math_oflow','__math_uflow','__math_xflow','__math_invalidl','__fpclassify','__signbit','__fpclassifyl','__signbitl']
 math_sources=[Path('vendor/musl-1.2.5/src/math')/(n+'.c') for n in math_names]
 math_sources.append(Path('vendor/musl-1.2.5/src/math/sqrtl.c'))
 math_sources=[p for p in math_sources if p.exists()]
 math=objects(math_sources,['-Ibuild/musl','-Ivendor/musl-1.2.5/src/internal','-Ivendor/musl-1.2.5/arch/generic','-include','include/musl_compat.h']) if mode in ('page','all') else []
 if mode in ('page','all'):
  lh=objects([p for p in Path('vendor/litehtml-0.9/src').glob('*.cpp') if p.name!='tstring_view.cpp'],['-DLITEHTML_NO_THREADS','-Ivendor/litehtml-0.9/include','-Ivendor/litehtml-0.9/include/litehtml','-Ivendor/litehtml-0.9/src/gumbo/include'])
  gumbo_src=list(Path('vendor/litehtml-0.9/src/gumbo').rglob('*.c'))
  gumbo_src=[p for p in gumbo_src if '/tests/' not in str(p) and '/examples/' not in str(p)]
  gumbo=objects(gumbo_src,['-Ivendor/litehtml-0.9/src/gumbo/include','-Ivendor/litehtml-0.9/src/gumbo/include/gumbo'])
  duk=objects([Path('build/generated/duktape.c')],['-Wno-implicit-fallthrough'])
  app=objects([Path('src/page.cpp'),Path('src/cxx_runtime.cpp'),Path('src/gif.c')],['-Ivendor/litehtml-0.9/include'])
  module('SBPAGE.SCL','SbPageMain',common+math+lh+gumbo+duk+app,True)
 if mode in ('net','all'):
  bear=objects(list(Path('vendor/bearssl-0.6/src').rglob('*.c')),['-Ivendor/bearssl-0.6/inc','-Ivendor/bearssl-0.6/src','-DBR_USE_URANDOM=0','-DBR_USE_UNIX_TIME=0','-DBR_RDRAND=0','-DBR_AES_X86NI=0','-DBR_SSE2=0'])
  inflate=objects([Path('vendor/miniz-3.0.2/miniz_tinfl.c')],['-DMINIZ_NO_ARCHIVE_APIS','-DMINIZ_NO_ZLIB_APIS','-DMINIZ_NO_STDIO'])
  app=objects([Path('src/net.c')],['-Ivendor/bearssl-0.6/inc','-Ivendor/miniz-3.0.2'])
  module('SBNET.SCL','SbNetMain',common+bear+inflate+app,exports=['SbNetMain','SbNetCreate','SbNetFetch','SbNetRelease','SbNetDestroy'])
 if mode in ('ui','all'):
  app=objects([Path('src/browser.c')]);module('SBUI.SCL','SbBrowserMain',common+math+app)
 if mode in ('installer','all'):installer(common)
 for name,entry,lib in [('BROWSER.SCX','SbBrowserMain','SBUI.SCL'),('WEBPAGE.SCX','SbPageMain','SBPAGE.SCL'),('WEBNET.SCX','SbNetMain','SBNET.SCL')]:
  obj=object_file(Path('src/launch.c'),['-DSB_ENTRY='+entry])
  m=dict(kind='application',name=name,dependencies=[dict(name=lib,abi_major=1,min_minor=0,min_version=1)],imports=[dict(name=entry,kind='function',dependency=lib,size=1)])
  blob=mkdscx.pack_elf(obj.read_bytes(),m);(ROOT/'build/payload'/name).write_bytes(blob)
 print('Build finished; guest execution is reserved for user verification.',flush=True)

if __name__=='__main__':
 try:main()
 except Exception as exc:print('BUILD FAILED:',exc,file=sys.stderr);sys.exit(1)
