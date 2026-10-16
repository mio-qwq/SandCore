"""只读现存CORE/日志/来源与依赖；只在本目录写静态凭证，不执行构建或客体。"""
from pathlib import Path
import hashlib,json,re,shlex,struct,sys,zlib
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from mkcore import inspect_elf
from mkfs_m10 import check_main

CORE=ROOT/'build/m10a2-p6-final-01/core'
RUN=ROOT/'build/m10a2-p6-guest-generation-run-03'
LOG=CORE.parent/'core-build-01.log'
observed={}
def digest(body):return hashlib.sha256(body).hexdigest()
def rel(path):return path.resolve().relative_to(ROOT).as_posix()
def read(path):
    path=path.resolve(strict=True)
    if not path.is_relative_to(ROOT):raise ValueError('读取离开工作区')
    body=path.read_bytes();wanted=digest(body)
    if path in observed and observed[path]!=wanted:raise ValueError('凭证读取期间输入变化')
    observed[path]=wanted;return body
def pin(path):
    body=read(path);return dict(path=rel(path),bytes=len(body),sha256=digest(body))
def load(path):return json.loads(read(path).decode('utf-8'))
def save(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def require(value,why):
    if not value:raise ValueError(why)

def elf(path,flat):
    body=read(path);h=struct.unpack_from('<16sHHIIIIIHHHHHH',body)
    require(h[0][:7]==b'\x7fELF\x01\x01\x01' and h[1:3]==(2,3),'不是ELF32/i386 executable')
    sections=[struct.unpack_from('<10I',body,h[6]+i*40) for i in range(h[12])]
    symbols={}
    for s in sections:
        if s[1]!=2:continue
        strings=sections[s[6]];names=body[strings[4]:strings[4]+strings[5]]
        for at in range(s[4],s[4]+s[5],16):
            name,value,size,info,other,index=struct.unpack_from('<IIIBBH',body,at)
            if not name:continue
            label=names[name:names.index(0,name)].decode('ascii')
            symbols.setdefault(label,[]).append(dict(address=value,size=size,index=index,info=info))
    alloc=[s for s in sections if s[2]&2 and s[5] and s[1]!=8]
    base=min(s[3] for s in alloc);coverage=bytearray(len(flat));matches=[]
    for s in alloc:
        at=s[3]-base;blob=body[s[4]:s[4]+s[5]]
        require(at>=0 and at+len(blob)<=len(flat) and flat[at:at+len(blob)]==blob,'ELF实际ALLOC正文与flat不等')
        require(not any(coverage[at:at+len(blob)]),'ELF重叠ALLOC节')
        coverage[at:at+len(blob)]=bytes([1])*len(blob)
        matches.append(dict(address=s[3],bytes=s[5],flags=s[2],sha256=digest(blob)))
    require(all(touched or value==0 for touched,value in zip(coverage,flat)),'flat的ELF对齐空隙不是零')
    return dict(base=base,entry=h[4],sections=matches,padding_bytes=coverage.count(0)),symbols

def scalar_symbol(symbols,label):
    rows=symbols.get(label,[]);require(len(rows)==1,'发行/公钥符号缺失或歧义：'+label);return rows[0]['address']
def from_flat(flat,base,address,count):
    at=address-base;require(at>=0 and count<=len(flat)-at,'实际符号超出flat');return flat[at:at+count]
def header_array(body,label):
    value=re.search(r'\b'+re.escape(label)+r'\[32\]=\{([^}]+)\}',body.decode('utf-8'))
    require(value is not None,'生成头缺发行数组');return bytes(int(x.strip(),0) for x in value[1].split(','))

def main():
    native=load(RUN/'p6-generation.json');pre=native['preflight']
    require(native['status']=='DECLARED_P6_NATIVE_GENERATION_PASS' and native['current_inputs_unchanged'] is True and native['source_disk_unchanged'] is True and native['changed_input_output_paths']==[],'run03尚无完整成功来源')
    account=next(x for x in native['cases'] if x['name']=='complete-generation-single-owner-page-account')
    require(account['status']=='PASS' and account['physical_delta']==0,'run03 owner账不符')
    host_path=Path(pre['build_report']);host=load(host_path)
    require(digest(read(host_path))==pre['build_report_sha256'] and host['exit_code']==0 and host['source_files_modified_during_build']==[],'HOST03来源不符')
    historical={v['path']:v['sha256'] for v in host['inputs']};host_outputs={v['path']:v for v in host['outputs']}
    host_dir=Path(host.get('build_directory',pre['build']))
    if not host_dir.is_absolute():host_dir=ROOT/host_dir
    log=read(LOG).decode('utf-8');log_lines=log.splitlines();commands=[]
    for line_no,line in enumerate(log_lines,1):
        if line.startswith(('gcc ','ld ','/usr/bin/nasm ','python3 ','objcopy ','nm ')):
            commands.append(dict(line=line_no,args=shlex.split(line)))
    c_commands=[c for c in commands if c['args'][0]=='gcc' and '-c' in c['args']]
    link_commands=[c for c in commands if c['args'][0]=='ld']
    require(len(link_commands)==2,'日志不是固定CORE/loader两次链接')
    d_files={};source_files={};generated={};d_targets=set()
    for path in sorted(CORE.rglob('*.d')):
        first=read(path).decode('utf-8').replace('\\\n',' ').splitlines()[0];target,tail=first.split(':',1)
        target_path=(ROOT/target).resolve(strict=True);require(target_path.is_relative_to(CORE),'依赖目标越界');d_targets.add(rel(target_path))
        names=shlex.split(tail);d_files[rel(path)]=dict(sha256=observed[path.resolve()],target=rel(target_path),dependencies=len(names))
        for name in names:
            file=(ROOT/name).resolve(strict=True);row=pin(file)
            if file.is_relative_to(CORE):generated[rel(file)]=row
            else:
                require(historical.get(rel(file))==row['sha256'],'HOST03实际.d源码缺失/漂移：'+rel(file));source_files[rel(file)]=row['sha256']
    require(d_targets=={rel(ROOT/c['args'][c['args'].index('-o')+1]) for c in c_commands},'.d目标与日志实际C编译对象不一致')
    asm_inputs={};objects={}
    for command in commands:
        args=command['args']
        if args[0]=='/usr/bin/nasm':
            source=next(x for x in args if x.endswith('.asm'));row=pin(ROOT/source)
            require(historical.get(source)==row['sha256'],'HOST03汇编源码不符');asm_inputs[source]=row
    for command in link_commands:
        args=command['args']
        for name in args[1:args.index('-o')]:
            if not name.endswith('.o'):continue
            body=read(ROOT/name);require(body[:7]==b'\x7fELF\x01\x01\x01' and struct.unpack_from('<2H',body,16)==(1,3),'链接输入非真实i386对象')
            objects[name]=dict(bytes=len(body),sha256=digest(body))
    require(set(objects)==d_targets|{rel(ROOT/c['args'][c['args'].index('-o')+1]) for c in commands if c['args'][0]=='/usr/bin/nasm' and '-f' in c['args'] and c['args'][c['args'].index('-f')+1]=='elf32'},'实际链接对象与编译来源不闭合')
    build_inputs={}
    for name in ('Makefile','m9.mk','m10.mk','m10a2.mk','core.ld','boot/loader.ld','tools/mkcore.py','tools/mkimg.py','tools/mk_login_issue.py','tools/mk_shell_issue.py','tools/mkcore_public_key.py','tools/mkfont.py','tools/mkfont8.py','tools/prepare_lwip_dhcp.py','kernel/font16.txt','assets/font/phoenix-ascii.json','assets/trust/M10-OWNER-ED25519.PUB'):
        row=pin(ROOT/name);old=historical.get(name)
        if old is not None:require(row['sha256']==old,'HOST03生成器/规则输入漂移：'+name)
        row['host03_input_sha256']=old;row['snapshot_scope']='EXACT_HOST03_INPUT' if old else 'CURRENT_EXPLICIT_INPUT_NOT_PRESENT_IN_HOST03_SOURCE_SNAPSHOT';build_inputs[name]=row
    artifacts={name:pin(CORE/name) for name in ('boot.bin','loader.elf','loader.bin','loader.sym','sandcore.img','core.elf','core.bin','core.sym','fs/SYS/CORE/CORE.SKM')}
    flat=read(CORE/'core.bin');packed=read(CORE/'fs/SYS/CORE/CORE.SKM');check_main(packed);sym=inspect_elf(CORE/'core.elf')
    require(packed[128:]==flat,'CORE正文不等于flat')
    header=struct.unpack_from('<16I',packed,8)
    require(sym['__core_base']==0x800000 and header[8]==sym['_core_start']-sym['__core_base'] and header[9]==len(flat)==sym['__core_image_end']-sym['__core_base'] and header[10]==sym['__bss_start']-sym['__core_base'] and header[11]==sym['__bss_end']-sym['__bss_start'],'ELF/CORE实际布局不符')
    elf_check,symbols=elf(CORE/'core.elf',flat);loader_check,_=elf(CORE/'loader.elf',read(CORE/'loader.bin'))
    boot=read(CORE/'boot.bin');loader=read(CORE/'loader.bin');image=read(CORE/'sandcore.img')
    require(len(boot)==512 and boot[-2:]==b'\x55\xaa' and len(loader)<=128*512 and len(image)==1474560 and image[:512]==boot and image[512:512+len(loader)]==loader and not any(image[512+len(loader):]),'真实软盘不是boot+loader+零填充')
    bindings=[]
    for name,label,header_name in [('LOGIN2.SCX','login_issued_sha256','login_program_issue.h'),('SCSHELL.SCX','shell_host_issued_sha256','shell_host_issue.h')]:
        item=native['native_artifacts'][name];body=read(Path(item['path']));require(item['guest_sccc_compiled'] and item['generated_by_actual_scld'] and len(body)==item['bytes'] and digest(body)==item['sha256'],'native发行源与run03不符')
        value=from_flat(flat,elf_check['base'],scalar_symbol(symbols,label),32)
        require(value.hex()==digest(body) and header_array(read(CORE/header_name),label)==value,'实际CORE发行程序摘要不符')
        require(re.search(r'issued_bytes=(\d+)u;',read(CORE/header_name).decode())[1]==str(len(body)),'生成头程序长度不符')
        bindings.append(dict(name=name,native_source=pin(Path(item['path'])),actual_compiled_digest_address=scalar_symbol(symbols,label),sha256=value.hex(),compile_case=item['compile_case']))
    expected={'SYS/LIB/'+n:native['native_libraries'][n] for n in ('SCLDR.SCL','SC32API.SCL','SCCLIB.SCL','SCLIB.SCL','SCUI.SCL')}
    uax=read(ROOT/'assets/ui/UAX15_1.SCU');uax_pin=pre['bootstrap']['SYS/UI/UAX15_1.SCU'];require(digest(uax)==uax_pin['sha256'] and len(uax)==uax_pin['bytes'],'UAX与run03固定资源不同')
    expected['SYS/UI/UAX15_1.SCU']=dict(path=str(ROOT/'assets/ui/UAX15_1.SCU'),sha256=digest(uax),bytes=len(uax))
    resources=[];found=set();table=scalar_symbol(symbols,'login_issued_resources')
    for i in range(6):
        row=from_flat(flat,elf_check['base'],table+i*40,40);address,size=struct.unpack_from('<2I',row);text=from_flat(flat,elf_check['base'],address,64).split(b'\0',1)[0].decode('ascii')
        require(text in expected and text not in found,'实际CORE资源表集合不同');found.add(text);wanted=expected[text];source=pin(Path(wanted['path']))
        require(size==wanted['bytes']==source['bytes'] and row[8:].hex()==wanted['sha256']==source['sha256'],'实际CORE六条资源摘要不符')
        resources.append(dict(destination=text,actual_compiled_record=table+i*40,source=source))
    require(found==set(expected),'实际CORE资源集合不完整')
    key=read(ROOT/'assets/trust/M10-OWNER-ED25519.PUB');require(len(key)==32 and header_array(read(CORE/'core_public_key.h'),'core_public_key')==key,'公钥与生成头不符')
    require(from_flat(flat,elf_check['base'],scalar_symbol(symbols,'core_public_key'),32)==key,'实际CORE不是指定公开key')
    old_flat=read(host_dir/'core.bin');old_elf=pin(host_dir/'core.elf');old_flat_pin=pin(host_dir/'core.bin')
    for item in (old_elf,old_flat_pin):
        require(host_outputs[item['path']]['sha256']==item['sha256'] and host_outputs[item['path']]['bytes']==item['bytes'],'HOST03实际CORE不是原输出')
    old_symbols=inspect_elf(host_dir/'core.elf');require(from_flat(old_flat,old_symbols['__core_base'],old_symbols['core_public_key'],32)==key,'新CORE公钥配置不同于HOST03实际CORE')
    for name,row in generated.items():
        counterpart=host_dir/Path(name).relative_to(CORE.relative_to(ROOT).as_posix())
        row['host03_counterpart']=pin(counterpart) if counterpart.is_file() else None
        row['same_bytes_as_host03_counterpart']=row['host03_counterpart'] is not None and row['sha256']==row['host03_counterpart']['sha256']
    required_log='CORE: image=%d B, BSS=%d B, entry=%d, sha256=%s'%(len(flat),header[11],header[8],digest(flat));require(required_log in log,'日志最终CORE包装结果不符')
    excluded='tools/verify_m10a2_scl.py';excluded_pin=pin(ROOT/excluded)
    excluded_pin.update(host03_input_sha256=historical.get(excluded),in_actual_compile_dependency_closure=excluded in source_files)
    require(not excluded_pin['in_actual_compile_dependency_closure'],'可选metadata工具意外进入编译闭包')
    index=dict(schema=1,scope='READ_ONLY_CURRENT_CORE_COMPILED_INPUTS_NOT_BUILD_BEFORE_AFTER_WITNESS',d_files=d_files,non_generated_compile_sources=source_files,assembler_sources=asm_inputs,linked_objects=objects,generated_headers={n:v for n,v in generated.items() if n.lower().endswith('.h')},generated_private_sources={n:v for n,v in generated.items() if not n.lower().endswith('.h')},build_generation_inputs=build_inputs,excluded_non_compile_tool_change=excluded_pin)
    index_path=HERE/'core-build-inputs.json';save(index_path,index)
    command=next(line.strip() for line in read(ROOT/'drafts/m10a2-p6-final-flow/CORE-SDK.md').decode('utf-8').splitlines() if line.strip().startswith('make -j4 BUILD='))
    receipt=dict(schema=1,status='STATIC_ARTIFACTS_AND_NATIVE_ISSUANCE_VERIFIED_NOT_GUEST_BOOT',date='2026-10-16',scope='READ_ONLY_EXISTING_CORE_BUILD_RECEIPT',build_command=command,
        build_exit=dict(reported_exit_code=0,source='Root explicit successful execution statement',reviewer_observed_process=False,log_final_package_message=required_log),
        log=pin(LOG),artifacts=artifacts,core=dict(image_bytes=len(flat),bss_offset=header[10],bss_bytes=header[11],memory_bytes=header[12],entry_rva=header[8],payload_sha256=digest(flat),elf_flat=elf_check,skm2_header_crc_and_payload_sha256_valid=True),loader=dict(elf_flat=loader_check,floppy_exact_boot_loader_and_zero_padding=True),
        native_origin=dict(report=pin(RUN/'p6-generation.json'),status=native['status'],libraries=len(native['native_libraries']),artifacts=len(native['native_artifacts']),objects=len(native['objects']),original_report_source_inputs_unchanged=native['current_inputs_unchanged'],original_report_source_disk_unchanged=native['source_disk_unchanged'],original_report_owner_physical_delta=account['physical_delta']),
        host03_source=dict(report=pin(host_path),run03_preflight_report_sha256=pre['build_report_sha256'],exact_actual_d_source_files=len(source_files),actual_d_files=len(d_files),all_current_compile_dependencies_equal_host03_source_pins=True),
        generated_headers=dict(count=sum(n.endswith('.h') for n in generated),listed_separately_in=rel(index_path),native_issue_headers_intentionally_differ_from_host_bootstrap=True),
        issued_programs=bindings,actual_compiled_login_resources=resources,public_key=dict(source=pin(ROOT/'assets/trust/M10-OWNER-ED25519.PUB'),configured=True,actual_compiled_address=scalar_symbol(symbols,'core_public_key'),same_bytes_as_host03_actual_compiled_key=True,host03_original_raw_key_snapshot_present='assets/trust/M10-OWNER-ED25519.PUB' in historical,private_key_accessed=False),
        inputs_index=pin(index_path),inspector_script=pin(Path(__file__)),reviewer_observed_build_before_after=False,observed_input_stability_scope='RECEIPT_READ_WINDOW_ONLY',guest_booted=False,guest_runtime_verified=False,SDK_staging_verified=False,full_M10a2=False)
    changed=[rel(p) for p,wanted in observed.items() if digest(p.read_bytes())!=wanted];require(not changed,'凭证读取期间输入变化：'+repr(changed))
    receipt['receipt_read_inputs_unchanged']=True;save(HERE/'core-build-receipt.json',receipt)
    print(json.dumps(dict(status=receipt['status'],sources=len(source_files),d_files=len(d_files),generated_headers=receipt['generated_headers']['count'],core_bytes=len(packed),core_sha256=artifacts['fs/SYS/CORE/CORE.SKM']['sha256']),indent=2))
if __name__=='__main__':main()
