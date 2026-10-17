"""只编译一个新UP探针并按实际SCO导入生成清单，不改正式应用或来源记录。"""
from pathlib import Path
import hashlib,json,os,shlex,struct,subprocess

ROOT=Path(__file__).resolve().parents[2]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    out=ROOT/'build/m10a2-drop-release-probe-03';out.mkdir(exist_ok=False)
    compiler=ROOT/'build/m10a2-p6-adapter-02/sccc-host'
    linker=ROOT/'build/m10a2-terminal2-agent-native/scld-host'
    assert sha(compiler)=='a6dfe92930876400b349ddc33924ac2c5bae76a5dcb8995c1bbbf93b08760696'
    source=ROOT/'drafts/m10a2-drop-release-probe/DROPUP.C'
    obj=out/'UP.SCO'
    root='/mnt/'+ROOT.drive[0].lower()+ROOT.as_posix()[2:]
    def run(args,log):
        command='cd '+shlex.quote(root)+' && '+' '.join(shlex.quote(x.relative_to(ROOT).as_posix() if isinstance(x,Path) else x) for x in args)
        result=subprocess.run(['wsl.exe','sh','-lc',command],capture_output=True)
        (out/log).write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError(log+': '+(result.stdout+result.stderr).decode('utf-8','replace'))
    run([compiler,source,obj,'--object'],'compile.log')
    catalog={};versions={}
    for folder,stem,suffix in (('runtime','SCLROOT','.json'),('sc32','SC32API','.json'),('stdlib','SCCLIB','.json'),('sclib','SCLIB','.json'),('scui','SCUI','.json')):
        doc=json.loads((ROOT/'user'/folder/(stem+suffix)).read_bytes());library=doc['name']
        versions[library]=(doc['abi_major'],doc['abi_minor'],doc['library_version'])
        for symbol in doc['exports']:catalog[symbol['name']]=library
    body=obj.read_bytes();start,count=struct.unpack_from('<2I',body,36);strings=struct.unpack_from('<I',body,68)[0]
    imports=[]
    for i in range(count):
        name,binding,kind,section,value,size,abi,flags=struct.unpack_from('<8I',body,start+i*32)
        if section:continue
        name=body[strings+name:body.index(0,strings+name)].decode('utf-8')
        imports.append(dict(name=name,dependency=catalog[name],kind='function' if kind==1 else 'data',size=size))
    manifest=json.loads((ROOT/'user/systemtools/CALC2.NATIVE.JSON').read_bytes())
    manifest.update(name='UP.SCX',imports=imports,dependencies=[])
    for library in sorted({row['dependency'] for row in imports}):
        major,minor,version=versions[library]
        manifest['dependencies'].append(dict(name=library,abi_major=major,min_minor=minor,min_version=version))
    target=out/'UP.JSON';target.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    run([linker,out/'UP.SCX','--dynamic',target,obj],'link.log')
    report=dict(status='ONE_PROBE_SCCC_BACKEND_AND_STRICT_LINK_PASS_GUEST_PENDING',guest_compile=False,guest_runtime=False,
        compiler_sha256=sha(compiler),linker_sha256=sha(linker),imports=len(imports),
        files={str(p.relative_to(ROOT)):dict(bytes=p.stat().st_size,sha256=sha(p)) for p in (source,target,obj,out/'UP.SCX')})
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(report['status'],len(imports))


if __name__=='__main__':main()
