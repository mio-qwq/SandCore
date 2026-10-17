"""只更新现有试用盘的明确系统清单，备份原盘并保留全部其它对象。"""
from pathlib import Path
import hashlib,json,os,shutil,sys
ROOT=Path(__file__).resolve().parents[2]
sys.dont_write_bytecode=True
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'drafts/m10a2-p6-final-flow'),str(ROOT/'drafts/m10a2-session-return-01')]
from prepare_candidate import Candidate
from verify_m10a2_scl import prepare
import importlib.util
spec=importlib.util.spec_from_file_location('previous_update_safety',ROOT/'drafts/m10a2-session-return-01/update_test.py')
safety=importlib.util.module_from_spec(spec);spec.loader.exec_module(safety)
TEST=Path('C:/Users/Administrator/Desktop/test').resolve()
OUT=ROOT/'build/m10a2-test-product-update-01'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(r):(OUT/'update.json').write_bytes((json.dumps(r,ensure_ascii=False,indent=2)+'\n').encode())
def main():
    review=ROOT/'evidence/m10a2-product-fix/review.json'
    r=json.loads(review.read_bytes())
    assert r['status']=='REPORTED_STUDIO_FILES_PRODUCT_DEFECTS_VERIFIED' and all(r['checked'].values()) and r['qemu_closed']
    core=ROOT/'build/m10a2-p6-final-15/core'
    assert sha(core/'fs/SYS/CORE/CORE.SKM')==r['core_sha256']
    assert set(p.name for p in TEST.iterdir())=={'run.py','run.bat','sandcore.img','sanddata.img'}
    assert sha(core/'sandcore.img')==sha(TEST/'sandcore.img')
    candidate=Candidate(ROOT/r['native_report'],ROOT/r['host_report'],ROOT/'build/m10a2-sdk-delivery-03',core,ROOT/'build/m10a1/sanddata.img')
    if OUT.exists():
        previous=json.loads((OUT/'update.json').read_bytes())
        assert previous['status']=='STARTED_NOT_DELIVERED'
        assert set(p.name for p in OUT.iterdir())=={'update.json'}
    else:OUT.mkdir(exist_ok=False)
    data=TEST/'sanddata.img'
    record=dict(status='STARTED_NOT_DELIVERED',review_sha256=sha(review),target=str(data),core_sha256=r['core_sha256'])
    save(record)
    with safety.deny_writers(data):
        before={p.name:sha(p) for p in TEST.iterdir()}
        backup=OUT/'sanddata-before.img'
        shutil.copyfile(data,backup)
        assert sha(backup)==before['sanddata.img']
        record.update(before=before,backup=str(backup),backup_sha256=sha(backup));save(record)
        working=OUT/'sanddata.img';shutil.copyfile(backup,working)
        extra,metadata,plan=candidate.plan(working)
        record['metadata_plan']=plan;save(record)
        result=prepare(working,core,extra,runtime_fixtures=False,metadata=metadata)
        # prepare已逐对象核对清单之外的正文、身份、权限与generation。
        assert sha(data)==before['sanddata.img'] and sha(backup)==before['sanddata.img']
    # 关闭准备时的保护句柄后再提交；原盘有变化就停止，不覆盖用户新写入。
    assert sha(data)==before['sanddata.img']
    os.replace(working,data)
    after={p.name:sha(p) for p in TEST.iterdir()}
    assert set(after)==set(before) and all(after[n]==before[n] for n in ('run.py','run.bat','sandcore.img'))
    record.update(status='SYSTEM_PAYLOAD_UPDATED_USER_OBJECTS_PRESERVED',after=after,prepare_result=result,
        all_unlisted_objects_byte_metadata_generation_preserved=True,launcher_bat_boot_unchanged=True,target_four_files_only=True)
    save(record)
    print(json.dumps(dict(status=record['status'],data_sha256=after['sanddata.img'],backup=str(backup))))
if __name__=='__main__':main()
