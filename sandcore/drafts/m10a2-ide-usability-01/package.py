"""从只读基盘装配补丁运行包；不使用或打包用户的test盘。"""
from pathlib import Path
import hashlib,json,shutil,sys,zipfile
ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'drafts/m10a2-p6-final-flow')]
from prepare_candidate import Candidate
OUT=ROOT/'build/m10a2-product-delivery-01'
SDK=ROOT/'build/m10a2-sdk-delivery-03'
E=ROOT/'evidence/m10a2-product-fix'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    review=json.loads((E/'review.json').read_bytes())
    assert review['status']=='REPORTED_STUDIO_FILES_PRODUCT_DEFECTS_VERIFIED' and all(review['checked'].values())
    core=ROOT/'build/m10a2-p6-final-15/core'
    candidate=Candidate(ROOT/review['native_report'],ROOT/review['host_report'],SDK,core,ROOT/'build/m10a1/sanddata.img')
    assert candidate.core_binding['core_sha256']==review['core_sha256']
    assert sha(ROOT/'user/SCAPI.H')=='81a730450fcb3d310ed841ad3e5c70a3fed32aa7fbd10acac8b888ac7da9ffc1'
    OUT.mkdir(exist_ok=False);runtime=OUT/'runtime';runtime.mkdir()
    shutil.copyfile(candidate.source,runtime/'sanddata.img')
    installation=candidate.install(runtime/'sanddata.img')
    for name in ('run.py','run.bat'):shutil.copyfile(ROOT/'drafts/m10a2-minimal-playable'/name,runtime/name)
    shutil.copyfile(core/'sandcore.img',runtime/'sandcore.img')
    assert set(p.name for p in runtime.iterdir())=={'run.py','run.bat','sandcore.img','sanddata.img'}
    (runtime/'run.bat').read_bytes().decode('ascii')
    manifest=json.loads((SDK/'MANIFEST.json').read_bytes())
    for name,row in manifest['files'].items():
        assert (SDK/name).stat().st_size==row['bytes'] and sha(SDK/name)==row['sha256']
    assets={}
    for name,folder in (('SandCore-M10a2.1.zip',runtime),('SandCore-M10a2.1-SDK.zip',SDK)):
        p=OUT/name
        with zipfile.ZipFile(p,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
            for f in sorted(folder.rglob('*')):
                if f.is_file():z.write(f,f.relative_to(folder).as_posix())
        with zipfile.ZipFile(p) as z:assert z.testzip() is None
        assets[name]=dict(bytes=p.stat().st_size,sha256=sha(p),entries=len(z.namelist()))
    sums=OUT/'SHA256SUMS'
    sums.write_bytes(''.join(v['sha256']+'  '+n+'\n' for n,v in assets.items()).encode('ascii'))
    record=dict(status='PRODUCT_PATCH_ASSETS_PREPARED',tag='M10a2.1',assets=assets,sums_sha256=sha(sums),
        core_sha256=review['core_sha256'],sdk_manifest_sha256=sha(SDK/'MANIFEST.json'),sdk_payload_files=len(manifest['files']),
        runtime_file_sha256={p.name:sha(p) for p in runtime.iterdir()},source_disk_unchanged=sha(candidate.source)==candidate.source_sha,
        user_test_disk_packaged=False,runtime_four_files_only=True,new_qemu_runs=0,new_compiler_runs=0,
        sdk_library_revisions={n:[v['abi_major'],v['abi_minor'],v['library_version']] for n,v in manifest['libraries'].items()})
    for p,r in ((OUT/'installation.json',installation),(E/'delivery.json',record)):
        p.write_bytes((json.dumps(r,ensure_ascii=False,indent=2)+'\n').encode())
    shutil.copyfile(sums,E/'SHA256SUMS')
    print(json.dumps(record['assets']))
if __name__=='__main__':main()
