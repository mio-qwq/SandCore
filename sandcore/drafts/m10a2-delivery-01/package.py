"""只做最终交付装盘/ZIP/摘要检查；不启动QEMU，不读用户试玩盘正文。"""
from pathlib import Path
from contextlib import ExitStack
import hashlib,json,shutil,sys,zipfile,subprocess
sys.dont_write_bytecode=True
ROOT=Path(__file__).resolve().parents[2];REPO=ROOT.parent
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'drafts/m10a2-p6-final-flow')]
from prepare_candidate import Candidate
from mkfs_m9 import fold,read_image
from mkfs_m10 import mapped,release_views
OUT=ROOT/'build/m10a2-delivery-01';SDK=ROOT/'build/m10a2-sdk-delivery-02'
EVIDENCE=ROOT/'evidence/m10a2-delivery-01'
SCAPI='81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1'
def sha(p):
    h=hashlib.sha256()
    with p.open('rb') as f:
        for b in iter(lambda:f.read(1048576),b''):h.update(b)
    return h.hexdigest()
def save(p,v):p.write_bytes((json.dumps(v,ensure_ascii=False,indent=2)+'\n').encode())
def main():
    assert not OUT.exists() and not EVIDENCE.exists()
    assert sha(ROOT/'user/SCAPI.H')==SCAPI
    subprocess.run(['git','diff','--check'],cwd=REPO,check=True,capture_output=True)
    manifest=json.loads((SDK/'MANIFEST.json').read_bytes())
    expected={n:(a,b,c,e) for n,a,b,c,e in (
        ('SCLDR.SCL',1,2,3,12),('SC32API.SCL',1,5,7,194),('SCCLIB.SCL',1,0,1,94),
        ('SCLIB.SCL',1,0,1,163),('SCUI.SCL',1,4,6,174),('SCGFX.SCL',1,0,1,56),
        ('SCSHELL.SCL',1,2,3,99),('SCTERM.SCL',1,0,1,19),('SCSOUND.SCL',1,0,1,5))}
    assert set(manifest['libraries'])==set(expected) and manifest['inputs_unchanged'] and not manifest['SDK_runtime_verified']
    versions={}
    for name,row in manifest['libraries'].items():
        got=(row['abi_major'],row['abi_minor'],row['library_version'],len(row['exports']))
        assert got==expected[name];versions[name]=list(got)
    for name,row in manifest['files'].items():
        assert sha(SDK/name)==row['sha256'] and (SDK/name).stat().st_size==row['bytes']
    for name in ('M10A2-SDK.md','SC32-AUTH.md','SESSION2.md','M10A2-ACCEPTANCE.md','M10A2-RELEASE.md','M10A2-DEFAULT-ENTRY.md','SCSHELL-IMPLEMENTATION.md'):
        assert sha(SDK/'SYS/SDK/DOC'/name)==sha(ROOT/'docs'/name)
    core=ROOT/'build/m10a2-p6-final-12/core';base=ROOT/'build/m10a1/sanddata.img'
    assert sha(base)=='3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64'
    candidate=Candidate(ROOT/'build/m10a2-p6-target-refresh-11/p6-generation.json',
        ROOT/'build/m10a2-main-session-return-01/p6-unified-host-21.json',SDK,core,base)
    assert candidate.core_binding['core_sha256']=='44d0b635f4f2597ef8009067de0da299bb36270c367589a8898711a64361f6b2'
    OUT.mkdir();runtime=OUT/'runtime';runtime.mkdir();shutil.copyfile(base,runtime/'sanddata.img')
    install=candidate.install(runtime/'sanddata.img')
    for name in ('run.py','run.bat'):
        shutil.copyfile(ROOT/'drafts/m10a2-minimal-playable'/name,runtime/name)
    shutil.copyfile(core/'sandcore.img',runtime/'sandcore.img')
    assert set(p.name for p in runtime.iterdir())=={'run.py','run.bat','sandcore.img','sanddata.img'}
    (runtime/'run.bat').read_bytes().decode('ascii')
    assert sha(runtime/'run.py')=='bf979ae2208b59f763b06a53027cea48852ca911887df64abf09efb19a5c14b8'
    # 安装器已逐对象回读；再按最终SDK清单核对盘内规范，防止打包旧文档盘。
    with ExitStack() as stack:
        records=read_image(mapped(stack,runtime/'sanddata.img'),False);stack.callback(lambda:release_views(records))
        for name,row in manifest['files'].items():
            if name.startswith('SYS/'):
                assert hashlib.sha256(records[fold(name)].payload).hexdigest()==row['sha256'],name
        assert 'HOME/M10A2/DROPUP.SCX' not in records
        record_count=len(records)
    assets={}
    for filename,folder in (('SandCore-M10a2.zip',runtime),('SandCore-M10a2-SDK.zip',SDK)):
        output=OUT/filename
        with zipfile.ZipFile(output,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for p in sorted(folder.rglob('*')):
                if p.is_file():z.write(p,p.relative_to(folder).as_posix())
        with zipfile.ZipFile(output) as z:
            assert z.testzip() is None
            expected_files={p.relative_to(folder).as_posix():p for p in folder.rglob('*') if p.is_file()}
            assert set(z.namelist())==set(expected_files) and len(z.namelist())==len(expected_files)
            for name,p in expected_files.items():
                h=hashlib.sha256()
                with z.open(name) as f:
                    for b in iter(lambda:f.read(1048576),b''):h.update(b)
                assert h.hexdigest()==sha(p)
        assets[filename]={'bytes':output.stat().st_size,'sha256':sha(output),'entries':len(expected_files)}
    (OUT/'SHA256SUMS').write_bytes(''.join(v['sha256']+'  '+n+'\n' for n,v in assets.items()).encode('ascii'))
    user_update=json.loads((ROOT/'build/m10a2-test-session-update-01/update.json').read_bytes())
    receipt={'status':'FINAL_DELIVERY_FILES_AND_SOURCE_PROVENANCE_CHECKED','sdk_directory':str(SDK.relative_to(ROOT)),
        'sdk_manifest_sha256':sha(SDK/'MANIFEST.json'),'sdk_payload_files':len(manifest['files']),
        'runtime_file_sha256':{p.name:sha(p) for p in runtime.iterdir()},'disk_records':record_count,
        'assets':assets,'sums_sha256':sha(OUT/'SHA256SUMS'),'library_versions_and_exports':versions,
        'core_binding':candidate.core_binding,'scapi_sha256':SCAPI,'source_disk_unchanged':sha(base)==candidate.source_sha,
        'all_sdk_payloads_and_packaged_doc_bytes_equal':True,'zip_crc_and_every_entry_sha256_checked':True,
        'new_qemu_runs':0,'new_compiler_runs':0,'runtime_evidence_reused_by_exact_unchanged_inputs':'docs/M10A2-ACCEPTANCE.md',
        'gui13_raw_resource_verdict':'FAIL','gui13_raw_unexplained_pages':8,'gui13_functional_checks_verified':True,
        'user_disk_migration':{'status':user_update['status'],'data_sha256':user_update['after']['sanddata.img'],
            'unlisted_objects_preserved':1626,'home_desktop_objects_preserved':34,'raw_user_disk_not_packaged':True},
        'signed_commit_created':False,'release_created':False}
    EVIDENCE.mkdir();save(EVIDENCE/'delivery.json',receipt)
    shutil.copyfile(Path(__file__).parent/'sdk-receipt.json',EVIDENCE/'sdk-receipt.json')
    shutil.copyfile(OUT/'SHA256SUMS',EVIDENCE/'SHA256SUMS')
    save(OUT/'installation.json',install)
    (EVIDENCE/'README.md').write_bytes(('''# M10a2最终交付检查

只装配一次独立干净运行盘及ZIP，无新QEMU或编译。HOST21/native11/final12运行字节保持；SDK按统一规范restage，8普通原生库、SCSOUND codec例外、原生G1/SCLD和完整来源严格检查。SCAPI冻结摘要、CORE实际发行表、盘内SDK规范、所有SDK文件及ZIP逐条CRC/SHA核对通过。运行ZIP严格4文件；SDK ZIP独立。用户test盘未打包，未再次修改。

GUI12零差与GUI13功能/资源FAIL边界见对应原始证据。delivery.json仅表示交付检查，不把staging当新运行验收。签名提交和Release执行结果由后续publish.json记录。
''').encode('utf-8'))
    print(json.dumps({'status':receipt['status'],'assets':assets,'sdk_files':len(manifest['files']),'disk_records':record_count}))
if __name__=='__main__':main()
