from pathlib import Path
import json,shlex,shutil,subprocess,sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
import build_m10a2_p6 as host

old=ROOT/'build/m10a2-main-product-fix-03'
new=ROOT/'build/m10a2-main-product-fix-04'
if new.exists():raise FileExistsError(new)
shutil.copytree(old,new,ignore=shutil.ignore_patterns('*.log','p6-unified-host-*.json'))
for p in new.rglob('*.d'):
    p.write_bytes(p.read_bytes().replace(b'build/m10a2-main-product-fix-03',b'build/m10a2-main-product-fix-04'))

# 编译仍实际由WSL make执行；源码枚举与摘要在Windows本地完成，避免
# 大量跨WSL挂载文件操作。沿原构建器保存同一编译命令和实际输出。
actual_call=subprocess.call
launches=[]
def wsl_make(command,**kwargs):
    assert command[0]=='make'
    cwd='/mnt/c/Users/Administrator/Desktop/projectos/sandcore'
    argv=['wsl.exe','sh','-lc','cd '+shlex.quote(cwd)+' && PYTHONDONTWRITEBYTECODE=1 '+shlex.join(command)]
    launches.append(argv)
    return actual_call(argv,**kwargs)
host.subprocess.call=wsl_make
sys.argv=[host.__file__,'--tag','p6-unified-host-26','--build',str(new)]
code=host.main()
report=new/'p6-unified-host-26.json'
r=json.loads(report.read_bytes())
r['actual_host_launcher']=launches
r['source_hashing_host']='WINDOWS_LOCAL_FILESYSTEM'
report.write_text(json.dumps(r,indent=2)+'\n',encoding='utf-8')
raise SystemExit(code)
