"""从已实际运行的native06/final06装配四文件试玩副本；不复制活VM磁盘。"""
from pathlib import Path
import hashlib,json,os,shutil,sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'drafts/m10a2-p6-final-flow'))
from prepare_candidate import Candidate

def sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda:stream.read(1048576),b''):h.update(chunk)
    return h.hexdigest()

destination=Path(r'C:\Users\Administrator\Desktop\test').resolve(strict=True)
assert destination==Path(r'C:\Users\Administrator\Desktop\test').resolve()
assert destination.is_dir() and not list(destination.iterdir()),'试玩目录必须为空；保留既有用户文件'
out=ROOT/'build/m10a2-minimal-playable-01'
assert not out.exists()
native=ROOT/'build/m10a2-p6-target-refresh-06/p6-generation.json'
host=ROOT/'build/m10a2-main-studio-01/p6-unified-host-12.json'
sdk=ROOT/'build/m10a2-p6-final-06/sdk'
core=ROOT/'build/m10a2-p6-final-06/core'
original=ROOT/'build/m10a1/sanddata.img'
assert sha(native)=='f2f5d67a8946ebe07c12459e17d85f00cd6538a38f785298d11194fd4dd1c7ba'
assert sha(host)=='7e29017e4c20095c0cf8ec2e5bbcd9ae90626fe974d12dfae4596d641548e750'
assert sha(core/'fs/SYS/CORE/CORE.SKM')=='5da64fc176cb273803fddd328b02d5123b726440673689cf20aa5e48a3464eff'
original_sha=sha(original)
assert original_sha=='3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64'
candidate=Candidate(native,host,sdk,core,original)
out.mkdir()
image=out/'sanddata.img'
shutil.copyfile(original,image)
assert sha(image)==original_sha
installation=candidate.install(image)
# 所有生成、原对象及权限回读仍走原Candidate.install；核清绝对源/目标
# 后同卷移动已装配的单个文件，避免再长期保留一份256MiB中间盘。
assert image.resolve().is_relative_to((ROOT/'build').resolve())
target=destination/'sanddata.img'
assert target.parent.resolve()==destination and not target.exists()
os.replace(image,target)
shutil.copyfile(core/'sandcore.img',destination/'sandcore.img')
shutil.copyfile(HERE/'run.py',destination/'run.py')
shutil.copyfile(HERE/'run.bat',destination/'run.bat')
assert set(p.name for p in destination.iterdir())=={'run.py','run.bat','sandcore.img','sanddata.img'}
assert (destination/'run.bat').read_bytes().isascii()
assert sha(original)==original_sha and not candidate.input_pins.unchanged()
files={p.name:dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(destination.iterdir())}
report=dict(status='CURRENT_NATIVE06_PLAYABLE_COPY_ASSEMBLED_NOT_NEW_GUEST_ACCEPTANCE',
    destination=str(destination),host_report_sha256=sha(host),native_report_sha256=sha(native),
    core_sha256=sha(core/'fs/SYS/CORE/CORE.SKM'),source_disk_sha256=original_sha,
    source_disk_unchanged=True,live_vm_disk_copied=False,current_vm_still_separate=True,
    original_inputs_unchanged=True,installation=installation,files=files,
    user_visible_files_exactly_four=True,full_M10a2=False)
(out/'delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(status=report['status'],destination=str(destination),files=files),indent=2))
