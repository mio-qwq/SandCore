"""只读核验本任务实际构建/staging；不复制盘、不构建或运行客体。"""
from pathlib import Path
import hashlib,importlib.util,json,shlex,struct,sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];OUT=ROOT/'build/m10a2-p6-final-12';CORE=OUT/'core'
sys.path.insert(0,str(ROOT/'tools'))
from mkcore import inspect_elf
from mkfs_m10 import check_main
from bindings import Binding,CORE_READER_SHA

# 复用本人冻结的ELF实际节/flat解析器；只调用纯读取函数，不调用旧main。
BASIS=ROOT/'drafts/m10a2-p6-native-core-receipt/make_receipt.py'
if hashlib.sha256(BASIS.read_bytes()).hexdigest()!=CORE_READER_SHA:raise ValueError('冻结CORE读取helper原像不同')
spec=importlib.util.spec_from_file_location('frozen_core_read_helpers',BASIS);basis=importlib.util.module_from_spec(spec);spec.loader.exec_module(basis)
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def digest(b):return hashlib.sha256(b).hexdigest()
def rel(p):return p.resolve().relative_to(ROOT).as_posix()
def pin(p):return dict(path=rel(p),bytes=p.stat().st_size,sha256=sha(p))
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def save(p,v):p.write_text(json.dumps(v,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def require(v,why):
    if not v:raise ValueError(why)
def main():
    run=load(OUT/'finalization.json');require(run['status']=='BUILT_AND_STAGED_ARTIFACT_INSPECTION_PENDING' and len(run['steps'])==3 and all(v['exit_code']==0 for v in run['steps']),'实际三步骤未成功')
    bound=Binding(run['native_report']['sha256'],run['current_refresh_binding']['vm_report']['sha256'],run['host_report']['sha256'])
    bound.layout(ROOT/run['layout_report']['path'],run['layout_report']['sha256'])
    require(run['native_generation_binding']==bound.metadata and run['runtime_build_binding']==bound.runtime_metadata and run['layout_reuse_evidence']==bound.layout_reuse,'实际生成/运行/布局复用分层记录不一致')
    for step in run['steps']:require(sha(ROOT/step['log'])==step['log_sha256'],'真实步骤日志摘要改变')
    require(run['core_source_files_changed_during_build']==[],'实际CORE构建源不守恒')
    npath=ROOT/run['native_report']['path'];require(sha(npath)==run['native_report']['sha256'],'native报告已变化');native=load(npath)
    hpath=ROOT/run['host_report']['path'];require(sha(hpath)==run['host_report']['sha256'],'HOST21已变化');host=load(hpath);pins={v['path']:v['sha256'] for v in host['inputs']}
    before=load(ROOT/run['core_source_before']['path']);require(all(sha(ROOT/p)==s for p,s in before.items()),'构建前固定源现有变化')
    dependencies={};headers={};generated_sources={};d_files={}
    for p in sorted(CORE.rglob('*.d')):
        first=p.read_text(encoding='utf-8').replace('\\\n',' ').splitlines()[0];target,names=first.split(':',1);names=shlex.split(names)
        d_files[rel(p)]=dict(sha256=sha(p),target=target,dependency_count=len(names))
        for name in names:
            q=(ROOT/name).resolve(strict=True);key=rel(q);value=sha(q)
            if q.is_relative_to(CORE):
                (headers if q.suffix.lower()=='.h' else generated_sources)[key]=value
            else:require(pins.get(key)==value and before.get(key)==value,'实际.d源不是固定HOST21/构建前输入：'+key);dependencies[key]=value
    log=(OUT/'core-build-01.log').read_text(encoding='utf-8',errors='replace');asm={}
    for line in log.splitlines():
        if line.startswith('/usr/bin/nasm '):
            args=shlex.split(line);name=next(x for x in args if x.endswith('.asm'));require(before.get(name)==sha(ROOT/name),'汇编源不是构建前输入');asm[name]=sha(ROOT/name)
    flat=(CORE/'core.bin').read_bytes();packed=(CORE/'fs/SYS/CORE/CORE.SKM').read_bytes();check_main(packed);symbols=inspect_elf(CORE/'core.elf');check,sym=basis.elf(CORE/'core.elf',flat)
    require(packed[128:]==flat,'CORE正文与flat不符');h=struct.unpack_from('<16I',packed,8)
    require(h[7]==symbols['__core_base']==check['base'] and h[8]==symbols['_core_start']-check['base'] and h[9]==len(flat)==symbols['__core_image_end']-check['base'] and h[10]==symbols['__bss_start']-check['base'] and h[11]==symbols['__bss_end']-symbols['__bss_start'],'CORE/ELF实际布局不同')
    def read_symbol(label,size):return basis.from_flat(flat,check['base'],basis.scalar_symbol(sym,label),size)
    issued=[]
    for name,label in [('LOGIN2.SCX','login_issued_sha256'),('SCSHELL.SCX','shell_host_issued_sha256')]:
        row=native['native_artifacts'][name];p=Path(row['path']);require(sha(p)==row['sha256'] and p.stat().st_size==row['bytes'] and read_symbol(label,32).hex()==row['sha256'],'实际CORE不是当前bundle程序摘要')
        issued.append(dict(name=name,source=pin(p),actual_compiled_address=basis.scalar_symbol(sym,label),generation_origin=row.get('generation_origin'),compile_case=row['compile_case']))
    resources=[];wanted={}
    for name in ('SCLDR.SCL','SC32API.SCL','SCCLIB.SCL','SCLIB.SCL','SCUI.SCL'):
        original=native['native_libraries'][name];p=OUT/'sdk/SYS/LIB'/name;require(sha(p)==original['sha256'] and p.stat().st_size==original['bytes'],'SDK库不是当前native bundle')
        wanted['SYS/LIB/'+name]=p
    wanted['SYS/UI/UAX15_1.SCU']=OUT/'sdk/SYS/UI/UAX15_1.SCU'
    require(sha(wanted['SYS/UI/UAX15_1.SCU'])==sha(ROOT/'assets/ui/UAX15_1.SCU'),'UAX实际源码/SDK副本不同')
    table=read_symbol('login_issued_resources',6*40);found=set()
    for i in range(6):
        row=table[i*40:(i+1)*40];p,size=struct.unpack_from('<2I',row);name=basis.from_flat(flat,check['base'],p,64).split(b'\0',1)[0].decode('ascii')
        require(name in wanted and name not in found and wanted[name].stat().st_size==size and sha(wanted[name])==row[8:].hex(),'CORE六条实际发行资源与SDK/native不同');found.add(name);resources.append(dict(destination=name,source=pin(wanted[name])))
    require(found==set(wanted),'CORE发行表不完整')
    key=ROOT/'assets/trust/M10-OWNER-ED25519.PUB';require(key.stat().st_size==32 and read_symbol('core_public_key',32)==key.read_bytes(),'实际CORE公钥与明确公开输入不同')
    loader=(CORE/'loader.bin').read_bytes();loader_check,_=basis.elf(CORE/'loader.elf',loader);boot=(CORE/'boot.bin').read_bytes();floppy=(CORE/'sandcore.img').read_bytes()
    require(len(boot)==512 and boot[-2:]==b'\x55\xaa' and len(loader)<=128*512 and len(floppy)==1474560 and floppy[:512]==boot and floppy[512:512+len(loader)]==loader and not any(floppy[512+len(loader):]),'新软盘不等于实际boot/loader/零填充')
    manifest=load(OUT/'sdk/MANIFEST.json');hashes=load(OUT/'sdk/HASHES.json')['files']
    for name,row in hashes.items():
        p=OUT/'sdk'/name;require(p.stat().st_size==row['bytes'] and sha(p)==row['sha256'],'SDK最终文件摘要不同：'+name)
    closure=dict(schema=1,scope='ACTUAL_CORE_BUILD_INPUT_SNAPSHOT_AND_D_DEPENDENCIES',before_snapshot=run['core_source_before'],d_files=d_files,non_generated_compile_sources=dependencies,assembler_sources=asm,generated_headers=headers,generated_private_sources=generated_sources)
    save(HERE/'core-build-inputs.json',closure)
    core_receipt=dict(schema=1,status='ACTUAL_CORE_BUILD_AND_NATIVE_BINDINGS_VERIFIED_NOT_GUEST_BOOT',build_step=run['steps'][2],source_before=run['core_source_before'],watched_count=len(before),source_files_changed_during_build=run['core_source_files_changed_during_build'],reviewer_executed_and_observed_process=False,build_execution_evidence='FINALIZER_RECORDED_ACTUAL_SUBPROCESS_AND_LOG',inspector_verified_recorded_execution=True,preparation_agent_executed_build=False,
        layout_report=run['layout_report'],native_generation_binding=bound.metadata,runtime_build_binding=bound.runtime_metadata,layout_reuse_evidence=bound.layout_reuse,
        artifacts={n:pin(CORE/n) for n in ('core.elf','core.bin','core.sym','fs/SYS/CORE/CORE.SKM','boot.bin','loader.elf','loader.bin','loader.sym','sandcore.img')},
        core_layout=dict(base=h[7],entry_rva=h[8],image_bytes=h[9],bss_offset=h[10],bss_bytes=h[11],memory_bytes=h[12]),actual_elf_flat=check,actual_loader_elf_flat=loader_check,core_payload_sha256=digest(flat),actual_compiled_programs=issued,actual_compiled_resources=resources,public_key=pin(key),private_key_accessed=False,dependency_closure=pin(HERE/'core-build-inputs.json'),guest_boot=False,guest_runtime_verified=False)
    save(HERE/'core-build-receipt.json',core_receipt)
    summary=dict(schema=1,status='STRICT_NATIVE_PREVIEW_STAGED_NOT_RUNTIME_ACCEPTANCE',steps=run['steps'][:2],profile=manifest['profile'],native_report=run['native_report'],host_report=run['host_report'],codec_proof=run['codec_proof'],manifest=pin(OUT/'sdk/MANIFEST.json'),provenance=pin(OUT/'sdk/PROVENANCE.json'),hashes=pin(OUT/'sdk/HASHES.json'),sha256sums=pin(OUT/'sdk/SHA256SUMS'),payload_files=len(manifest['files']),payload_bytes=sum(v['bytes'] for v in manifest['files'].values()),libraries={n:dict(source=v['origin']['source'],sha256=v['origin']['sha256'],bytes=v['origin']['bytes'],abi_major=v['abi_major'],abi_minor=v['abi_minor'],library_version=v['library_version'],generation_case=v['origin'].get('generation_case'),origin_report=v['origin'].get('origin_report')) for n,v in manifest['libraries'].items()},compiler=manifest['compiler'],linker=manifest['linker'],source_input_sha256_scope='FULL_STRICT_STAGER_MANIFEST_PROVENANCE_AND_ORIGINAL_REPORTS',layout_report=run['layout_report'],native_generation_binding=bound.metadata,runtime_build_binding=bound.runtime_metadata,layout_reuse_evidence=bound.layout_reuse,all_recorded_payload_and_manifest_hashes_checked=True,SDK_runtime_verified=False,guest_boot=False)
    save(HERE/'sdk-staging-receipt.json',summary)
    require(not bound.checked.unchanged(),'只读核验期间明确输入改变')
    run.update(status='CORE_AND_STRICT_SDK_VERIFIED_NOT_GUEST_BOOT',core_receipt=pin(HERE/'core-build-receipt.json'),sdk_receipt=pin(HERE/'sdk-staging-receipt.json'),sdk_payload_files=summary['payload_files'],sdk_payload_bytes=summary['payload_bytes']);save(OUT/'finalization.json',run)
    print(json.dumps(dict(status=run['status'],core_sha256=core_receipt['artifacts']['fs/SYS/CORE/CORE.SKM']['sha256'],sdk_files=summary['payload_files'],sdk_bytes=summary['payload_bytes']),indent=2))
if __name__=='__main__':main()
