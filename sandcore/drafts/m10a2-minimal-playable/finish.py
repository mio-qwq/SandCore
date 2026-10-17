"""装配后缺BAT的明确恢复：只补入口、只读核对现有两盘，不重新复制。"""
from pathlib import Path
from contextlib import ExitStack
import hashlib,json,shutil,sys
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'drafts/m10a2-p6-final-flow'))
from prepare_candidate import Candidate
from mkfs_m9 import read_image,fold
from mkfs_m10 import mapped,release_views,digest_payload
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
dest=Path(r'C:\Users\Administrator\Desktop\test').resolve(strict=True)
assert dest==Path(r'C:\Users\Administrator\Desktop\test').resolve()
assert set(p.name for p in dest.iterdir())=={'run.py','sandcore.img','sanddata.img'}
native=ROOT/'build/m10a2-p6-target-refresh-06/p6-generation.json'
host=ROOT/'build/m10a2-main-studio-01/p6-unified-host-12.json'
core=ROOT/'build/m10a2-p6-final-06/core';sdk=ROOT/'build/m10a2-p6-final-06/sdk'
original=ROOT/'build/m10a1/sanddata.img'
assert sha(original)=='3db2e3cd9262343204e8c19af02847e98244732c5b5ad4397dd334be84fe7e64'
assert sha(native)=='f2f5d67a8946ebe07c12459e17d85f00cd6538a38f785298d11194fd4dd1c7ba'
assert sha(host)=='7e29017e4c20095c0cf8ec2e5bbcd9ae90626fe974d12dfae4596d641548e750'
c=Candidate(native,host,sdk,core,original)
extra,metadata,plan=c.plan(original)
with ExitStack() as stack:
    old=read_image(mapped(stack,original),False);stack.callback(lambda:release_views(old))
    actual=read_image(mapped(stack,dest/'sanddata.img'),False);stack.callback(lambda:release_views(actual))
    for name,body in extra.items():
        row=actual[fold(name)]
        assert digest_payload(row.payload)==hashlib.sha256(body).hexdigest(),name
        assert (row.uid,row.gid,row.mode)==metadata[fold(name)],name
    controlled={fold(name) for name in extra}
    preserved=0
    for key,row in old.items():
        if key in controlled:continue
        value=actual[key]
        assert (digest_payload(value.payload),value.uid,value.gid,value.mode,value.generation)==(
            digest_payload(row.payload),row.uid,row.gid,row.mode,row.generation),key
        preserved+=1
assert sha(dest/'sandcore.img')==sha(core/'sandcore.img')
assert sha(dest/'run.py')==sha(HERE/'run.py')
bat=(HERE/'run.bat').read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
assert bat.isascii();(HERE/'run.bat').write_bytes(bat)
shutil.copyfile(HERE/'run.bat',dest/'run.bat')
assert set(p.name for p in dest.iterdir())=={'run.py','run.bat','sandcore.img','sanddata.img'}
assert not c.input_pins.unchanged()
assert sha(original)==c.source_sha
files={p.name:dict(bytes=p.stat().st_size,sha256=sha(p)) for p in sorted(dest.iterdir())}
report=dict(status='CURRENT_NATIVE06_PLAYABLE_COPY_READBACK_VERIFIED_NOT_NEW_GUEST_ACCEPTANCE',
    destination=str(dest),files=files,native_report_sha256=sha(native),host_report_sha256=sha(host),
    core_sha256=sha(core/'fs/SYS/CORE/CORE.SKM'),source_disk_sha256=c.source_sha,
    source_disk_unchanged=True,live_vm_disk_copied=False,all_planned_payload_and_metadata_checked=True,
    preserved_other_objects=preserved,payload_files=len(extra),original_inputs_unchanged=True,
    initial_assembly='BAT_MISSING_AFTER_DISK_INSTALL_RECOVERED_WITHOUT_NEW_DISK_COPY',
    user_visible_files_exactly_four=True,full_M10a2=False)
(ROOT/'build/m10a2-minimal-playable-01/delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(status=report['status'],files=files),indent=2))
