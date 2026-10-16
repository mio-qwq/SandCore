"""SDK最终增量的严格策略；只规划/核验复制，不构建或伪造guest来源。"""
from pathlib import Path
import hashlib,json,re

CODEC='SCSOUND.SCL'
CODEC_MANIFEST='user/scsound/SCSOUND.json'
CODEC_EXPORTS={'ScSoundOpen','ScSoundRead','ScSoundSeek','ScSoundQuery','ScSoundClose'}
CODEC_LICENSES={'dr_mp3','dr_flac'}
CODEC_FIXED={
    'dr_mp3':dict(source='user/audio/vendor',guest='SYS/LICENSE/DR_MP3',files={'dr_mp3.h':'997b7ee18de6e6b81e2a83f1ea9fc62aef25c62b28d48db95635f49e65de0a2f','LICENSE':'dd1c647e6f767f8ff4b2dfae0fed314726600a01e0cf1ef556afddd5fa96ff15'},local_files=['PROVENANCE.md','DR-MP3-NOTICE','MINIMP3-NOTICE']),
    'dr_flac':dict(source='user/audio/flacvendor',guest='SYS/LICENSE/DR_FLAC',files={'dr_flac.h':'111144e778f55738db6851cb226015c419e00d04b916a09506d4856d9cff945c','LICENSE':'dd1c647e6f767f8ff4b2dfae0fed314726600a01e0cf1ef556afddd5fa96ff15'},local_files=['PROVENANCE.md','DR-FLAC-NOTICE'])}
CODEC_HEADERS=('user/SCSOUND.H','user/SCTYPES.H','user/SC32API.H','user/SC32LEASE.H','user/SC32PATH.H','user/SC32SIMD.H')
PUBLIC_ADDITIONS=('SCUITABS.H','SC32REPLACE.H','SC32CONFIG.H','SC32CAPTURE.H','SCSHELLABSENT.H','SCSOUND.H','SC32TERMINAL.H','SCUITERM.H')
OPTIONAL_AUTH=('SC32AUTH.H','SCUIAUTH.H')
SOURCE_SUFFIXES={'.c','.h','.inc','.asm','.json','.s'}
NATIVE_ENTRIES={
    'SCLDR.SCL':'user/runtime/SCLROOT.C','SC32API.SCL':'user/sc32/SC32API.C',
    'SCCLIB.SCL':'user/stdlib/SCCLIB.C','SCLIB.SCL':'user/sclib/SCLIB.C',
    'SCUI.SCL':'user/scui/SCUI.C','SCSHELL.SCL':'user/scshell/SCSHELL.C',
    'SCGFX.SCL':'user/scgfx/SCGFX.C','SCTERM.SCL':'user/scterm/SCTERM.C'}

def configure(root,libraries,headers,docs):
    manifest,roots=libraries['SC32API.SCL']
    additions=['user/audioapi/','user/configapi/']
    for include in re.findall(r'(?m)^\s*#\s*include\s+"([^"]+\.c)"',(root/'user/sc32/SC32API.C').read_text(encoding='utf-8')):
        source=(root/'user/sc32'/include).resolve(strict=True)
        if not source.is_relative_to(root/'user'):raise ValueError('SC32实现include离开user源码')
        additions.append(source.parent.relative_to(root).as_posix()+'/')
    libraries['SC32API.SCL']=(manifest,tuple(dict.fromkeys(roots+tuple(additions))))
    libraries['SCSHELL.SCL']=('user/scshell/SCSHELL.json',('user/scshell/',))
    libraries['SCTERM.SCL']=('user/scterm/SCTERM.JSON',('user/scterm/',))
    libraries[CODEC]=(CODEC_MANIFEST,('user/scsound/',))
    for name in PUBLIC_ADDITIONS:headers[name]='user/'+name
    for name in OPTIONAL_AUTH:
        if (root/'user'/name).is_file():headers[name]='user/'+name
    # 新正式文档只在主代理同批整合后加入，不把缺失文档伪造为空文件。
    additions=('SC32-AUDIO.md','SCSOUND.md','SC32-CONFIG.md','SC32-TERMINAL.md','SCTERM.md','SDK-RESEARCHDESK.md')
    return tuple(dict.fromkeys(docs+tuple(name for name in additions if (root/'docs'/name).is_file())))

def required_sources(root,roots,extra=()):
    result=set(extra)
    for folder in roots:
        base=root/folder
        if not base.is_dir():raise ValueError('SDK缺实际实现目录：'+folder)
        result.update(p.relative_to(root).as_posix() for p in base.rglob('*') if p.is_file() and p.suffix.lower() in SOURCE_SUFFIXES)
    return result

def native_sources(root,folder):
    # Desktop是独立应用，不能因应用改布局而迫使未变SCSHELL库重编。
    return [p for p in (root/folder).rglob('*') if p.is_file() and p.suffix.lower() in SOURCE_SUFFIXES-{'.json'}
            and not (folder=='user/scshell/' and p.is_relative_to(root/'user/scshell/desktop'))]

def implementation_headers(root,roots,extra,entry=None):
    # 公共声明位于库源目录外；按实际literal include补闭包，避免新Secret/PTY头被固定列表漏掉。
    # 原生来源核实际聚合入口的完整literal include闭包；目录中未接线的
    # 历史草稿仍由required_sources完整归档并按HOST输入固定，但它们没有
    # 参加本库编译，不能反过来要求一份虚假的客体编译来源。
    seeds=[root/entry] if entry else [p for folder in roots for p in native_sources(root,folder)]
    required=set(extra)|{p.relative_to(root).as_posix() for p in seeds}
    pending=seeds+[root/name for name in extra];seen=set()
    while pending:
        file=pending.pop().resolve(strict=True)
        if file in seen:continue
        seen.add(file)
        if not file.is_relative_to(root/'user'):raise ValueError('库实现include越过user源边界')
        for local,standard in re.findall(r'(?m)^\s*#\s*include\s*(?:"([^"]+)"|<([^>]+)>)',file.read_text(encoding='utf-8')):
            name=local or standard;dirs=([file.parent] if local else [])+[root/'user/stdlib',root/'user',root/'user/sclib']
            match=next((base/name for base in dirs if (base/name).is_file()),None)
            if match is None:raise ValueError('库实现include闭包缺项：'+str(file)+' -> '+name)
            match=match.resolve(strict=True);required.add(match.relative_to(root).as_posix());pending.append(match)
    return required

def select_manifest(root,name,host,profile):
    if profile!='native-preview' or name==CODEC:return host
    path=Path(host);native=path.with_name(path.stem+'.NATIVE.JSON').as_posix()
    return native if (root/native).is_file() else host

def codec_sources(root):
    files=required_sources(root,('user/scsound/','user/codec/include/'),CODEC_HEADERS)
    files.update(('user/audio/vendor/dr_mp3.h','user/audio/flacvendor/dr_flac.h'))
    files.update('user/stdlib/'+name for name in ('stddef.h','stdint.h','stdlib.h','string.h','limits.h'))
    # ADAPTATION是来源记录；实现证明仍要求全部实际C/H，不能只核外层名字。
    files.discard('user/scsound/ADAPTATION.json')
    return files

def codec_components(index):
    components=[v for v in index['components'] if v['name'] in CODEC_LICENSES]
    if len(components)!=2 or {v['name'] for v in components}!=CODEC_LICENSES:raise ValueError('codec缺唯一完整MP3/FLAC许可')
    for component in components:
        if component.get('license')!='MIT-0' or component.get('upstream')!='https://github.com/mackron/dr_libs' or component.get('revision')!='dfe8377631000664666519fdb83da193fd8037f4':raise ValueError('codec固定许可/源版本变化，不能继承白名单')
        if any(component.get(key)!=value for key,value in CODEC_FIXED[component['name']].items()):raise ValueError('codec固定源目录/原件/完整notice变化')
        if component['name']=='dr_mp3' and component.get('embedded')!=[dict(name='minimp3',upstream='https://github.com/lieff/minimp3',license='CC0-1.0',revision='embedded-in-dr_mp3-snapshot')]:raise ValueError('codec嵌套minimp3许可缺失')
    return components

def host_codec(stage,proof_path,manifest,inputs):
    proof=inputs.json(stage.inside(proof_path,stage.ROOT))
    fields={'schema','mode','name','build_report','build_report_sha256','artifact','sha256','bytes','source_input_sha256','license_manifest'}
    if set(proof)!=fields or proof['schema']!=1 or proof['mode']!='USER_APPROVED_HOST_CODEC_EXCEPTION' or proof['name']!=CODEC:
        raise ValueError('宿主例外仅接受固定SCSOUND.SCL codec证明')
    if manifest['name']!=CODEC or {row['name'] for row in manifest['exports']}!=CODEC_EXPORTS:
        raise ValueError('codec公开合同不是冻结SCSOUND五入口')
    report_path=stage.relative(proof['build_report']);report=inputs.json(report_path,proof['build_report_sha256'])
    if report.get('exit_code')!=0 or report.get('source_files_modified_during_build')!=[] or not str(report.get('scope','')).startswith('M10A2_'):
        raise ValueError('codec缺实际成功且源码守恒的host构建报告')
    recorded={}
    for row in report.get('inputs',[]):
        if row['path'] in recorded:raise ValueError('codec host报告含重复输入路径')
        recorded[row['path']]=row['sha256']
    outputs=[row for row in report.get('outputs',[]) if row['path']==proof['artifact']]
    if len(outputs)!=1 or outputs[0].get('sha256')!=proof['sha256'] or outputs[0].get('bytes')!=proof['bytes'] or Path(proof['artifact']).name!=CODEC:
        raise ValueError('codec不是原host报告的精确输出')
    required=codec_sources(stage.ROOT)|{CODEC_MANIFEST}
    if set(proof['source_input_sha256'])!=required:raise ValueError('codec proof源码闭包不完整或含任意豁免路径')
    for name in sorted(required):
        expected=proof['source_input_sha256'][name]
        if recorded.get(name)!=expected:raise ValueError('codec源码不是实际host构建输入：'+name)
        inputs.read(stage.relative(name),expected)
    if proof['license_manifest']!='third_party/M9-SOURCES.json':raise ValueError('codec必须使用原M9固定MP3/FLAC许可')
    license_index=inputs.json(stage.relative(proof['license_manifest']))
    components=codec_components(license_index)
    for component in components:
        for file,digest in component['files'].items():inputs.read(stage.relative(component['source']+'/'+file),digest)
        for file in component.get('local_files',[]):inputs.read(stage.relative(component['source']+'/'+file))
    body=inputs.read(stage.relative(proof['artifact']),proof['sha256'],proof['bytes']);table=stage.contract(body,manifest)
    return body,table,dict(source='USER_APPROVED_HOST_CODEC_EXCEPTION_NOT_GUEST_NATIVE',name=CODEC,
        proof=str(proof_path),proof_sha256=stage.sha(proof_path),origin_report=str(report_path),origin_report_sha256=stage.sha(report_path),
        generation_source_input_sha256=proof['source_input_sha256'],bytes=len(body),sha256=stage.digest(body),
        license_manifest=proof['license_manifest'],license_components=sorted(CODEC_LICENSES),staging_is_execution_evidence=False)

def native_linker(stage,path,inputs,recorded):
    value,report,report_path,out=stage.provenance(path,inputs);item=value.get('applications',{}).get('SCLD.SCX')
    if not item:raise ValueError('linker provenance缺实际SCLD.SCX')
    target=Path(item['path']);target=target if target.is_absolute() else path.parent/target
    body=inputs.read(target,item['sha256'],item['bytes'],out);generation=report;generation_path=report_path
    origin=item.get('generation_origin');source=value['source_input_sha256']
    if origin:
        if value.get('source_snapshot_scope')!='CURRENT_REFRESH_PREPARATION_ONLY_NOT_COMMON_GENERATION_ORIGIN' or value.get('artifact_generation',{}).get('SCLD.SCX')!=origin:
            raise ValueError('SCLD逐产物来源未分层')
        if origin['mode']=='REUSED':
            generation_path=stage.inside(Path(origin['report']),stage.ROOT);generation=inputs.json(generation_path,origin['report_sha256'])
            old=generation.get('native_artifacts',{}).get('SCLD.SCX',{})
            if any(old.get(key)!=item.get(key) for key in ('sha256','bytes','compile_case')) or item.get('generated_this_vm') is not False:raise ValueError('SCLD复用不是原byte')
        elif origin['mode']!='FRESH' or item.get('generated_this_vm') is not True:raise ValueError('SCLD未知generation类型')
        source=origin['source_input_sha256']
    if not str(generation.get('status','')).startswith('DECLARED_') or not generation['status'].endswith('PASS') or generation.get('current_inputs_unchanged') is not True or generation.get('source_disk_unchanged') is not True:
        raise ValueError('SCLD原生成报告不是守恒成功轮')
    cases=[row for row in generation['cases'] if row['name']==item['compile_case']]
    if len(cases)!=1 or cases[0].get('status')!='PASS' or cases[0].get('exit_code')!=0 or cases[0].get('generated_sha256')!=item['sha256'] or cases[0].get('generated_bytes')!=len(body) or item.get('guest_sccc_compiled') is not True:
        raise ValueError('SCLD没有真实guest compiler case')
    from scd_format import validate_scx
    parsed=validate_scx(body)
    required={'user/scld.c','user/SCOFMT.H','user/SCDBGFILE.H','user/runtime/SCLFMT.H','user/runtime/SCDYN.H','user/runtime/scl_format.c','user/runtime/scd_format.c'}
    # 链接器自身包含的公共布局和标准声明也是生成输入，不能只核main C而漏掉头漂移。
    required.update('user/'+name for name in ('SCTYPES.H','SC32API.H','SC32GUI.H','SC32FONT.H','SC32PALETTE.H','SC32POPUP.H'))
    required.update('user/stdlib/'+name for name in ('stdio.h','stdlib.h','string.h','stddef.h','stdarg.h'))
    for name in required:
        if name not in source or generation.get('input_sha256',{}).get(name)!=source[name] or recorded.get(name)!=source[name]:raise ValueError('SCLD缺自身精确来源：'+name)
        inputs.read(stage.relative(name),source[name])
    selected=select_manifest(stage.ROOT,'SCLD.SCX','user/SCLD.json','native-preview');declared=inputs.json(stage.relative(selected))
    dynamic_contract(stage,parsed,declared)
    if cases[0].get('stdout_path'):inputs.read(Path(cases[0]['stdout_path']),cases[0]['stdout_sha256'],cases[0]['stdout_bytes'])
    return body,dict(source='ACTUAL_GUEST_SCCC_SCLD_CLI',bytes=len(body),sha256=stage.digest(body),origin_report=str(generation_path),origin_report_sha256=stage.sha(generation_path),generation_case=item['compile_case'],generation_source_input_sha256=source,staging_is_execution_evidence=False),parsed

def dynamic_contract(stage,table,manifest):
    kinds={'function':1,'data':2}
    declared={(v['name'],v['dependency'],kinds[v['kind']],v['size']) for v in manifest['imports']}
    actual={(v['name'],v['dependency'],v['kind'],v['size']) for v in table['imports']}
    deps=lambda rows:{(v['name'],v['abi_major'],v['min_minor'],v['min_version']) for v in rows}
    if manifest.get('kind')!='application' or declared!=actual or len(declared)!=len(manifest['imports']) or deps(manifest['dependencies'])!=deps(table['dependencies']):
        raise ValueError('原生SCX动态表与所选精确manifest不符')

def extra_headers(name):
    if name=='SCSHELL.SCL':return ('user/SCSHELL.H','user/SCSHELLABSENT.H','user/SC32LEASE.H','user/SC32REPLACE.H')
    if name=='SCTERM.SCL':return ('user/SCUITERM.H','user/SCUI.H','user/SC32API.H','user/sclib/SCLIB.H')
    if name=='SCUI.SCL':return ('user/SCUI.H','user/SCUITABS.H','user/SCUIARGB.H','user/SC32API.H','user/SC32BROKER.H','user/SC32DROP.H','user/SC32FONT.H','user/sclib/SCLIB.H','user/textdata/SCUTEXTDATA.H')
    return None

def install_sources(stage,inputs,recorded,add):
    files=set()
    for _,roots in stage.LIBRARIES.values():files.update(required_sources(stage.ROOT,roots))
    for folder in ('user/systemtools/','user/sound2/','user/studio2/','user/debug2/','user/files2/','user/notes2/','user/terminal2/'):
        if (stage.ROOT/folder).is_dir():files.update(required_sources(stage.ROOT,(folder,)))
    files.update(codec_sources(stage.ROOT))
    files.update(p.relative_to(stage.ROOT).as_posix() for p in (stage.ROOT/'user').glob('s3c*') if p.is_file())
    files.update(('user/sccc.c','user/scld.c','user/SCLD.json','user/SCAPI.H','user/SCIO.H','user/COMPILECLI.inc','user/sccc_include.inc'))
    for source in stage.HEADERS.values():files.add(source)
    for name in stage.STANDARD:files.add('user/stdlib/'+name)
    for name in sorted(files):
        body=inputs.read(stage.relative(name),recorded.get(name));add('SYS/SDK/SRC/'+name,body,dict(source=name,build_member=name in recorded,scope='SOURCE_ONLY_NOT_PUBLIC_INC'))

def install_researchdesk(stage,inputs,add):
    base=stage.ROOT/'examples/m10a2-sdk/researchdesk'
    if not base.is_dir():raise ValueError('缺独立多TU ResearchDesk源码目录')
    allowed={'.c','.h','.json','.scp','.rsp','.sh','.md','.rdt'}
    for file in sorted(base.iterdir()):
        if file.is_file() and file.suffix.lower() in allowed:add('SYS/SDK/EXAMPLES/RDESK/'+file.name.upper(),inputs.read(file),str(file))
