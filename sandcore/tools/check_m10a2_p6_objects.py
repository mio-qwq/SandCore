#!/usr/bin/env python3
"""用已构建SCCC/SCLD的真实后端检查P6原生对象；只有宿主IO，不称客体。"""
from pathlib import Path
import argparse,hashlib,json,os,struct,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
CASES={
 'SCLDR':('runtime/SCLROOT.C','runtime/SCLROOT.json',True),
 'SC32API':('sc32/SC32API.C','sc32/SC32API.json',True),
 'SCCLIB':('stdlib/SCCLIB.C','stdlib/SCCLIB.json',True),
 'SCLIB':('sclib/SCLIB.C','sclib/SCLIB.json',True),
 'SCUI':('scui/SCUI.C','scui/SCUI.json',True),
 'SCSHELL':('scshell/SCSHELL.C','scshell/SCSHELL.json',True),
 'SCGFX':('scgfx/SCGFX.C','scgfx/SCGFX.json',True),
 'SCTERM':('scterm/SCTERM.C','scterm/SCTERM.JSON',True),
 'FILES2':('files2/FILES2.C','files2/FILES2.json',False),
 'NOTES2':('notes2/NOTES2.C','notes2/NOTES2.json',False),
 'DEBUG2':('debug2/DEBUG2.C','debug2/DEBUG2.json',False),
 'CALC2':('systemtools/CALC2.C','systemtools/CALC2.json',False),
 'PALETTE2':('systemtools/PALETTE2.C','systemtools/PALETTE2.json',False),
 'MONITOR2':('systemtools/MONITOR2.C','systemtools/MONITOR2.json',False),
 'STUDIO2':('studio2/STUDIO2.C','studio2/STUDIO2.JSON',False),
 'CANVAS2':('imageapps/CANVAS2.C','imageapps/CANVAS2.json',False),
 'LENS2':('imageapps/LENS2.C','imageapps/LENS2.json',False),
 'SETTINGS2':('settings2/SETTINGS2.C','settings2/SETTINGS2.json',False),
 'INSTALLER2':('settings2/INSTALLER2.C','settings2/INSTALLER2.json',False),
 'TERMINAL2':('terminal2/TERMINAL2.C','terminal2/TERMINAL2.JSON',False),
 'SOUND2':('sound2/SOUND2.C','sound2/SOUND2.JSON',False),
 'SOUNDWORK':('sound2/SOUNDWORK.C','sound2/SOUNDWORK.JSON',False),
 'SCFILEWORK':('shellworker/SCFILEWORK.C','shellworker/SCFILEWORK.json',False),
 'DESKTOP':('scshell/desktop/SCSHELL.C','scshell/desktop/SCSHELL.json',False),
 'SCLD':('scld.c','SCLD.json',False),
 'SESSION2':('session2/SESSION2.C','session2/SESSION2.json',False),
 'LOGIN2':('session2/SESSION2.C','session2/LOGIN2.json',False),
}
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--case',action='append',choices=tuple(CASES))
    p.add_argument('--compiler',type=Path,default=ROOT/'build/m10a2-terminal2-agent-native/sccc-host')
    args=p.parse_args();out=args.out.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists():raise ValueError('需要build内新目录')
    compiler=args.compiler.resolve(strict=True)
    linker=ROOT/'build/m10a2-terminal2-agent-native/scld-host'
    out.mkdir();report=dict(scope='P6_REAL_SCCC_SCO_SCLD_WITH_HOST_IO',guest_compile=False,guest_runtime=False,
        compiler_sha256=sha(compiler),linker_sha256=sha(linker),driver_sha256=sha(Path(__file__)),cases=[])
    catalogs={};versions={}
    for name,folder in [('SCLDR','runtime'),('SC32API','sc32'),('SCCLIB','stdlib'),('SCLIB','sclib'),
                        ('SCUI','scui'),('SCSHELL','scshell'),('SCGFX','scgfx'),('SCTERM','scterm'),('SCSOUND','scsound')]:
        template=ROOT/'user'/folder/(('SCLROOT' if name=='SCLDR' else name)+('.JSON' if name=='SCTERM' else '.json'))
        doc=json.loads(template.read_text(encoding='utf-8'));lib=name+'.SCL'
        versions[lib]=(doc['abi_major'],doc['abi_minor'],doc['library_version'])
        for e in doc['exports']:
            if e['name'] in catalogs:raise ValueError('重复provider:'+e['name'])
            catalogs[e['name']]=lib
    environment=os.environ.copy();environment.pop('SCCC_INCLUDE',None)
    def run(command,log):
        result=subprocess.run(list(map(str,command)),cwd=ROOT,env=environment,capture_output=True,text=True)
        (out/log).write_text(result.stdout+result.stderr,encoding='utf-8');return result
    selected=args.case or list(CASES)
    for name in selected:
        source,template,is_library=CASES[name];obj=out/(name+'.SCO');started=time.monotonic()
        result=run([compiler,'user/'+source,obj.relative_to(ROOT),'--object'],name+'-compile.log')
        item=dict(name=name,source='user/'+source,source_sha256=sha(ROOT/'user'/source),compile_exit=result.returncode)
        if not result.returncode:
            try:
                blob=obj.read_bytes();sym,count=struct.unpack_from('<2I',blob,36);strings=struct.unpack_from('<I',blob,68)[0]
                imports=[]
                def string(offset):return blob[strings+offset:blob.index(0,strings+offset)].decode('utf-8')
                for i in range(count):
                    offset,binding,kind,section,value,size,abi,flags=struct.unpack_from('<8I',blob,sym+i*32)
                    if not section:
                        symbol=string(offset);imports.append(dict(name=symbol,dependency=catalogs[symbol],kind='function' if kind==1 else 'data',size=size))
                manifest=json.loads((ROOT/'user'/template).read_text(encoding='utf-8'))
                manifest['imports']=imports;manifest['dependencies']=[]
                for lib in sorted({i['dependency'] for i in imports}):
                    major,minor,version=versions[lib]
                    manifest['dependencies'].append(dict(name=lib,abi_major=major,min_minor=minor,min_version=version))
                original=set(i['name'] for i in json.loads((ROOT/'user'/template).read_text(encoding='utf-8'))['imports'])
                actual=set(i['name'] for i in imports)
                target=out/(name+'.NATIVE.JSON');target.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
                image=out/(manifest['name'])
                result=run([linker,image.relative_to(ROOT),'--scl' if is_library else '--dynamic',target.relative_to(ROOT),obj.relative_to(ROOT)],name+'-link.log')
                item.update(imports=len(imports),object_sha256=sha(obj),manifest_sha256=sha(target),link_exit=result.returncode,
                    additional_native_imports=sorted(actual-original),host_only_imports=sorted(original-actual))
                files,nfiles=struct.unpack_from('<2I',blob,52);inputs={}
                for i in range(nfiles):
                    offset,byte_count,fnv,reserved=struct.unpack_from('<4I',blob,files+i*16)
                    source_name=string(offset)
                    if source_name.lstrip('/').startswith('SYS/INC/'):
                        base=source_name.lstrip('/')[8:]
                        candidates=[ROOT/'user'/base,ROOT/'user/stdlib'/base,ROOT/'user/sclib'/base,ROOT/'user/runtime'/base]
                    else:candidates=[ROOT/source_name.lstrip('/')]
                    path=next((q.resolve() for q in candidates if q.is_file()),None)
                    if path is None:raise ValueError('实际源文件无法回溯：'+source_name)
                    data=path.read_bytes();actual_fnv=2166136261
                    for value in data:actual_fnv=((actual_fnv^value)*16777619)&0xffffffff
                    if len(data)!=byte_count or actual_fnv!=fnv:raise ValueError('SCO来源内容已改变：'+source_name)
                    inputs[path.relative_to(ROOT).as_posix()]=sha(path)
                item['actual_compiler_input_sha256']=inputs
                if image.is_file():item.update(image_bytes=image.stat().st_size,image_sha256=sha(image),image_path=image.relative_to(ROOT).as_posix())
            except Exception as error:item['error']=str(error)
        item['seconds']=time.monotonic()-started
        item['status']='PASS' if item.get('compile_exit')==0 and item.get('link_exit')==0 and not item.get('error') else 'FAIL'
        report['cases'].append(item);print(name,item['status'],item.get('error',''),flush=True)
        (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    report['status']='PASS' if all(i['status']=='PASS' for i in report['cases']) else 'FAIL'
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    return 0 if report['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
